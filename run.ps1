param(
    [string] $Mode = $null,
    
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# Combine Mode and ExtraArgs into a single array of arguments
$allArgs = @()
if ($Mode) {
    $allArgs += $Mode
}
if ($ExtraArgs) {
    $allArgs += $ExtraArgs
}

$isTerminal = $allArgs -contains "--terminal" -or $allArgs -contains "--t" -or $allArgs -contains "-t"
$is2D = $allArgs -contains "--2d" -or $allArgs -contains "2d"
$isStop = $allArgs -contains "stop"
$isWallpaper = $allArgs -contains "--wallpaper" -or $allArgs -contains "--w" -or $allArgs -contains "-w" -or $allArgs -contains "--dcomp" -or $allArgs -contains "--d" -or $allArgs -contains "-d"

if ($isStop) {
    Write-Host "Stopping any running Black Hole simulation processes..."
    Stop-Process -Name "BlackHole3D" -ErrorAction SilentlyContinue
    Stop-Process -Name "BlackHole2D" -ErrorAction SilentlyContinue
    Write-Host "Processes stopped."
    return
}

# Define executable and working directory
if ($is2D) {
    $exe = Join-Path $root "build\winlibs\BlackHole2D.exe"
    $workingDirectory = Join-Path $root "build\winlibs"
    $passArgs = $allArgs | Where-Object { $_ -ne "--2d" -and $_ -ne "2d" }
    $dlls = @("glew32.dll", "glfw3.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll")
} else {
    $exe = Join-Path $root "build\winlibs\BlackHole3D.exe"
    $workingDirectory = Join-Path $root "build\winlibs"
    $dlls = @("glew32.dll", "glfw3.dll", "libstdc++-6.dll", "libgcc_s_seh-1.dll")
    
    # Process arguments for the 3D executable:
    # 1) If it is terminal mode, we will run it directly with --terminal
    # 2) If it is wallpaper mode:
    #    - If it contains --w, -w, --wallpaper, map it to --dcomp for the recommended Win11 DirectComposition path
    #    - Map --d or -d to --dcomp
    $passArgs = @()
    foreach ($arg in $allArgs) {
        if ($arg -eq "--terminal" -or $arg -eq "--t" -or $arg -eq "-t") {
            # Handled separately in the terminal run block
        }
        elseif ($arg -eq "--wallpaper" -or $arg -eq "--w" -or $arg -eq "-w" -or $arg -eq "--dcomp" -or $arg -eq "--d" -or $arg -eq "-d") {
            $passArgs += "--dcomp"
        }
        else {
            $passArgs += $arg
        }
    }
}

if (-not (Test-Path -LiteralPath $exe)) {
    throw "$exe was not found. Run .\build.ps1 first."
}

$missingDlls = $dlls | Where-Object { -not (Test-Path -LiteralPath (Join-Path $workingDirectory $_)) }
if ($missingDlls) {
    throw "Missing runtime DLL(s) in ${workingDirectory}: $($missingDlls -join ', ')"
}

if ($isTerminal) {
    Push-Location $workingDirectory
    try {
        # Filter out any terminal or wallpaper flags from passArgs, and run locally with --terminal
        $terminalPassArgs = @()
        foreach ($arg in $allArgs) {
            if ($arg -ne "--terminal" -and $arg -ne "--t" -and $arg -ne "-t" -and $arg -ne "--w" -and $arg -ne "-w" -and $arg -ne "--wallpaper") {
                $terminalPassArgs += $arg
            }
        }
        & $exe --terminal $terminalPassArgs
    } finally {
        Pop-Location
    }
    return
}

# Start standard window or wallpaper process
$startParams = @{
    FilePath = $exe
    WorkingDirectory = $workingDirectory
}
if ($passArgs) { $startParams.ArgumentList = $passArgs }

if ($isWallpaper) {
    $startParams.WindowStyle = "Hidden"
} else {
    $startParams.Wait = $true
}

Start-Process @startParams

if ($isWallpaper) {
    Write-Host "Black hole wallpaper running (DirectComposition/Wallpaper)."
    Write-Host "Move the mouse for parallax. Quit from anywhere with Ctrl+Alt+Q."
}
