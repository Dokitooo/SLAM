#!/usr/bin/env python3
"""Writes build/linux/{kernel32,user32,gdi32}.def from what the compiled SLAM objects import.
Run after changing which Windows functions SLAM uses:  python3 tools/gen_defs.py build/obj/slam.obj build/obj/mh_*.obj
"""
import re, subprocess, sys
NM = "/usr/lib/llvm-18/bin/llvm-nm"
GDI = {"CreateCompatibleDC", "CreateDIBSection", "CreateFontA", "DeleteDC", "DeleteObject", "GdiFlush",
       "GetTextExtentPoint32A", "SelectObject", "SetBkMode", "SetTextColor", "TextOutA"}
USER = {"GetAsyncKeyState", "MessageBoxA", "wsprintfA", "wvsprintfA"}
NOIMP = {"CreateToolhelp32Snapshot", "Module32First", "Module32Next", "Thread32First", "Thread32Next"}  # declared without dllimport
out = {"kernel32": set(), "user32": set(), "gdi32": set()}
for obj in sys.argv[1:]:
    for line in subprocess.check_output([NM, "-u", obj], text=True).splitlines():
        sym = line.split()[-1]
        m = re.match(r"(__imp_)?_(\w+?)(@\d+)?$", sym)
        if not m or (not m.group(1) and m.group(2) not in NOIMP): continue
        name = m.group(2) + (m.group(3) or "")
        dll = "gdi32" if m.group(2) in GDI else "user32" if m.group(2) in USER else "kernel32"
        out[dll].add(name)
for dll, names in out.items():
    with open("build/linux/%s.def" % dll, "w") as f:
        f.write("LIBRARY %s.dll\nEXPORTS\n" % dll + "".join("    %s\n" % n for n in sorted(names)))
    print(dll, len(names))
