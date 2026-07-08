$ErrorActionPreference = "Stop"

$Version = if ($env:BJ_LED_VERSION) { $env:BJ_LED_VERSION } else { "0.1.17" }
$Repository = if ($env:BJ_LED_REPOSITORY) { $env:BJ_LED_REPOSITORY } else { "alman790/bj-led-screen-sync" }
$BaseUrl = if ($env:BJ_LED_RELEASE_BASE_URL) { $env:BJ_LED_RELEASE_BASE_URL } else { "https://github.com/$Repository/releases/download/v$Version/" }
$TempDir = Join-Path $env:TEMP ("bj-led-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

try {
    $SetupName = "BJLEDAmbilight-$Version-windows-x64-setup.exe"
    $SetupUrl = "$BaseUrl$SetupName"
    $SetupPath = Join-Path $TempDir $SetupName

    try {
        Write-Host "Downloading $SetupUrl"
        Invoke-WebRequest -Uri $SetupUrl -OutFile $SetupPath
        Start-Process -FilePath $SetupPath -Wait
        exit 0
    } catch {
        Write-Host "Setup installer unavailable, trying portable zip..."
    }

    $ZipName = "BJLEDAmbilight-$Version-windows-x64.zip"
    $ZipUrl = "$BaseUrl$ZipName"
    $ZipPath = Join-Path $TempDir $ZipName
    $InstallDir = Join-Path $env:LOCALAPPDATA "BJ LED Ambilight"

    Write-Host "Downloading $ZipUrl"
    Invoke-WebRequest -Uri $ZipUrl -OutFile $ZipPath
    Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Expand-Archive -Path $ZipPath -DestinationPath $InstallDir -Force
    Write-Host "Installed: $InstallDir"
} finally {
    Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue
}
