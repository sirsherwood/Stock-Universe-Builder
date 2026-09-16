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

function Find-BuiltExecutable([string]$Name) {
    $candidate = Join-Path $buildDir "Release\$Name"
    if (Test-Path $candidate) { return $candidate }
    $candidate = Join-Path $buildDir $Name
    if (Test-Path $candidate) { return $candidate }
    throw "The build completed without producing $Name."
}

function Get-PeSubsystem([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "$Path is not a valid PE executable."
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    $optionalHeader = $peOffset + 24
    return [BitConverter]::ToUInt16($bytes, $optionalHeader + 68)
}

$guiExecutable = Find-BuiltExecutable "StockUniverseBuilder.exe"
$coreExecutable = Find-BuiltExecutable "UniverseBuilderCore.exe"
if ((Get-PeSubsystem $guiExecutable) -ne 2) {
    throw "StockUniverseBuilder.exe was not linked as a Windows GUI application."
}
if ((Get-PeSubsystem $coreExecutable) -ne 3) {
    throw "UniverseBuilderCore.exe was not linked as a console application."
}

New-Item -ItemType Directory -Force -Path $distributionDir | Out-Null
if (Test-Path $releaseDir) { Remove-Item -Recurse -Force $releaseDir }
if (Test-Path $archive) { Remove-Item -Force $archive }
New-Item -ItemType Directory -Force -Path `
    $releaseDir, `
    (Join-Path $releaseDir "Data"), `
    (Join-Path $releaseDir "manifests"), `
    (Join-Path $releaseDir "licenses") | Out-Null

Copy-Item $guiExecutable (Join-Path $releaseDir "StockUniverseBuilder.exe")
Copy-Item $coreExecutable (Join-Path $releaseDir "UniverseBuilderCore.exe")
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
    $packagedGui = Join-Path $testDir "StockUniverseBuilder.exe"
    $packagedCore = Join-Path $testDir "UniverseBuilderCore.exe"
    if (-not (Test-Path $packagedGui) -or -not (Test-Path $packagedCore)) {
        throw "The packaged release does not contain both Windows executables."
    }
    if (Test-Path (Join-Path $testDir "APIKeys.txt")) {
        throw "APIKeys.txt must never be included in the Windows release."
    }
    if (Test-Path (Join-Path $testDir "ticket.txt")) {
        throw "Generated ticket.txt must never be included in the Windows release."
    }

    $missingTicket = Join-Path $testDir "missing-ticket.txt"
    $ticketOutput = & $packagedCore $missingTicket 2>&1
    $ticketStatus = $LASTEXITCODE
    if ($ticketStatus -eq 0 -or ($ticketOutput -join "`n") -notmatch "Copy ticket.example.txt") {
        throw "The Windows core missing-ticket check failed."
    }

    Copy-Item (Join-Path $testDir "ticket.example.txt") (Join-Path $testDir "ticket.txt")
    Copy-Item (Join-Path $testDir "APIKeys.example.txt") (Join-Path $testDir "APIKeys.txt")
    Push-Location $testDir
    try {
        $credentialOutput = & $packagedCore (Join-Path $testDir "ticket.txt") 2>&1
        $credentialStatus = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    if ($credentialStatus -eq 0 -or ($credentialOutput -join "`n") -notmatch "example placeholders") {
        throw "The Windows missing-credential check failed."
    }
}
finally {
    if (Test-Path $testDir) { Remove-Item -Recurse -Force $testDir }
}

Compress-Archive -Path $releaseDir -DestinationPath $archive -CompressionLevel Optimal
Write-Host "Windows release ready: $archive"
