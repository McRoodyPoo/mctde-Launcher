@echo off
REM Builds the standalone mctde launcher (mctde_launcher.exe).
REM 32-bit, statically linked (/MT) so it has no runtime deps -- runs under Proton/Wine
REM in the same prefix as the game. Drop the result in the DATA folder beside DARKSOULS.exe.
setlocal
set VCVARS=Z:\Visual Studio\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" (
  echo vcvars32.bat not found at "%VCVARS%" - edit this path for your VS install.
  exit /b 1
)
call "%VCVARS%"
cd /d "%~dp0"
if not exist mctde.ico (
  echo mctde.ico not found - generating the stand-in icon...
  powershell -ExecutionPolicy Bypass -File "%~dp0make_icon.ps1"
)
if not exist bin mkdir bin
cd bin
rc /nologo /fo mctde_launcher.res "..\mctde_launcher.rc"
if %ERRORLEVEL% NEQ 0 (echo RESOURCE COMPILE FAILED & exit /b %ERRORLEVEL%)
cl /nologo /O2 /MT /EHsc /DUNICODE /D_UNICODE /I".." "..\mctde_launcher.cpp" mctde_launcher.res ^
   /Fe:mctde_launcher.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib
set ERR=%ERRORLEVEL%
del /q *.obj *.res 2>nul
if %ERR% NEQ 0 (echo BUILD FAILED & exit /b %ERR%)
echo.
echo Built: %~dp0bin\mctde_launcher.exe
echo Deploy to: DATA\mctde_launcher.exe  (beside DARKSOULS.exe / d3d9.dll)
endlocal
