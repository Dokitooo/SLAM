#!/usr/bin/env python3
"""Checks built SLAM files: 32-bit x86, expected exports, only the expected system DLLs imported.
Uses llvm-readobj (LLVM 18). Exit code 1 on any problem."""
import subprocess, sys, re, os
RO = "/usr/lib/llvm-18/bin/llvm-readobj"
EXPECT = {
    "dinput8.dll":   ({"kernel32.dll", "user32.dll", "gdi32.dll"}, {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer", "DllUnregisterServer"}),
    "SLAM.asi":      ({"kernel32.dll", "user32.dll", "gdi32.dll"}, set()),
    "SLAMCrash.exe": ({"kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "advapi32.dll"}, set()),
    "HelloSLAM.asi": ({"kernel32.dll"}, {"SlamModInfo", "SlamModStart"}),
    "MyMod.asi":     ({"kernel32.dll"}, {"SlamModInfo", "SlamModStart"}),
}
bad = 0
for path in sys.argv[1:]:
    name = os.path.basename(path)
    imps_want, exps_want = EXPECT.get(name, (None, None))
    hdr = subprocess.check_output([RO, "--file-headers", path], text=True)
    imps = {l.split(":", 1)[1].strip().lower() for l in subprocess.check_output([RO, "--coff-imports", path], text=True).splitlines() if l.strip().startswith("Name:")}
    exps = set(re.findall(r"Name: (\w+)", subprocess.check_output([RO, "--coff-exports", path], text=True)))
    probs = []
    if "IMAGE_FILE_MACHINE_I386" not in hdr: probs.append("not 32-bit x86")
    if imps_want is not None and not imps <= imps_want: probs.append("unexpected imports %s" % sorted(imps - imps_want))
    if exps_want is not None and exps != exps_want: probs.append("exports %s" % sorted(exps))
    size = os.path.getsize(path)
    print("%-14s %7d bytes  imports %-45s exports %d  %s" % (name, size, ",".join(sorted(imps)), len(exps), "OK" if not probs else "PROBLEM: " + "; ".join(probs)))
    bad += bool(probs)
sys.exit(1 if bad else 0)
