@echo off
rem Builds out\version.dll with the Visual Studio C++ build tools (x64, static runtime; the DLL depends only on
rem kernel32 and user32). Run from a plain command prompt or PowerShell:   .\build.bat
setlocal
set "VCVARS="
for %%p in (
  "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VCVARS if exist %%p set "VCVARS=%%~p"
if not defined VCVARS goto novc
call "%VCVARS%" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /LD /O2 /MT /W3 /EHsc src\dllmain.cpp /Fo:out\ /Fe:out\version.dll /link /DYNAMICBASE user32.lib
if errorlevel 1 exit /b 1
echo.
echo Built out\version.dll. Install it with install.ps1 (game closed).
exit /b 0

:novc
echo No Visual Studio C++ build tools found. Install "Build Tools for Visual Studio" with the
echo "Desktop development with C++" workload, or edit the paths at the top of this file.
exit /b 1
