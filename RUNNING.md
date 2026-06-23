# Black Hole — Guia de Execução

Simulação de buraco negro em **compute shader** (OpenGL 4.3, ray-march de geodésicas).
Toolchain portátil em `.deps` (mingw + cmake + ninja) — não precisa de vcpkg nem instalar nada.

---

## 1. Build

```powershell
.\build.ps1
```

Gera:
- `build\winlibs\BlackHole3D.exe` — simulação principal (GPU)
- `build\winlibs\BlackHole2D.exe` — lente 2D (demo)

> O build **falha** se houver uma instância do `BlackHole3D` aberta (link não consegue
> sobrescrever o `.exe`). O `build.ps1` mata o processo antes de compilar.

---

## 2. Como rodar

Use o wrapper `.\run.ps1 <modo> [flags]` (recomendado) **ou** chame o exe direto em
`build\winlibs\BlackHole3D.exe [flags]`.

| Modo | Comando (`run.ps1`) | Exe direto | O que é |
|---|---|---|---|
| **Janela** (padrão) | `.\run.ps1` | `BlackHole3D.exe` | Janela GL interativa |
| **Terminal** | `.\run.ps1 --terminal` | `BlackHole3D.exe --terminal` | Renderiza dentro do terminal (meios-blocos ANSI truecolor) |
| **Wallpaper** | `.\run.ps1 --wallpaper` | `BlackHole3D.exe --dcomp` | Papel de parede atrás dos ícones (DirectComposition, Win11 24H2) |
| **2D** | `.\run.ps1 --2d` | `BlackHole2D.exe` | Lente gravitacional 2D |
| **Parar** | `.\run.ps1 stop` | — | Mata processos `BlackHole3D/2D` |

> `run.ps1` mapeia `--wallpaper`/`-w` → `--dcomp` (o caminho recomendado no Win11 24H2) e
> repassa qualquer flag extra pro exe. Wallpaper roda escondido; saia com **Ctrl+Alt+Q**.

---

## 3. Flags (CLI)

| Flag | Efeito |
|---|---|
| `--terminal` / `-t` | Modo terminal |
| `--wallpaper` / `-w`, `--dcomp` / `-d` | Modo wallpaper (DirectComposition) |
| `--spin N` | **Kerr**: spin adimensional `a*` ∈ [0, 1] (1 = extremo). `a = a*·r_s/2` |
| `--tilt G` | Inclina o eixo do buraco negro em **G graus** (disco + eixo de spin + jatos juntos; o Kerr segue o ângulo). Ex.: `--tilt 45` |
| `--cinematic` | **Modo cinemático**: câmera voa num caminho em loop (longe/face-on → mergulha edge-on no anel de fótons → recua). Ótimo p/ wallpaper/screenshot. Na janela: tecla `C` liga/desliga |
| `--anim` | Liga a animação Kepleriana do disco (material girando) |
| `--legacy` / `-l` | **Shader Schwarzschild original** (geodésica antiga, sem Kerr) — pra comparar |
| `--no-grid` | Esconde a grade de curvatura do espaço-tempo |
| `--no-disk` | Esconde o disco de acreção |
| `--no-beam` | Desliga o ray-march dos sóis (usa interseção reta — mais rápido) |
| `--red` | Disco em tons de vermelho |
| `--white` | Disco em tons de branco (white-shift nos meteoros) |
| `--blue` | Disco azul (estética estrela de nêutrons / binária X) |
| `--green` | Disco verde (exótico / pulsar alienígena) |
| `--test` | Roda os testes unitários e sai |
| `--render` | Renderiza 1 frame headless em alta-res e salva BMP, depois sai (validação visual). Sub-flags: `--size 900x600`, `--elev <rad>`, `--azim <rad>`, `--zoom <metros>`, `--time <s>` (fixa `cam.time` p/ validar flare/luas/animação), `--out arquivo.bmp` |

Exemplos:
```powershell
.\run.ps1 --spin 0.9            # janela, Kerr near-extremal
.\run.ps1 --terminal --anim     # terminal, disco animado
.\run.ps1 --legacy              # versão antiga (Schwarzschild aproximado)
.\run.ps1 --terminal --legacy   # comparar a versão antiga no terminal
```

---

## 4. Controles

### Modo janela
| Tecla / Mouse | Ação |
|---|---|
| Arrastar (botão esq./meio) | Orbitar a câmera |
| Scroll do mouse | Zoom |
| Setas ← → ↑ ↓ | Orbitar |
| `+` / `-` (ou `=` / `-`) | Zoom in / out |
| `[` / `]` | Menos / mais passos de integração (qualidade vs. perf) |
| **`K`** | Liga/desliga **spin de Kerr** (0 ↔ 0.9) |
| **`.`** / **`,`** | Ajuste fino do spin de Kerr (+/− 0.1, faixa 0–1) |
| **`A`** | Liga/desliga **animação** da cena: disco Kepleriano girando + **luas orbitando** os sóis |
| **`T`** / **Shift+`T`** | Inclina o eixo do buraco negro +5° / −5° (ao vivo; spin segue) |
| **`R`** | Liga/desliga a **auto-rotação** da câmera (ligada por padrão; desligar deixa o TAA acumular = imagem mais nítida parada) |
| **`C`** | Liga/desliga o **modo cinemático** (câmera voa num caminho: longe → mergulho edge-on → recua) |
| **`F`** | Cicla a **paleta de cores** do disco/jatos/meteoros: padrão→vermelho→branco→azul→verde→padrão |
| `G` (ou botão direito) | Liga/desliga gravidade (N-body dos sóis) |
| `M` | Mostra/esconde a grade |
| `B` | Liga/desliga bloom |
| `S` | Salva screenshot (`bh_XXXX.bmp`) |
| `Q` / `Esc` | Sair |

### Modo terminal
Setas (orbitar), `+`/`-` (zoom), `[`/`]` (passos), `M` (grade), `G` (gravidade),
**`K`** (spin Kerr 0↔0.9), **`.`/`,`** (spin fino ±0.1), **`A`** (animação), **`B`** (bloom),
**`T`/Shift+`T`** (inclinar ±5°), **`F`** (ciclar paleta de cor), mouse passando = parallax, `Q`/`Esc` = sair.

O render ocupa **a tela inteira do terminal, opaco** (sem barra de status e sem
fundo transparente) — todo o espaço é o buraco negro. Os atalhos acima continuam
valendo mesmo sem o lembrete na tela.

**Desempenho** — o terminal reescreve só as células que mudaram (frame-diff via
reposicionamento de cursor) em vez de repintar a tela inteira a cada frame (~18×
menos bytes pro emulador, o gargalo real do "travado"), e **limita a resolução do
ray-march** a um teto (`maxW×maxH`, padrão 220×160) fazendo upscale pras células —
abaixo do teto é qualidade cheia (inclui meios-blocos verticais), acima degrada em
blocos sem travar a GPU. O pacing é adaptativo (não soma sleep fixo em frame lento).

> **`Ctrl +`/`Ctrl -` é o zoom da FONTE do Windows Terminal**, não um ajuste de
> qualidade: fonte menor = mais células = mais pixels pra GPU ray-marchar por frame.
> O teto acima evita que isso trave; pra trocar nitidez×fluidez use `BH_TERM_MAXRES`.

Variáveis de ambiente (diagnóstico/escape):
| Var | Efeito |
|---|---|
| `BH_TERM_MAXRES=WxH` | Teto de resolução do ray-march (ex.: `320x240` mais nítido/pesado, `120x90` mais leve/blocado). Padrão `220x160` |
| `BH_TERM_NODIFF=1` | Repinta a tela cheia todo frame (fallback se algum terminal não renderizar bem os updates incrementais) |
| `BH_TERM_STATS=1`  | Imprime no stderr a contagem de bytes/frame (diff vs full-redraw), pra medir o ganho |

### Modo wallpaper
Mouse = parallax. **Ctrl+Alt+Q** = sair (atalho global). As teclas valem quando o
desktop está em foco (ou segurando Ctrl+Alt): setas (orbitar), `+`/`-` (zoom),
`[`/`]` (passos), `G`/`M`, **`K`** (spin), **`.`/`,`** (spin fino), **`A`** (animação),
**`B`** (bloom), **`T`/Shift+`T`** (inclinar ±5°), **`F`** (ciclar paleta).

Qualidade: renderiza a **metade da resolução** da tela (antes era 1/5) e o DComp faz
upscale. Ajuste com `BH_WP_DIV` (divisor): `1` = nativo (mais nítido, mais pesado),
`3`–`4` = mais leve se a rotação ficar pesada. Padrão `2`.

---

## 5. Kerr × Legacy — o que mudou

O shader (`geodesic.comp`) tem **dois caminhos**, escolhidos pelo valor do spin:

- **Sem spin (`a*=0`, padrão)** → integrador **Schwarzschild do legacy** (Christoffel).
  Aparência **idêntica** ao `--legacy`: sombra lisa, anel de fótons sem chuvisco.
- **Com spin (`a*>0`, via `K` ou `--spin`)** → integrador de **Kerr** (separação de Carter,
  eixo de spin = Y), em unidades não-dimensionais (r_s = 1) pra não estourar float32.
  Mostra a assimetria de frame-dragging (sombra em "D", anel de fótons deslocado).

Ou seja: **o padrão é o visual limpo do legacy**, e o Kerr é uma feature opt-in.

```powershell
.\run.ps1                 # padrão = visual do legacy (liso); aperte K pra ligar o spin
.\run.ps1 --spin 0.9      # Kerr near-extremal direto
.\run.ps1 --legacy        # força o shader original standalone (geodesic_legacy.comp)
```

> `--legacy` carrega o shader original isolado (`geodesic_legacy.comp`). Como o padrão sem
> spin já usa a mesma geodésica, os dois ficam iguais sem spin — a diferença aparece ao ligar o Kerr.

O backup da versão pré-modularização fica em `backup_original/` (commit `42ad16c`).
