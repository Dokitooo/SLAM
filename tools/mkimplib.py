#!/usr/bin/env python3
"""Import libraries for a no-CRT 32-bit build, made from what the objects actually import.
   mkimplib.py <out_dir> <obj>...   -> <out_dir>/<dll>.lib for every Windows DLL needed; prints the .lib paths.
Which DLL a function lives in comes from MinGW-w64's lib32 .def files (env MGWCRT = mingw-w64-crt folder)."""
import glob, os, re, subprocess, sys
LLVM = "/usr/lib/llvm-18/bin/"
crt = os.environ["MGWCRT"]  # the mingw-w64-crt folder
PREFER = ["kernel32", "user32", "gdi32", "shell32", "advapi32", "ole32", "oleaut32", "winmm", "d3d9", "dbghelp", "version"]
where = {}
for dll in PREFER:
    for d in (crt + "/lib32/%s.def" % dll, crt + "/lib-common/%s.def" % dll, crt + "/lib-common/%s.def.in" % dll):
        if not os.path.exists(d): continue
        for line in open(d, errors="ignore"):
            m = re.match(r"\s*([A-Za-z_]\w*)(@\d+)?", line)
            if m and m.group(1) not in ("LIBRARY", "EXPORTS") and m.group(1) not in where: where[m.group(1)] = dll
NOIMP = {"CreateToolhelp32Snapshot", "Module32First", "Module32Next", "Thread32First", "Thread32Next"}
out, missing = {}, []
for obj in sys.argv[2:]:
    for line in subprocess.check_output([LLVM + "llvm-nm", "-u", obj], text=True).splitlines():
        sym = line.split()[-1]
        m = re.match(r"(__imp_)?_(\w+?)(@\d+)?$", sym)
        if not m or (not m.group(1) and m.group(2) not in NOIMP): continue
        dll = where.get(m.group(2))
        if not dll: missing.append(m.group(2)); continue
        out.setdefault(dll, set()).add(m.group(2) + (m.group(3) or ""))
if missing: sys.exit("mkimplib: no DLL known for: " + ", ".join(sorted(set(missing))))
os.makedirs(sys.argv[1], exist_ok=True)
for dll, names in sorted(out.items()):
    d = os.path.join(sys.argv[1], dll + ".def"); l = os.path.join(sys.argv[1], dll + ".lib")
    open(d, "w").write("LIBRARY %s.dll\nEXPORTS\n" % dll + "".join("    %s\n" % n for n in sorted(names)))
    subprocess.check_call([LLVM + "llvm-dlltool", "-m", "i386", "-k", "-d", d, "-l", l])
    print(l)
