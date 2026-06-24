#Requires -Version 5.1
# setup.ps1 — one-time toolchain bootstrap
# Downloads the portable compiler (WinLibs MinGW-w64) and OpenGL libraries
# (GLEW, GLFW3, GLM) into .deps\ so build.ps1 works without any system installs.
# Safe to re-run: skips anything already cached or extracted.
$ErrorActionPreference = "Stop"

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$deps    = Join-Path $root   ".deps"
$dlDir   = Join-Path $deps   "downloads"
$pkgDir  = Join-Path $deps   "msys2-packages"
$winDir  = Join-Path $deps   "winlibs"
$msysDir = Join-Path $deps   "msys2-root"
$cmake   = Join-Path $winDir "mingw64\bin\cmake.exe"

if (Test-Path $cmake) {
    Write-Host "[setup] Toolchain already installed. Run .\build.ps1 to compile." -ForegroundColor Green
    exit 0
}

Write-Host "Black Hole — portable toolchain setup (one-time, ~300 MB download)" -ForegroundColor Cyan
Write-Host "Everything goes into .deps\  —  nothing is installed system-wide.`n"

foreach ($d in @($dlDir, $pkgDir, $winDir, $msysDir)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

# ── Download helper (uses curl.exe — built into Windows 10/11) ────────────────
function Get-File($url, $dest, $label) {
    if (Test-Path $dest) {
        Write-Host "  [cached] $label"
        return
    }
    Write-Host "  [dl]     $label"
    curl.exe --progress-bar --location --output "$dest" "$url"
    if ($LASTEXITCODE -ne 0) { throw "Download failed ($LASTEXITCODE): $url" }
}

# ─── Step 1: WinLibs MinGW-w64 (compiler + cmake + ninja) ───────────────────
Write-Host "[1/3] WinLibs MinGW-w64  (compiler, cmake, ninja)"

$wlRelease = "16.1.0posix-14.0.0-msvcrt-r1"
$wlFile    = "winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64msvcrt-14.0.0-r1.zip"
$wlUrl     = "https://github.com/brechtsanders/winlibs_mingw/releases/download/$wlRelease/$wlFile"
$wlZip     = Join-Path $dlDir $wlFile

Get-File $wlUrl $wlZip $wlFile

Write-Host "  [x]      Extracting WinLibs (~935 MB unzipped, may take a minute)..."
Expand-Archive -LiteralPath $wlZip -DestinationPath $winDir -Force

if (-not (Test-Path $cmake)) {
    throw "WinLibs extraction failed: $cmake not found after extract."
}
$cmakeVer = (& $cmake --version | Select-Object -First 1)
Write-Host "  [ok]     $cmakeVer" -ForegroundColor Green

# ─── Step 2: MSYS2 packages (GLEW, GLFW3, GLM, pthreads runtime) ─────────────
Write-Host "`n[2/3] MSYS2 packages  (GLEW, GLFW3, GLM, pthreads)"

# Uses the MSYS2 archive mirror — versions are pinned and stored permanently there.
$mirror = "https://archive.msys2.org/mingw/mingw64"
$pkgs   = @(
    "mingw-w64-x86_64-gcc-libs-16.1.0-2-any.pkg.tar.zst",
    "mingw-w64-x86_64-glew-2.2.0-3-any.pkg.tar.zst",
    "mingw-w64-x86_64-glfw-3.4-1-any.pkg.tar.zst",
    "mingw-w64-x86_64-glm-1.0.3-1-any.pkg.tar.zst",
    "mingw-w64-x86_64-libwinpthread-git-12.0.0.r747.g1a99f8514-1-any.pkg.tar.zst",
    "mingw-w64-x86_64-winpthreads-git-12.0.0.r747.g1a99f8514-1-any.pkg.tar.zst"
)

foreach ($pkg in $pkgs) {
    Get-File "$mirror/$pkg" (Join-Path $pkgDir $pkg) $pkg
}

# ─── Step 3: Extract packages into msys2-root ─────────────────────────────────
Write-Host "`n[3/3] Extracting packages into .deps\msys2-root"

Push-Location $msysDir
try {
    foreach ($pkg in $pkgs) {
        $src = Join-Path $pkgDir $pkg
        Write-Host "  [x]      $pkg"
        # cmake -E tar handles .tar.zst natively since CMake 3.22
        & $cmake -E tar xf "$src"
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $pkg" }
    }
} finally {
    Pop-Location
}

# ─── Verify ───────────────────────────────────────────────────────────────────
Write-Host "`nVerifying..."
$checks = @(
    @{ Path = Join-Path $winDir  "mingw64\bin\cmake.exe";          Label = "cmake" },
    @{ Path = Join-Path $winDir  "mingw64\bin\ninja.exe";          Label = "ninja" },
    @{ Path = Join-Path $winDir  "mingw64\bin\g++.exe";            Label = "g++" },
    @{ Path = Join-Path $msysDir "mingw64\lib\cmake\glew";         Label = "GLEW cmake" },
    @{ Path = Join-Path $msysDir "mingw64\lib\cmake\glfw3";        Label = "GLFW3 cmake" },
    @{ Path = Join-Path $msysDir "mingw64\share\glm\glmConfig.cmake"; Label = "GLM cmake" },
    @{ Path = Join-Path $msysDir "mingw64\include\glm\glm.hpp";   Label = "GLM headers" }
)

$ok = $true
foreach ($c in $checks) {
    if (Test-Path $c.Path) {
        Write-Host "  [ok] $($c.Label)" -ForegroundColor Green
    } else {
        Write-Host "  [!!] MISSING: $($c.Label)  ($($c.Path))" -ForegroundColor Red
        $ok = $false
    }
}

if ($ok) {
    Write-Host "`nSetup complete. Run .\build.ps1 to compile." -ForegroundColor Green
} else {
    Write-Host "`nSome items are missing. Delete .deps\ and re-run .\setup.ps1." -ForegroundColor Yellow
    exit 1
}
