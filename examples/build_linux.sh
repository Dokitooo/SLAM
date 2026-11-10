#!/bin/sh
# Builds the example mods on Linux (LLVM 18 + MinGW-w64 headers, see ../build/build_linux.sh).
# Output: ../bin/examples/<Mod>/<Mod>.asi
set -e
export PATH="/usr/lib/llvm-18/bin:$PATH"
cd "$(dirname "$0")"
: "${MGW:?set MGW to the mingw-w64-headers folder}"; : "${GEN:?set GEN to the folder with the generated _mingw.h}"
OBJ=../build/obj; OUT=../bin/examples; mkdir -p $OBJ
INC="-imsvc $GEN -imsvc $MGW/crt -imsvc $MGW/include -imsvc $MGW/defaults/include"
CF="--target=i686-pc-windows-msvc /c /O2 /GS- /Zl /W3 -D_X86_ -fgnuc-version=4.2.1 $INC -I../sdk"
llvm-dlltool -m i386 -k -d kernel32.def -l $OBJ/ex_kernel32.lib
for m in HelloSLAM:HelloSLAM ModTemplate:MyMod; do
  dir=${m%%:*}; name=${m##*:}
  mkdir -p $OUT/$name
  clang-cl $CF /Fo$OBJ/ex_$name.obj $dir/$name.c
  lld-link /brepro /dll /nodefaultlib /entry:DllMain@12 /subsystem:windows,5.01 /machine:x86 /safeseh:no /opt:ref /dynamicbase /nxcompat \
    /noimplib /out:$OUT/$name/$name.asi $OBJ/ex_$name.obj $OBJ/ex_kernel32.lib
  [ -f $dir/$name.ini ] && cp $dir/$name.ini $OUT/$name/
done
ls -l $OUT/*/*.asi
