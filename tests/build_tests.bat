@echo off
rem Build and run the desktop tests with MSVC (Visual Studio 2019/2022).
rem Usage: tests\build_tests.bat   (from any directory)
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - install Visual Studio with C++ tools.
    exit /b 2
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo Visual Studio with C++ tools not found.
    exit /b 2
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 2
if not exist "%HERE%build" mkdir "%HERE%build"
pushd "%HERE%build"
cl /nologo /std:c++14 /O2 /W3 /EHsc /fp:precise ^
   ..\test_main.cpp ..\..\Controller.cpp ..\..\Engine.cpp ..\..\MidiMapper.cpp ..\..\MidiParser.cpp ^
   ..\..\Params.cpp ..\..\SettingsStorage.cpp ..\..\Synth.cpp ..\..\Vocoder.cpp ^
   /Fe:vocoder_tests.exe
if errorlevel 1 (
    popd
    exit /b 1
)
.\vocoder_tests.exe
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
