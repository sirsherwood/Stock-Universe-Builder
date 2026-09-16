@echo off
setlocal
cd /d "%~dp0"

if not exist "ticket.txt" (
  echo Could not open ticket.txt.
  echo Copy ticket.example.txt to ticket.txt, then run this launcher again.
  set "exit_code=1"
  goto finished
)

if not exist "UniverseBuilderCore.exe" (
  echo Could not run UniverseBuilderCore.exe.
  echo Keep run.bat and UniverseBuilderCore.exe together in the same folder.
  set "exit_code=1"
  goto finished
)

"%~dp0UniverseBuilderCore.exe" "%~dp0ticket.txt"
set "exit_code=%ERRORLEVEL%"

:finished
echo.
echo Exit status: %exit_code%
if "%exit_code%"=="0" (
  echo Finished. Historical CSVs are in the Data folder and run results are in manifests.
) else (
  echo The program stopped with exit status %exit_code%.
  echo Read the message above for the next step.
)

if not defined STOCK_BUILDER_NO_PAUSE pause
exit /b %exit_code%
