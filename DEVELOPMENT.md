# Developer build and release guide

Ordinary release users do not need any of these tools. This file is for contributors
who build the native executables and ZIPs.

## Source dependencies

- A C++17 compiler
- libcurl development files
- nlohmann/json headers
- CMake 3.21 or newer for the Windows build
- vcpkg for the Windows static dependencies

## Linux x64

On Ubuntu 24.04 or WSL Ubuntu 24.04, install the compiler packages once:

```bash
sudo apt-get install g++ libcurl4-openssl-dev nlohmann-json3-dev
```

Run:

```bash
./scripts/build-release-linux.sh
```

This compiles the native binary, assembles `dist/Stock-Universe-Builder-linux-x64/`,
bundles its non-glibc shared libraries and their installed copyright files, runs the
packaging smoke tests, and creates `dist/Stock-Universe-Builder-linux-x64.zip`.

## Windows x64

Use a Developer PowerShell for Visual Studio 2022 with CMake, Git, and vcpkg available.
Set `VCPKG_ROOT` to the vcpkg folder, then run:

```powershell
.\scripts\build-release-windows.ps1
```

The script uses the `x64-windows-static` triplet. libcurl, zlib, the C++ runtime, and
the program are linked into `StockUniverseBuilder.exe`; TLS uses Windows Schannel.
The release has no compiler or DLL prerequisite. vcpkg copyright files are copied into
the release before the ZIP is created.

The GitHub Actions workflow performs the same Windows and Linux builds. A manually
started run uploads both ZIPs as workflow artifacts. A tag beginning with `v` also
attaches them to a GitHub Release.

## Direct source build

The existing VS Code task remains available. Its equivalent Linux command is:

```bash
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp \
  Asset_Pull/assetPull.cpp Data_Pull/AlpacaClient.cpp \
  Data_Processing/DataProcessor.cpp -o app -lcurl
```

Run `./app ticket.txt`, or pipe a ticket to standard input for compatibility with older
developer workflows.

## Release contents and checks

The assembly scripts copy only the executable, launchers, safe examples, beginner
guide, third-party notices, and empty `Data/` and `manifests/` folders. Local
credentials, tickets, CSV datasets, and generated manifests are never copied.

`tests/packaging-smoke.sh RELEASE_DIRECTORY` checks a path containing spaces, launcher
resolution, missing-ticket and missing-credential guidance, directory creation,
canonical CSV output, and unchanged manifest statuses using a local existing-file
fixture. Live Alpaca verification requires local credentials and is intentionally kept
out of CI.
