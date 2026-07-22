# uWeddingPlayer rebuild helper
#
# Usage:
#   .\rebuild.ps1               # Release build
#   .\rebuild.ps1 -Run          # build then launch
#   .\rebuild.ps1 -Config Debug
#   .\rebuild.ps1 -Clean        # remove build dir and reconfigure
#
# Steps:
#   1) kill any running uWeddingPlayer.exe (avoid LNK1104 lock)
#   2) cmake --build build --config <Config>
#   3) if -Run, start the exe on success

param(
    [switch]$Run,
    [switch]$Clean,
    [string]$Config = "Release"
)

$exeName = "uWeddingPlayer"
$exePath = Join-Path "build" (Join-Path $Config "$exeName.exe")

function Write-Step($tag, $msg, $color) {
    Write-Host ("[{0}] {1}" -f $tag.PadRight(5), $msg) -ForegroundColor $color
}

# 1. Kill running instance if present
try {
    $procs = Get-Process $exeName -ErrorAction Stop
    Write-Step "stop"  ("killing {0} process(es)" -f $procs.Count) Yellow
    Stop-Process -Name $exeName -Force
    Start-Sleep -Milliseconds 300
} catch {
    # not running: normal
}

# Optional: full clean rebuild
if ($Clean) {
    if (Test-Path "build") {
        Write-Step "clean" "removing build dir" Yellow
        Remove-Item "build" -Recurse -Force
    }
    Write-Step "cmake" "configure" Cyan
    cmake -B build -DUWP_AUTO_WINDEPLOYQT=ON
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# 2. Build
Write-Step "build" ("cmake --build build --config {0}" -f $Config) Cyan
cmake --build build --config $Config
$rc = $LASTEXITCODE
if ($rc -ne 0) {
    Write-Step "fail" ("exit {0}" -f $rc) Red
    exit $rc
}
Write-Step "ok"   "build succeeded" Green

# 3. Optional: launch
if ($Run) {
    if (Test-Path $exePath) {
        Write-Step "run"  $exePath Cyan
        Start-Process $exePath
    } else {
        Write-Step "fail" ("exe not found: {0}" -f $exePath) Red
        exit 1
    }
}
