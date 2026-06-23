#ifdef _WIN32
#include "bh_engine.hpp"

static void getTermSize(int& cols, int& rows) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else { cols=80; rows=24; }
}

// Rendered signature of one terminal cell (a vertical pair of pixels drawn as a
// half-block). Compared post-threshold so the frame-diff skips cells that look
// identical even when the raw RGB jitters sub-threshold (e.g. faint background).
//   g: 0=blank  1=full block (fg=top,bg=bottom)  2=top only  3=bottom only
struct TCell {
    unsigned char g=0, fr=0, fg=0, fb=0, br=0, bg=0, bb=0, pad=0;
    bool operator==(const TCell& o) const {
        return g==o.g && fr==o.fr && fg==o.fg && fb==o.fb
            && br==o.br && bg==o.bg && bb==o.bb;
    }
};

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
    const int LIT=20;
    std::vector<unsigned char> px, dn;
    std::vector<TCell> cur, prev;   // frame-diff: current vs last drawn cells
    std::string frame;
    // Optional diagnostic: set BH_TERM_STATS=1 to print byte/frame accounting at exit.
    bool wantStats = std::getenv("BH_TERM_STATS") != nullptr;
    // Escape hatch: BH_TERM_NODIFF=1 forces a full repaint every frame (pre-diff
    // behaviour) — handy if a terminal mis-renders incremental updates.
    bool noDiff = std::getenv("BH_TERM_NODIFF") != nullptr;
    long statFrames=0; unsigned long long statBytes=0; size_t statFullFrame=0;
    // Compute-resolution ceiling. The terminal cell grid (driven by the WT font
    // size: Ctrl +/- zooms the font -> changes rows/cols) would otherwise dictate
    // how many pixels the GPU ray-marches each frame, so a small font explodes the
    // per-frame cost. Cap it and nearest-upscale to the cells; override with
    // BH_TERM_MAXRES=WxH (e.g. 320x240 for sharper-but-heavier).
    int maxW=220, maxH=160;
    if (const char* mr=std::getenv("BH_TERM_MAXRES")) {
        int a=0,b=0; if (std::sscanf(mr,"%dx%d",&a,&b)==2 && a>=16 && b>=16) { maxW=a; maxH=b; }
    }
    bool showSize = std::getenv("BH_TERM_SIZE") != nullptr;   // diagnostic: print detected size
    // Alternate screen buffer: a clean viewport-sized surface (like vim/htop) so the
    // render reliably fills the whole window and the shell scrollback is restored on
    // exit — avoids main-buffer \x1b[2J leaving the frame mispositioned in WT.
    std::printf("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");   // alt screen, hide cursor, autowrap off, clear

    while (running && !glfwWindowShouldClose(eng.window)) {
        auto tFrame0 = Clock::now();
        int cols, rows; getTermSize(cols, rows);
        int drawRows = std::max(4, rows);   // full height — no status line reserved
        int tw = std::max(8, cols), th = drawRows*2;
        // res menor: cap the ray-march samples in BOTH axes to (maxW x maxH) and
        // nearest-upscale to the cell grid. Independent factors are fine — aspect
        // comes from eng.WIDTH/HEIGHT (display = tw/th), separate from renderW/H,
        // so it never distorts. Below the cap, cw==tw & chh==th -> full quality
        // incl. vertical half-blocks; above it (tiny fonts), it degrades blockily
        // instead of stalling the GPU.
        int DSx = (tw + maxW - 1) / maxW, DSy = (th + maxH - 1) / maxH;
        int cw = (tw + DSx - 1) / DSx, chh = (th + DSy - 1) / DSy;
        bool fullRedraw = false;
        if (cols!=prevCols||rows!=prevRows) {
            eng.COMPUTE_WIDTH=eng.COMPUTE_MOVING_WIDTH=cw;
            eng.COMPUTE_HEIGHT=eng.COMPUTE_MOVING_HEIGHT=chh;
            eng.WIDTH=tw; eng.HEIGHT=th;   // display dims -> aspect=tw/th (undistorted)
            std::printf("\x1b[2J"); prevCols=cols; prevRows=rows;
            fullRedraw=true;
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
                    else if (vk=='B') eng.bloomEnabled=!eng.bloomEnabled;
                    else if (vk=='K') eng.kerrSpin=(eng.kerrSpin<0.01f)?kKerrSpinOn:0.0f;
                    else if (vk=='A') eng.diskAnimEnabled=!eng.diskAnimEnabled;
                    else if (vk==VK_OEM_PERIOD) eng.kerrSpin=glm::clamp(eng.kerrSpin+kKerrSpinStep,0.0f,1.0f);
                    else if (vk==VK_OEM_COMMA)  eng.kerrSpin=glm::clamp(eng.kerrSpin-kKerrSpinStep,0.0f,1.0f);
                    else if (vk=='T') eng.bhTilt += (recs[i].Event.KeyEvent.dwControlKeyState&SHIFT_PRESSED)?-kTiltStep:kTiltStep;
                    else if (vk=='F') cycleColorMode();
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

        g_animate = true; updateGravityPhysics();   // orbit moons/binary in the terminal too

        eng.dispatchCompute(camera, 0);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);

        size_t texBytes=size_t(cw)*chh*4;
        eng.initPBO(texBytes);
        const unsigned char* pboPtr=eng.pboBeginRead(GL_RGBA);
        px.resize(texBytes);
        if (pboPtr) memcpy(px.data(), pboPtr, texBytes);
        eng.pboEndRead();

        // upscale the cw x chh compute buffer (GL bottom-up) to the tw x th cell
        // pixel grid (top-down), nearest in both axes (GL vertical flip).
        dn.assign(size_t(tw)*th*3, 0);
        for (int oy=0;oy<th;++oy) {
            int sy=oy/DSy; if (sy>=chh) sy=chh-1;
            int srcY=chh-1-sy;
            const unsigned char* row=&px[size_t(srcY)*cw*4];
            unsigned char* drow=&dn[size_t(oy)*tw*3];
            for (int ox=0;ox<tw;++ox) {
                int sx=ox/DSx; if (sx>=cw) sx=cw-1;
                const unsigned char* p=&row[sx*4];
                unsigned char* o=&drow[ox*3];
                o[0]=p[0]; o[1]=p[1]; o[2]=p[2];
            }
        }

        // signature each cell from its top/bottom pixel pair (post-threshold)
        cur.assign(size_t(tw)*drawRows, TCell{});
        for (int r=0;r<drawRows;++r) {
            const unsigned char* trow=&dn[(size_t(2*r)*tw)*3];
            const unsigned char* brow=&dn[(size_t(2*r+1)*tw)*3];
            for (int x=0;x<tw;++x) {
                const unsigned char* t=&trow[x*3];
                const unsigned char* b=&brow[x*3];
                TCell& c=cur[size_t(r)*tw+x];
                // Opaque fill: every cell is a half-block. Dark pixels stay black
                // (0,0,0) so the scene covers the whole terminal — no transparent
                // bleed-through — while the constant black background still lets the
                // frame-diff skip it (the look the user wants: all space = BH scene).
                c.g=1;
                if ((t[0]+t[1]+t[2])>LIT){ c.fr=t[0]; c.fg=t[1]; c.fb=t[2]; }
                if ((b[0]+b[1]+b[2])>LIT){ c.br=b[0]; c.bg=b[1]; c.bb=b[2]; }
            }
        }
        if (prev.size()!=cur.size()) fullRedraw=true;

        // emit: full repaint on first frame / resize, else only changed cells.
        // SGR colour state persists across cursor moves, so curFg/curBg stay
        // valid between runs within a frame.
        frame.clear(); frame.reserve(size_t(tw)*drawRows*14+64);
        long curFg=-2, curBg=-2;
        auto fgRGB=[&](unsigned char R,unsigned char G,unsigned char B){
            long k=(long(R)<<16)|(G<<8)|B;
            if(k!=curFg){frame+="\x1b[38;2;";frame+=numStrs[R];frame+=";";frame+=numStrs[G];frame+=";";frame+=numStrs[B];frame+="m";curFg=k;}
        };
        auto bgRGB=[&](unsigned char R,unsigned char G,unsigned char B){
            long k=(long(R)<<16)|(G<<8)|B;
            if(k!=curBg){frame+="\x1b[48;2;";frame+=numStrs[R];frame+=";";frame+=numStrs[G];frame+=";";frame+=numStrs[B];frame+="m";curBg=k;}
        };
        auto bgDef=[&](){if(curBg!=-1){frame+="\x1b[49m";curBg=-1;}};
        auto emitCell=[&](const TCell& c){
            switch(c.g){
                case 1: fgRGB(c.fr,c.fg,c.fb); bgRGB(c.br,c.bg,c.bb); frame+="\xe2\x96\x80"; break;
                case 2: fgRGB(c.fr,c.fg,c.fb); bgDef();               frame+="\xe2\x96\x80"; break;
                case 3: fgRGB(c.fr,c.fg,c.fb); bgDef();               frame+="\xe2\x96\x84"; break;
                default: bgDef();                                     frame+=" "; break;
            }
        };
        if (fullRedraw || noDiff) {
            for (int r=0;r<drawRows;++r) {
                frame+="\x1b["; frame+=std::to_string(r+1); frame+=";1H";
                for (int x=0;x<tw;++x) emitCell(cur[size_t(r)*tw+x]);
            }
        } else {
            const int MERGE=3;  // bridge tiny unchanged gaps instead of re-homing the cursor
            for (int r=0;r<drawRows;++r) {
                const TCell* cr=&cur[size_t(r)*tw];
                const TCell* pr=&prev[size_t(r)*tw];
                int x=0;
                while (x<tw) {
                    if (cr[x]==pr[x]) { ++x; continue; }
                    int runStart=x, lastChg=x, j=x;
                    while (j<tw) {
                        if (!(cr[j]==pr[j])) lastChg=j;
                        else if (j-lastChg>MERGE) break;
                        ++j;
                    }
                    int runEnd=lastChg+1;
                    frame+="\x1b[";frame+=std::to_string(r+1);frame+=";";frame+=std::to_string(runStart+1);frame+="H";
                    for (int xx=runStart;xx<runEnd;++xx) emitCell(cr[xx]);
                    x=runEnd;
                }
            }
        }
        prev.swap(cur);
        if (wantStats) {
            ++statFrames; statBytes+=frame.size(); if (fullRedraw) statFullFrame=frame.size();
            if ((statFrames & 127)==0) {
                double avg=double(statBytes)/statFrames, full=double(statFullFrame);
                std::fprintf(stderr,
                    "[BH_TERM_STATS] frames=%ld avg/frame=%.0fB full-redraw=%.0fB "
                    "diff=%.1f%% of full (%.1f%% saved)\n",
                    statFrames, avg, full, full>0?100.0*avg/full:0.0, full>0?100.0*(1.0-avg/full):0.0);
                std::fflush(stderr);
            }
        }
        // diagnostic overlay: detected terminal size vs cell grid (top-left corner)
        if (showSize) frame += "\x1b[1;1H\x1b[38;2;0;0;0;48;2;255;255;0m term "
            + std::to_string(cols) + "x" + std::to_string(rows) + " cells "
            + std::to_string(tw) + "x" + std::to_string(drawRows) + " \x1b[0m";
        if (!frame.empty()) { std::fwrite(frame.data(), 1, frame.size(), stdout); std::fflush(stdout); }
        glfwPollEvents();
        // adaptive pacing: sleep only the leftover of the ~30fps budget so a heavy
        // frame runs at its own rate instead of frame_time + a fixed 33ms.
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-tFrame0).count();
        if (dt < 33) std::this_thread::sleep_for(std::chrono::milliseconds(33 - dt));
    }
    std::printf("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l"); std::fflush(stdout);   // restore autowrap+cursor, leave alt screen
    if (wantStats && statFrames>0) {
        double avg = double(statBytes)/statFrames;
        double full = double(statFullFrame);
        std::fprintf(stderr,
            "[BH_TERM_STATS] frames=%ld  total=%lluB  avg/frame=%.0fB  "
            "full-redraw=%.0fB  diff-vs-full=%.1f%% of bytes (%.1f%% saved)\n",
            statFrames, statBytes, avg, full,
            full>0?100.0*avg/full:0.0, full>0?100.0*(1.0-avg/full):0.0);
    }
    SetConsoleOutputCP(prevCP);
    SetConsoleMode(hOut, outMode); SetConsoleMode(hIn, inMode);
}
#endif // _WIN32
