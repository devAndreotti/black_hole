# black_hole — fork GPU (Kerr · wallpaper · terminal)

Simulação de buraco negro em tempo real por **ray-march de geodésicas** num **compute
shader** (OpenGL 4.3). Fork de [`kavan010/black_hole`](https://github.com/kavan010/black_hole)
com física estendida (Kerr), pós-processamento cinematográfico e três formas novas de
rodar — inclusive como **papel de parede vivo** do Windows e **dentro do terminal**.

> **Este é um fork.**
> Upstream original: **[kavan010/black_hole](https://github.com/kavan010/black_hole)** —
> crédito ao autor pela base (ray-tracing, disco de acreção, grade de curvatura).
> Fork: **[devAndreotti/black_hole](https://github.com/devAndreotti/black_hole)**.

---

## O que este fork adiciona (diferenças vs. upstream)

### Física
- **Buraco negro de Kerr (rotação)** — integrador Kerr-Schild Cartesiano *sem costura no
  eixo*, com frame-dragging (sombra em "D", anel de fótons deslocado). Liga com `K` ou
  `--spin a*`. Sem spin, recai no integrador Schwarzschild idêntico ao do upstream.
- **Inclinação do eixo** (`--tilt G°` / tecla `T`) — disco + eixo de spin + jatos giram
  juntos para qualquer ângulo; o efeito Kerr acompanha.
- **ISCO variável** — a borda interna do disco recua conforme o spin sobe.
- **Doppler beaming relativístico + redshift gravitacional** no disco.

### Aparência / cena viva
- **Jatos polares relativísticos** (Blandford-Znajek) — surgem com o spin; cor segue a paleta.
- **Paleta de cores** ciclável (`F`) ou por flag: `--red` / `--white` / `--blue` /
  `--green` — disco, jatos e meteoros seguem a mesma paleta.
- **Sóis 3D com vida**: coronas, limb-darkening, granulação e manchas; um sol variável com
  flares; **luas orbitando** e um **companheiro binário** (animam com `A`).
- **Meteoros**: trânsito (flyby que cresce vindo de longe) ou **captura com
  espaguetificação** + redshift, afinando até sumir no horizonte.
- **Céu profundo**: campo de estrelas, poeira, galáxia espiral lenteada (anel de Einstein)
  e sóis distantes.

### Pós-processamento / câmera
- **Tone mapping ACES** (filmic) no frame final + **TAA** (anti-aliasing temporal) + **bloom**.
- **Modo cinemático** (`--cinematic` / tecla `C`) — câmera voa num caminho em loop
  (longe/face-on → mergulho edge-on no anel de fótons → recua).

### Plataforma / formas de rodar (Windows)
- **Modo wallpaper** — renderiza *atrás dos ícones* do desktop via DirectComposition
  (Win11 24H2). Roda em segundo plano com parallax pelo mouse.
- **Modo terminal** — renderiza *dentro do terminal* com meios-blocos ANSI truecolor
  (mesma pipeline GPU), com frame-diff e teto de resolução para não travar.
- **Toolchain portátil** — build via `.deps` (mingw + cmake + ninja); **não precisa de
  vcpkg** nem de instalar nada (o upstream exigia vcpkg).
- **Render headless** (`--render`) — salva BMP em alta resolução para validação visual.

---

## Build

```powershell
.\build.ps1
```

Gera `build\winlibs\BlackHole3D.exe` (GPU) e `BlackHole2D.exe` (lente 2D). A toolchain
portátil fica em `.deps` — nada a instalar. (Para o caminho clássico com vcpkg/apt, veja o
[README do upstream](https://github.com/kavan010/black_hole).)

## Rodar

```powershell
.\run.ps1                 # janela interativa (padrão)
.\run.ps1 --terminal      # dentro do terminal (ANSI truecolor)
.\run.ps1 --wallpaper     # papel de parede vivo (sair: Ctrl+Alt+Q)
.\run.ps1 --2d            # lente gravitacional 2D
.\run.ps1 stop            # mata os processos
```

### Modo wallpaper
Renderiza atrás dos ícones (DirectComposition). Mouse = parallax; **Ctrl+Alt+Q** sai de
qualquer lugar. Ajuste a qualidade com `BH_WP_DIV` (`1` = nativo, `2` = padrão, `3`–`4` =
mais leve).

### Modo terminal
A mesma pipeline GPU desenhada em células de texto (`▀`, truecolor): ocupa a tela inteira,
opaco. Reescreve só as células que mudam (frame-diff) e limita a resolução do ray-march
(`BH_TERM_MAXRES`, padrão `220x160`) para não travar quando a fonte é pequena.

## Flags e controles

Referência completa (todas as flags, teclas por modo e variáveis de ambiente) em
**[RUNNING.md](RUNNING.md)**. Resumo:

| Flag | Efeito |
|---|---|
| `--spin a*` | Spin de Kerr 0–1 (1 = extremo) |
| `--tilt G` | Inclina o eixo do BH em G graus |
| `--anim` | Disco Kepleriano girando + luas orbitando |
| `--cinematic` | Câmera voa num caminho em loop |
| `--red` / `--white` / `--blue` / `--green` | Paleta do disco/jatos/meteoros |
| `--render` | 1 frame headless → BMP (`--size`, `--time`, `--out`, …) |
| `--legacy` | Shader Schwarzschild original (para comparar) |

Teclas principais (valem em janela/terminal/wallpaper): `K` spin · `.`/`,` spin fino ·
`T`/Shift+`T` inclinar · `A` animação · `F` paleta · `C` cinemático · `B` bloom · `M`
grade · `+`/`-` zoom · setas orbitam.

---

## Como o código funciona

- **2D** (`BlackHole2D`): lente gravitacional direta em `2D_lensing.cpp`.
- **3D** (`BlackHole3D`): `black_hole.cpp` monta a cena e os UBOs (câmera, disco, objetos)
  e despacha o compute shader `geodesic.comp`, que integra as geodésicas na GPU. Os módulos
  `bh_engine` (pipeline GL/bloom/TAA), `bh_terminal` e `bh_wallpaper` cuidam de cada modo de
  saída. Veja [RUNNING.md](RUNNING.md) para o detalhe de Kerr × legacy e da arquitetura.

## Créditos

- **Base original:** [kavan010/black_hole](https://github.com/kavan010/black_hole) —
  ray-tracing, disco de acreção e grade de espaço-tempo.
- **Fork e extensões** (Kerr inclinável, jatos, wallpaper, terminal, cinemático, ACES,
  vida na cena): este repositório.

A licença segue a do projeto upstream.
