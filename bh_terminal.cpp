#ifdef _WIN32
#include "bh_engine.hpp"

static void getTermSize(int& cols, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else { cols=80; rows=24; }
}

void runTerminal(Engine& eng) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE), hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD outMode=0, inMode=0;
    GetConsoleMode(hOut,&outMode); GetConsoleMode(hIn,&inMode);
    UINT prevCP = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleMode(hOut, outMode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleMode(hIn, (inMode&~ENABLE_QUICK_EDIT_MODE)|ENABLE_MOUSE_INPUT|ENABLE_EXTENDED_FLAGS);
    glfwHideWindow(eng.window);

    float baseAz=camera.azimuth, baseEl=1.35f, offAz=0.0f, offEl=0.0f, tgtAz=0.0f, tgtEl=0.0f;
    int prevCols=-1, prevRows=-1;
    bool running=true;
    const int SS=1, LIT=20;
    std::vector<unsigned char> px, dn;
    std::string frame;
    std::printf("\x1b[?25l\x1b[2J");

    while (running && !glfwWindowShouldClose(eng.window)) {
        int cols, rows; getTermSize(cols, rows);
        int drawRows = std::max(4, rows-1);
        int tw = std::max(8, cols), th = drawRows*2;
        int ssW = tw*SS, ssH = th*SS;
        if (cols!=prevCols||rows!=prevRows) {
            eng.COMPUTE_WIDTH=eng.COMPUTE_MOVING_WIDTH=ssW;
            eng.COMPUTE_HEIGHT=eng.COMPUTE_MOVING_HEIGHT=ssH;
            eng.WIDTH=ssW; eng.HEIGHT=ssH;
            std::printf("\x1b[2J"); prevCols=cols; prevRows=rows;
        }

        DWORD pending=0; GetNumberOfConsoleInputEvents(hIn, &pending);
        if (pending) {
            INPUT_RECORD recs[64]; DWORD got=0;
            ReadConsoleInput(hIn, recs, std::min<DWORD>(pending,64), &got);
            for (DWORD i=0;i<got;++i) {
                if (recs[i].EventType==KEY_EVENT&&recs[i].Event.KeyEvent.bKeyDown) {
                    WORD vk=recs[i].Event.KeyEvent.wVirtualKeyCode;
                    if (vk=='Q'||vk==VK_ESCAPE) running=false;
                    else if (vk==VK_LEFT)  baseAz-=0.10f;
                    else if (vk==VK_RIGHT) baseAz+=0.10f;
                    else if (vk==VK_UP)    baseEl=glm::clamp(baseEl-0.08f,0.01f,float(M_PI)-0.01f);
                    else if (vk==VK_DOWN)  baseEl=glm::clamp(baseEl+0.08f,0.01f,float(M_PI)-0.01f);
                    else if (vk==VK_OEM_PLUS||vk==VK_ADD)     { camera.processScroll(0,+1); saveSessionState("terminal"); }
                    else if (vk==VK_OEM_MINUS||vk==VK_SUBTRACT){ camera.processScroll(0,-1); saveSessionState("terminal"); }
                    else if (vk=='M') showGrid=!showGrid;
                    else if (vk=='G') Gravity=!Gravity;
                    else if (vk==VK_OEM_4) { eng.COMPUTE_STEPS=std::max(100,eng.COMPUTE_STEPS-100); eng.COMPUTE_MOVING_STEPS=eng.COMPUTE_STEPS; saveSessionState("terminal"); }
                    else if (vk==VK_OEM_6) { eng.COMPUTE_STEPS=std::min(2000,eng.COMPUTE_STEPS+100); eng.COMPUTE_MOVING_STEPS=eng.COMPUTE_STEPS; saveSessionState("terminal"); }
                } else if (recs[i].EventType==MOUSE_EVENT) {
                    COORD mp=recs[i].Event.MouseEvent.dwMousePosition;
                    tgtAz=glm::clamp(2.0f*float(mp.X)/float(std::max(1,cols))-1.0f,-1.0f,1.0f)*0.5f;
                    tgtEl=glm::clamp(2.0f*float(mp.Y)/float(std::max(1,rows))-1.0f,-1.0f,1.0f)*0.3f;
                }
            }
        }
        offAz+=(tgtAz-offAz)*0.10f; offEl+=(tgtEl-offEl)*0.10f; baseAz+=0.0035f;
        camera.azimuth=baseAz+offAz;
        camera.elevation=glm::clamp(baseEl+offEl,0.01f,float(M_PI)-0.01f);
        camera.moving=false; camera.update();

        eng.dispatchCompute(camera, 0);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);

        size_t texBytes=size_t(ssW)*ssH*4;
        eng.initPBO(texBytes);
        const unsigned char* pboPtr=eng.pboBeginRead(GL_RGBA);
        px.resize(texBytes);
        if (pboPtr) memcpy(px.data(), pboPtr, texBytes);
        eng.pboEndRead();

        dn.assign(size_t(tw)*th*3, 0);
        for (int oy=0;oy<th;++oy) {
            int srcY=ssH-1-oy;
            const unsigned char* row=&px[size_t(srcY)*ssW*4];
            for (int ox=0;ox<tw;++ox) {
                const unsigned char* p=&row[ox*4];
                unsigned char* o=&dn[(size_t(oy)*tw+ox)*3];
                o[0]=p[0]; o[1]=p[1]; o[2]=p[2];
            }
        }

        frame.clear(); frame.reserve(size_t(tw)*drawRows*14+64);
        frame+="\x1b[H";
        long curFg=-2, curBg=-2;
        auto cell  =[&](int x,int y)->const unsigned char*{return &dn[(size_t(y)*tw+x)*3];};
        auto isLit =[&](const unsigned char* c){return (c[0]+c[1]+c[2])>LIT;};
        auto fgCol =[&](const unsigned char* c){
            long k=(long(c[0])<<16)|(c[1]<<8)|c[2];
            if(k!=curFg){frame+="\x1b[38;2;";frame+=numStrs[c[0]];frame+=";";frame+=numStrs[c[1]];frame+=";";frame+=numStrs[c[2]];frame+="m";curFg=k;}
        };
        auto bgCol =[&](const unsigned char* c){
            long k=(long(c[0])<<16)|(c[1]<<8)|c[2];
            if(k!=curBg){frame+="\x1b[48;2;";frame+=numStrs[c[0]];frame+=";";frame+=numStrs[c[1]];frame+=";";frame+=numStrs[c[2]];frame+="m";curBg=k;}
        };
        auto bgDef =[&](){if(curBg!=-1){frame+="\x1b[49m";curBg=-1;}};
        for (int r=0;r<drawRows;++r) {
            for (int x=0;x<tw;++x) {
                const unsigned char* t=cell(x,r*2), *b=cell(x,r*2+1);
                bool tl=isLit(t), bl=isLit(b);
                if (tl&&bl){ fgCol(t); bgCol(b); frame+="\xe2\x96\x80"; }
                else if (tl){ fgCol(t); bgDef(); frame+="\xe2\x96\x80"; }
                else if (bl){ fgCol(b); bgDef(); frame+="\xe2\x96\x84"; }
                else { bgDef(); frame+=" "; }
            }
            bgDef(); frame+="\n";
        }
        frame+="\x1b[0m  black hole  \xc2\xb7  arrows: orbit   +/-: zoom   q: quit ";
        std::fwrite(frame.data(), 1, frame.size(), stdout); std::fflush(stdout);
        glfwPollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    std::printf("\x1b[0m\x1b[?25h\x1b[2J\x1b[H"); std::fflush(stdout);
    SetConsoleOutputCP(prevCP);
    SetConsoleMode(hOut, outMode); SetConsoleMode(hIn, inMode);
}
#endif // _WIN32
