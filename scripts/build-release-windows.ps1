[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"
$repositoryDir = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repositoryDir "build\windows-x64"
$distributionDir = Join-Path $repositoryDir "dist"
$releaseName = "Stock-Universe-Builder-windows-x64"
$releaseDir = Join-Path $distributionDir $releaseName
$archive = Join-Path $distributionDir "$releaseName.zip"

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "VCPKG_ROOT is not set. Point it to a vcpkg checkout and run this script again."
}

$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) {
    throw "Could not find the vcpkg CMake toolchain at $toolchain."
}

cmake -S $repositoryDir -B $buildDir `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit status $LASTEXITCODE." }

cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "Windows build failed with exit status $LASTEXITCODE." }

$executable = Join-Path $buildDir "Release\StockUniverseBuilder.exe"
if (-not (Test-Path $executable)) {
    $executable = Join-Path $buildDir "StockUniverseBuilder.exe"
}
if (-not (Test-Path $executable)) {
    throw "The build completed without producing StockUniverseBuilder.exe."
}

New-Item -ItemType Directory -Force -Path $distributionDir | Out-Null
if (Test-Path $releaseDir) { Remove-Item -Recurse -Force $releaseDir }
if (Test-Path $archive) { Remove-Item -Force $archive }
New-Item -ItemType Directory -Force -Path `
    $releaseDir, `
    (Join-Path $releaseDir "Data"), `
    (Join-Path $releaseDir "manifests"), `
    (Join-Path $releaseDir "licenses") | Out-Null

Copy-Item $executable (Join-Path $releaseDir "StockUniverseBuilder.exe")
Copy-Item (Join-Path $repositoryDir "run.bat") $releaseDir
Copy-Item (Join-Path $repositoryDir "ticket.example.txt") $releaseDir
Copy-Item (Join-Path $repositoryDir "APIKeys.example.txt") $releaseDir
Copy-Item (Join-Path $repositoryDir "START_HERE.md") (Join-Path $releaseDir "README.txt")
Copy-Item (Join-Path $repositoryDir "THIRD_PARTY_NOTICES.txt") $releaseDir
Copy-Item (Join-Path $repositoryDir "Data\.gitkeep") (Join-Path $releaseDir "Data\.gitkeep")
Copy-Item (Join-Path $repositoryDir "manifests\.gitkeep") `
    (Join-Path $releaseDir "manifests\.gitkeep")

$installedShare = Join-Path $buildDir "vcpkg_installed\x64-windows-static\share"
if (Test-Path $installedShare) {
    Get-ChildItem -Path $installedShare -Filter copyright -Recurse | ForEach-Object {
        $packageName = Split-Path -Leaf (Split-Path -Parent $_.FullName)
        Copy-Item $_.FullName (Join-Path $releaseDir "licenses\$packageName.txt")
    }
}

$testDir = Join-Path $env:TEMP ("Stock Builder Packaging Test " + [guid]::NewGuid())
try {
    Copy-Item -Recurse $releaseDir $testDir
    $env:STOCK_BUILDER_NO_PAUSE = "1"
    $launcherOutput = & cmd.exe /d /c (Join-Path $testDir "run.bat") 2>&1
    $launcherStatus = $LASTEXITCODE
    if ($launcherStatus -eq 0 -or ($launcherOutput -join "`n") -notmatch "Copy ticket.example.txt") {
        throw "The Windows missing-ticket launcher check failed."
    }

    Copy-Item (Join-Path $testDir "ticket.example.txt") (Join-Path $testDir "ticket.txt")
    Copy-Item (Join-Path $testDir "APIKeys.example.txt") (Join-Path $testDir "APIKeys.txt")
    $credentialOutput = & cmd.exe /d /c (Join-Path $testDir "run.bat") 2>&1
    $credentialStatus = $LASTEXITCODE
    if ($credentialStatus -eq 0 -or ($credentialOutput -join "`n") -notmatch "example placeholders") {
        throw "The Windows missing-credential check failed."
    }
}
finally {
    Remove-Item Env:STOCK_BUILDER_NO_PAUSE -ErrorAction SilentlyContinue
    if (Test-Path $testDir) { Remove-Item -Recurse -Force $testDir }
}

Compress-Archive -Path $releaseDir -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Windows release ready: $archive"
