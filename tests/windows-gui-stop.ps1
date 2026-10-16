$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo "build\windows-gui-stop"
New-Item -ItemType Directory -Force -Path $build | Out-Null

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw "Visual Studio C++ build tools are required." }
    & (Join-Path $installation "Common7\Tools\Launch-VsDevShell.ps1") -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

Push-Location $build
try {
    $flags = @("/nologo", "/std:c++17", "/EHsc", "/W4", "/permissive-", "/utf-8",
               "/DUNICODE", "/D_UNICODE", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX")
    $libraries = @("comctl32.lib", "shell32.lib", "user32.lib", "gdi32.lib")
    & cl.exe @flags (Join-Path $repo "GUI\StockUniverseBuilderGui.cpp") /Fe:StockUniverseBuilder.exe /link /SUBSYSTEM:WINDOWS @libraries
    if ($LASTEXITCODE -ne 0) { throw "GUI build failed." }
    & cl.exe @flags (Join-Path $PSScriptRoot "windows-gui-stop.cpp") /Fe:windows-gui-stop.exe /link /SUBSYSTEM:CONSOLE @libraries
    if ($LASTEXITCODE -ne 0) { throw "Stop test build failed." }
    & .\windows-gui-stop.exe
    if ($LASTEXITCODE -ne 0) { throw "Stop tests failed." }
} finally {
    Pop-Location
}
