@echo off
rem Runs a command with the Visual Studio x64 build environment loaded (vcvars64.bat), which the MSVC presets need:
rem
rem     scripts\msvc.cmd cmake --preset msvc-x64-debug -DFENRIR_BUILD_EXAMPLES=OFF
rem     scripts\msvc.cmd cmake --build out/build/msvc-x64-debug --target FenrirScriptingTests
rem     scripts\msvc.cmd ctest --test-dir out/build/msvc-x64-debug -R "Scripting|RefCompile"
rem
rem Run it from cmd, which passes the arguments through as written. From PowerShell use scripts\msvc.ps1: PowerShell drops
rem the quotes around an argument, and cmd would then read a | in it (a ctest regex) as a pipe. The clang presets need
rem neither.
setlocal

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
    echo msvc.cmd: vswhere.exe is not at "%vswhere%": install Visual Studio 2022 with the C++ workload. 1>&2
    exit /b 1
)
set "vcvars="
for /f "usebackq delims=" %%p in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "vcvars=%%p"
if not defined vcvars (
    echo msvc.cmd: no Visual Studio with the x64 C++ tools found. 1>&2
    exit /b 1
)
if "%~1"=="" (
    echo usage: scripts\msvc.cmd ^<command^> [arguments...] 1>&2
    exit /b 2
)

rem vcvars64.bat runs vswhere by name itself.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%vcvars%" >nul || exit /b 1
%*
exit /b %errorlevel%
