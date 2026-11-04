@echo off
rem Builds SLAM with Visual Studio (2010 or newer). Run it from the
rem "x86 Native Tools Command Prompt for VS" (32-bit!). Output in bin\ .
setlocal
cd /d "%~dp0.."
if not exist bin mkdir bin
if not exist build\obj mkdir build\obj
set O=build\obj
set CF=/nologo /c /O2 /GS- /Gs999999 /Zl /W3 /Isdk /Ithird_party\minhook\include
set LF=/nologo /nodefaultlib /machine:x86 /safeseh:no /opt:ref /dynamicbase /nxcompat

cl %CF% /Fo%O%\ third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde32.c || goto :fail
set MH=%O%\buffer.obj %O%\hook.obj %O%\trampoline.obj %O%\hde32.obj
rc /nologo /fo %O%\slam.res src\slam.rc || goto :fail

rem dinput8.dll
cl %CF% /Fo%O%\slam.obj src\slam.c || goto :fail
link %LF% /dll /entry:DllMain /subsystem:windows /def:src\dinput8.def /out:bin\dinput8.dll %O%\slam.obj %MH% %O%\slam.res kernel32.lib user32.lib gdi32.lib || goto :fail
rem SLAM.asi
cl %CF% /DSLAM_ASI /Fo%O%\slam_asi.obj src\slam.c || goto :fail
link %LF% /dll /entry:DllMain /subsystem:windows /noimplib /out:bin\SLAM.asi %O%\slam_asi.obj %MH% %O%\slam.res kernel32.lib user32.lib gdi32.lib || goto :fail
rem SLAMCrash.exe
cl /nologo /c /O1 /GS- /Zl /W3 /Fo%O%\slamcrash.obj src\crashui\slamcrash.c || goto :fail
pushd src\crashui & rc /nologo /fo ..\..\%O%\slamcrash.res slamcrash.rc & popd
link %LF% /entry:start /subsystem:windows /largeaddressaware /out:bin\SLAMCrash.exe %O%\slamcrash.obj %O%\slamcrash.res kernel32.lib user32.lib gdi32.lib shell32.lib || goto :fail

del /q bin\*.lib bin\*.exp 2>nul
echo.
echo Built: bin\dinput8.dll  bin\SLAM.asi  bin\SLAMCrash.exe
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
