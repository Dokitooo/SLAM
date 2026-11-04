@echo off
rem Builds the example mods with Visual Studio. Run from the "x86 Native Tools Command Prompt for VS".
rem Your own mod: copy ModTemplate, rename, add it below. Output: ..\bin\examples\<Mod>\<Mod>.asi
setlocal
cd /d "%~dp0"
call :mod HelloSLAM HelloSLAM || goto :fail
call :mod ModTemplate MyMod || goto :fail
echo Built the examples in ..\bin\examples\
exit /b 0

:mod
if not exist ..\bin\examples\%2 mkdir ..\bin\examples\%2
cl /nologo /O2 /GS- /W3 /I..\sdk /LD %1\%2.c /Fo..\bin\examples\%2\ /Fe..\bin\examples\%2\%2.asi /link /noimplib kernel32.lib || exit /b 1
if exist %1\%2.ini copy /y %1\%2.ini ..\bin\examples\%2\ >nul
del /q ..\bin\examples\%2\*.obj ..\bin\examples\%2\*.exp 2>nul
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
