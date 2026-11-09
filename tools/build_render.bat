@echo off
rem Build tools\build\render_wav.exe with MSVC. Extra args are passed to cl
rem (e.g. /DVOCODER_FILTER_SECTIONS=1 to try compile-time variants).
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools not found.
    exit /b 2
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 2
if not exist "%HERE%build" mkdir "%HERE%build"
pushd "%HERE%build"
cl /nologo /std:c++14 /O2 /W3 /EHsc %* ^
   ..\render_wav.cpp ..\..\Engine.cpp ..\..\Params.cpp ..\..\Synth.cpp ..\..\Vocoder.cpp ^
   /Fe:render_wav.exe >nul
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
