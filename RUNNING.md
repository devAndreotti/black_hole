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
| `--anim` | Liga a animação Kepleriana do disco (material girando) |
| `--legacy` / `-l` | **Shader Schwarzschild original** (geodésica antiga, sem Kerr) — pra comparar |
| `--no-grid` | Esconde a grade de curvatura do espaço-tempo |
| `--no-disk` | Esconde o disco de acreção |
| `--no-beam` | Desliga o ray-march dos sóis (usa interseção reta — mais rápido) |
| `--red` | Disco em tons de vermelho |
| `--white` | Disco em tons de branco |
| `--test` | Roda os testes unitários e sai |

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
| **`A`** | Liga/desliga **animação Kepleriana** do disco |
| `G` (ou botão direito) | Liga/desliga gravidade (N-body dos sóis) |
| `M` | Mostra/esconde a grade |
| `B` | Liga/desliga bloom |
| `S` | Salva screenshot (`bh_XXXX.bmp`) |
| `Q` / `Esc` | Sair |

### Modo terminal
Setas (orbitar), `+`/`-` (zoom), `[`/`]` (passos), `M` (grade), `G` (gravidade),
mouse passando = parallax, `Q`/`Esc` = sair. *(Kerr/anim no terminal: passe `--spin`/`--anim` na linha de comando.)*

### Modo wallpaper
Mouse = parallax. **Ctrl+Alt+Q** = sair (atalho global).

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
