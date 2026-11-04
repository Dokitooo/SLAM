#!/bin/sh
# Builds SLAM on Linux with LLVM 18 (clang-cl, lld-link, llvm-rc, llvm-dlltool). No Windows SDK needed:
# Windows headers come from the MinGW-w64 headers (MGW = mingw-w64-headers folder, GEN = folder with a
# generated _mingw.h). Output in bin\:
#   dinput8.dll     SLAM (goes next to nba2k14.exe)
#   SLAM.asi        SLAM for players who already use another ASI loader
#   SLAMCrash.exe   crash window (goes in SLAM\)
#   examples\...    example mods (built by examples/build_linux.sh, if present)
set -e
export PATH="/usr/lib/llvm-18/bin:$PATH"
cd "$(dirname "$0")/.."
: "${MGW:=/home/dev/ext/mgw0/mingw-w64-headers}"; : "${GEN:=/home/dev/ext/gen}"
OBJ=build/obj; OUT=bin; mkdir -p $OBJ $OUT
INC="-imsvc $GEN -imsvc $MGW/crt -imsvc $MGW/include -imsvc $MGW/defaults/include"
CF="--target=i686-pc-windows-msvc /c /O2 /GS- /Gs999999 /Zl /W3 -D_X86_ -fgnuc-version=4.2.1 $INC"
LF="/brepro /nodefaultlib /machine:x86 /safeseh:no /opt:ref /dynamicbase /nxcompat"

for k in kernel32 user32 gdi32; do llvm-dlltool -m i386 -k -d build/linux/$k.def -l $OBJ/$k.lib; done
for k in kernel32 user32 gdi32 shell32; do llvm-dlltool -m i386 -k -d build/linux/crash_$k.def -l $OBJ/crash_$k.lib; done

# MinHook, unmodified (its intrinsics clash with clang's intrin.h, so they are off)
MH=""
for f in buffer hook trampoline hde/hde32; do
  o=$OBJ/mh_$(basename $f).obj
  clang-cl $CF -DMINHOOK_DISABLE_INTRINSICS -Ithird_party/minhook/include /Fo$o third_party/minhook/src/$f.c
  MH="$MH $o"
done
llvm-rc /fo $OBJ/slam.res src/slam.rc

# dinput8.dll
clang-cl $CF -Isdk -Ithird_party/minhook/include /Fo$OBJ/slam.obj src/slam.c
lld-link $LF /dll /entry:DllMain@12 /subsystem:windows,5.01 /def:src/dinput8.def /out:$OUT/dinput8.dll /map:$OBJ/dinput8.map \
  $OBJ/slam.obj $MH $OBJ/slam.res $OBJ/kernel32.lib $OBJ/user32.lib $OBJ/gdi32.lib
# SLAM.asi (no DirectInput exports)
clang-cl $CF -DSLAM_ASI -Isdk -Ithird_party/minhook/include /Fo$OBJ/slam_asi.obj src/slam.c
lld-link $LF /dll /entry:DllMain@12 /subsystem:windows,5.01 /out:$OUT/SLAM.asi \
  $OBJ/slam_asi.obj $MH $OBJ/slam.res $OBJ/kernel32.lib $OBJ/user32.lib $OBJ/gdi32.lib

# SLAMCrash.exe
clang-cl $CF /O1 /Fo$OBJ/slamcrash.obj src/crashui/slamcrash.c
(cd src/crashui && llvm-rc /fo ../../$OBJ/slamcrash.res slamcrash.rc)
lld-link $LF /entry:start /subsystem:windows,5.01 /largeaddressaware /out:$OUT/SLAMCrash.exe $OBJ/slamcrash.obj $OBJ/slamcrash.res \
  $OBJ/crash_kernel32.lib $OBJ/crash_user32.lib $OBJ/crash_gdi32.lib $OBJ/crash_shell32.lib

rm -f $OUT/*.lib $OUT/*.exp
[ -x examples/build_linux.sh ] && examples/build_linux.sh

# checks (native Linux)
gcc -Wall -Wno-unused-function -O1 tools/test_cause/t_cause.c -o $OBJ/t_cause && $OBJ/t_cause | tail -1
python3 tools/check_pe.py $OUT/dinput8.dll $OUT/SLAM.asi $OUT/SLAMCrash.exe $OUT/examples/*/*.asi
