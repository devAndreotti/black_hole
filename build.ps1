$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$win = Join-Path $root ".deps\winlibs\mingw64"
$sys = Join-Path $root ".deps\msys2-root\mingw64"
$build = Join-Path $root "build\winlibs"

$cmake = Join-Path $win "bin\cmake.exe"
$gcc = Join-Path $win "bin\gcc.exe"
$gxx = Join-Path $win "bin\g++.exe"
$blackHole3D = Join-Path $build "BlackHole3D.exe"

foreach ($requiredPath in @($cmake, $gcc, $gxx, (Join-Path $sys "lib\cmake"))) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Missing local build dependency: $requiredPath"
    }
}

$env:PATH = "$($win)\bin;$($sys)\bin;$env:PATH"

Get-Process BlackHole3D -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $blackHole3D } |
    Stop-Process -Force

& $cmake -S $root -B $build -G Ninja "-DCMAKE_CXX_COMPILER=$gxx" "-DCMAKE_C_COMPILER=$gcc" "-DCMAKE_PREFIX_PATH=$sys" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dllCopies = @(
    @{ Source = Join-Path $sys "bin\glew32.dll"; Target = Join-Path $build "glew32.dll" },
    @{ Source = Join-Path $sys "bin\glfw3.dll"; Target = Join-Path $build "glfw3.dll" },
    @{ Source = Join-Path $win "bin\libstdc++-6.dll"; Target = Join-Path $build "libstdc++-6.dll" },
    @{ Source = Join-Path $win "bin\libgcc_s_seh-1.dll"; Target = Join-Path $build "libgcc_s_seh-1.dll" },
    @{ Source = Join-Path $win "bin\libwinpthread-1.dll"; Target = Join-Path $build "libwinpthread-1.dll" }
)

foreach ($copy in $dllCopies) {
    if (Test-Path -LiteralPath $copy.Source) {
        Copy-Item -LiteralPath $copy.Source -Destination $copy.Target -Force
    }
}

foreach ($shader in @("geodesic.comp", "grid.vert", "grid.frag")) {
    Copy-Item -LiteralPath (Join-Path $root $shader) -Destination (Join-Path $build $shader) -Force
}

Write-Host "Built:"
Write-Host "  $build\BlackHole3D.exe"
Write-Host "  $build\BlackHole2D.exe"
