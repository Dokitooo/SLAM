#!/bin/sh
# Builds SLAM and packs SLAM_v<version>.zip (needs the same tools as build/build_linux.sh).
#   tools/make_release.sh <out folder>
set -e
cd "$(dirname "$0")/.."
OUTDIR=${1:?out folder}
VER=$(sed -n 's/^#define SLAM_VERSION "\(.*\)"/\1/p' src/slam.c)
rm -rf build/obj bin && ./build/build_linux.sh
R=$OUTDIR/SLAM_v$VER; rm -rf "$R"; mkdir -p "$R/Game folder/SLAM" "$R/Extras/SLAM" "$R/SDK/examples" "$R/source"
cp README.md LICENSE.txt CHANGELOG.md "$R/"
cp bin/dinput8.dll "$R/Game folder/"; cp bin/SLAMCrash.exe "$R/Game folder/SLAM/"
cp bin/SLAM.asi "$R/Extras/"; cp -r bin/examples/HelloSLAM "$R/Extras/SLAM/"
cp sdk/slam.h SDK.md "$R/SDK/"
cp -r examples/HelloSLAM examples/ModTemplate examples/kernel32.def examples/build_msvc.bat "$R/SDK/examples/"
sed 's|-I\.\./sdk|-I..|' examples/build_linux.sh > "$R/SDK/examples/build_linux.sh"; chmod +x "$R/SDK/examples/build_linux.sh"
sed -i 's|/I\.\.\\sdk|/I..|' "$R/SDK/examples/build_msvc.bat"
cp -r src sdk build tools third_party examples README.md SDK.md LICENSE.txt CHANGELOG.md "$R/source/"
rm -rf "$R/source/build/obj" "$R/source/build/build_slamcrash_linux.sh"
find "$R" -name "*.obj" -delete
(cd "$OUTDIR" && rm -f "SLAM_v$VER.zip" && zip -qr -X "SLAM_v$VER.zip" "SLAM_v$VER")
ls -l "$OUTDIR/SLAM_v$VER.zip"
