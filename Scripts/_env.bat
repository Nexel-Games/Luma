@echo off
REM Activates the MSVC x64 build environment, then runs the passed command.
REM Usage: _env.bat <command> [args...]
REM
REM Finds any Visual Studio 2022+ installation with the C++ x64 tools (Build
REM Tools, Community, Professional, Enterprise) through vswhere, which every
REM Visual Studio installer provides. Set LUMA_VCVARS to a vcvars64.bat path
REM to force a specific installation.
setlocal

set "VCVARS=%LUMA_VCVARS%"
if defined VCVARS goto activate

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto notfound

set "VSDIR="
REM (No parenthesised blocks around %VSWHERE%: the ")" in "(x86)" would end them.)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR goto found
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto notfound
:found
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvars64.bat"

:activate
if not exist "%VCVARS%" goto badpath
call "%VCVARS%" >nul 2>&1
if errorlevel 1 goto activatefailed
%*
exit /b %ERRORLEVEL%

:activatefailed
echo [Luma] Failed to activate the MSVC environment: "%VCVARS%" 1>&2
exit /b 1

:badpath
echo [Luma] vcvars64.bat not found: "%VCVARS%" (check LUMA_VCVARS or your Visual Studio install) 1>&2
exit /b 1

:notfound
echo [Luma] No Visual Studio with the C++ x64 tools was found. Install Visual Studio 2022 or newer 1>&2
echo [Luma] with the "Desktop development with C++" workload, or set LUMA_VCVARS to vcvars64.bat. 1>&2
exit /b 1
