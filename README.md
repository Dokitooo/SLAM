> **Download:** [SLAM_v1.0.1.zip](https://github.com/Dokitooo/SLAM/releases/download/v1.0.1/SLAM_v1.0.1.zip) (latest release) ·
> [SLAM_v1.0.1_Extras.zip](https://github.com/Dokitooo/SLAM/releases/download/v1.0.1/SLAM_v1.0.1_Extras.zip) (`SLAM.asi`, HelloSLAM) ·
> [all releases](https://github.com/Dokitooo/SLAM/releases)

# SLAM 1.0.1 — Script Loader & Add-on Manager for NBA 2K14 (PC)

SLAM loads mods for NBA 2K14, the way CLEO does for GTA San Andreas. Mods like **BodyHook**,
**CrowdTune**, **NosebleedCam** and **SigExt** need it. It also writes a crash report when the
game crashes, and tells you in plain words what most likely caused it.

SLAM changes nothing on disk: `nba2k14.exe` stays untouched. Everything happens in memory while
the game runs. When the game starts, SLAM shows the list of loaded mods at the top left for
3 seconds (text only), then nothing more. Everything else goes to `SLAM\SLAM.log`.

---

## Install

Copy everything from the **`Drag to 2K14 FOLDER`** folder of the zip into your NBA 2K14 folder (the one
with `nba2k14.exe`):

```
NBA 2K14\
    nba2k14.exe
    dinput8.dll              <- SLAM
    SLAM\
        SLAMCrash.exe        <- the crash window (and the Program Files fix)
```

Then put SLAM mods into `SLAM\`, each mod in its own folder:

```
    SLAM\
        BodyHook\BodyHook.asi   (+ its .ini and files)
        CrowdTune\CrowdTune.asi
```

Want to check that it works? From `SLAM_v1.0.1_Extras.zip`, copy `Extras\SLAM\HelloSLAM` into your `SLAM\` folder: it's a
small example mod that only writes `SLAM\HelloSLAM\HelloSLAM.log` (delete it afterwards).

Start the game. `SLAM\SLAM.log` lists what was loaded. `SLAM\SLAM.ini` is created on the first
start.

**Already have a `dinput8.dll` in the game folder?** It is probably an older BodyHook (which had
its own loader) or another mod:

- Older BodyHook → replace it with SLAM's `dinput8.dll` and get the BodyHook version made for
  SLAM.
- Another mod that came as `dinput8.dll` → rename it to **`dinput8_next.dll`** and keep it in the
  game folder. SLAM loads it first, so it works as before.

**Already use another ASI loader** (e.g. Ultimate ASI Loader as `winmm.dll` or `d3d9.dll`)?
Don't use SLAM's `dinput8.dll`. Put `Extras\SLAM.asi` from `SLAM_v1.0.1_Extras.zip` into your `scripts\` folder instead, and
keep `SLAM\SLAMCrash.exe` as above.

## NBA 2K14 in Program Files

Windows protects `C:\Program Files` and `C:\Program Files (x86)`. There, the game (and with it SLAM and
every mod) may not write into its own folder, so Windows quietly puts the files somewhere else
(`%LOCALAPPDATA%\VirtualStore\...`): `SLAM.log`, the mods' logs, ini and roster files never show up in
the game folder, and changes you make to the mods' files can be ignored.

SLAM 1.0.1 notices this when the game starts, before any mod loads, and asks once:

- **Yes**: Windows asks for administrator rights (the prompt shows `SLAMCrash.exe`). Your Windows user may then
  change the NBA 2K14 folder (SLAM, every SLAM mod and normal `.asi` mods), and the files Windows had put into
  VirtualStore are moved back (the newer copy wins). Details: `SLAM\SLAM folder permission.log`. Needed once.
- **No**: the game starts as before; SLAM asks again next time.
- **Cancel**: the game starts and SLAM doesn't ask again (`[Folder] Ask=0` in `SLAM.ini`).

Or move the game out of Program Files (e.g. `C:\Games\NBA 2K14`), or run `nba2k14.exe` as administrator.

## Update

Replace `dinput8.dll` and `SLAM\SLAMCrash.exe`. Your settings (`SLAM\SLAM.ini`) and mods stay.
SLAM is meant to change rarely: mods get updated, SLAM mostly doesn't.

## Uninstall

Delete `dinput8.dll` and the `SLAM` folder (this also removes the SLAM mods in it). If you
renamed another mod to `dinput8_next.dll`, rename it back to `dinput8.dll`.

---

## Where mods can go

SLAM loads `.asi` files from these places, in this order:

1. `SLAM\` and `SLAM\<Mod>\` (also one folder deeper)
2. `scripts\`, `plugins\`, `asi\` (and their subfolders)
3. the game folder itself

A file name is loaded only once (the first one found wins; `SLAM.log` says which one was
skipped).

**Mods that need extra DLLs:** put the DLLs next to the mod's `.asi`, in `SLAM\<Mod>\`. SLAM
loads each mod so that Windows looks in the mod's own folder first. If two mods bring different
DLLs with the same name, Windows can only use one of them. `SLAM.log` reports that as a "DLL
clash" and names both files.

**Mods that are not made for SLAM** (normal `.asi` mods) still load: SLAM is an ASI loader too.
They are listed in `SLAM.log` as "plain .asi". SLAM just can't check their version or
coordinate their changes with other mods.

## The mod list at start

For 3 seconds after the game window appears, the top left shows what SLAM loaded:

```
SLAM 1.0.1
BodyHook 1.19.9      SLAM
CrowdTune 1.5.1      SLAM
SomeOldMod           plain .asi
Broken.asi           FAILED to load
1 conflict - see SLAM\SLAM.log
```

**SLAM** = a SLAM mod that started fine. **plain .asi** = a normal .asi mod (not made for SLAM).
Red = didn't load or didn't start (the reason is in `SLAM.log`). Turn the list off with
`[Overlay] Show=0` in `SLAM\SLAM.ini`.

## Settings: `SLAM\SLAM.ini`

```ini
[Mods]
LoadFirst=CrowdTune.asi        ; load these first, in this order
Disabled=OldMod.asi            ; don't load these

[Crash]
Enabled=1        ; crash reports + crash window
Popup=1          ; show the crash window
Dump=1           ; also write a .dmp for mod authors
LogExceptions=1  ; note smaller errors the game survives in SLAM.log
TestKey=Ctrl+F11 ; test key: test report + crash window (any combination, e.g. Alt+T; 0 = off)
TestCloses=0     ; 1 = the test really closes the game, like a crash
Keep=20          ; crash reports kept in SLAM\crash\
FileTrail=1      ; remember which game files (.iff .ros ...) the game opened, for the crash report

[Folder]
Ask=1            ; game in Program Files: offer the fix at start (0 = don't ask)

[Overlay]
Show=1           ; list of loaded mods at the top left when the game starts
Seconds=3        ; for how long
```

Delete `SLAM.ini` to get the defaults back. Settings an older `SLAM.ini` doesn't have yet are added to it
on the next start (your own values stay).

---

## "SLAM not installed" window

If you install a mod that needs SLAM but SLAM isn't installed (or is too old), a small window
appears when the game starts:

> SLAM is not installed, so these mods won't work:
>   - BodyHook
>   - CrowdTune

Close it and the game starts normally, without those mods. Install (or update) SLAM and the
window won't appear again. With SLAM installed it never shows.

## When the game crashes

A window opens with:

- **the likely cause in plain words**, e.g. *"The crash happened inside the mod CrowdTune"*,
  *"The game ran out of memory"*, or *"The crash happened in the graphics driver"*, plus what
  you can try;
- **who to send it to** (the mod's author);
- **the full report**, with buttons to copy it or open the crash folder.

The crash window also looks around before it shows the cause:

- **Changed game files.** SLAM remembers every game file the game opened (`.iff`, `.ros`, ...).
  The window checks them on disk: files edited with RED MC and files changed after the game was
  installed are listed, **most recently changed first**. When the crash is in the game's own code,
  the file you changed last is named as the first suspect, even when the crash happens while the
  game is loading a different file (a file the game can't use often makes it fail later).
- **Every mod's log.** All `.log` files in `SLAM\` and `SLAM\<Mod>\` written this session: lines
  that report a problem (error, failed, warning, ...) in the last 3 minutes before the crash are
  shown with the cause, and the last lines of each log are added to the report.
- **Probe / research builds** of mods that are still installed.

All of it is added to the end of the report (once), so the mod author sees it too.

Reports are saved in `SLAM\crash\` (`crash_<date>_<time>.log` + `.dmp`). Send both files to the
mod author. You can open an old report again by starting `SLAM\SLAMCrash.exe`, or by dragging a
report onto it.

Press **Ctrl+F11** in the game to see a test report; the game keeps running. Want to test the
real thing? Set `TestCloses=1` in `SLAM.ini`: the test key then closes the game the way a crash
does. Any key combination works as the test key (`TestKey=Alt+T`, `TestKey=Ctrl+Shift+F12`, ...).

---

## `SLAM\SLAM.log`

Every start writes a new `SLAM.log` (the previous one is kept as `SLAM.previous.log`). It says:

- which mods were loaded, from where, and their versions;
- mods that need a **newer SLAM** (they are not started; the game still runs);
- **conflicts**: two mods changing the same part of the game. SLAM names both mods and blocks the
  second change instead of letting the mods silently break each other;
- DLL clashes, failed loads (with the reason), and memory use every 30 s.

Each SLAM mod also writes its own log to `SLAM\<Mod>\<Mod>.log`.

---

## Troubleshooting

| Problem | Try |
|---|---|
| No `SLAM\SLAM.log` after starting | `dinput8.dll` isn't next to `nba2k14.exe`, or another ASI loader is used → see "Already use another ASI loader". |
| No `SLAM\SLAM.log`, mods' files "don't save", game in Program Files | See "NBA 2K14 in Program Files" (answer Yes once, or move the game). The files may be in `%LOCALAPPDATA%\VirtualStore\`. |
| "SLAM is too old" window | Install the latest SLAM. |
| A mod is listed as `FAILED` in `SLAM.log` | The line says why; error 126 = a DLL the mod needs is missing (put it next to the `.asi`). |
| `CONFLICT` lines in `SLAM.log` | Two mods change the same thing. Use only one of them, or ask the authors. |
| Keyboard/controller stops working | Make sure the system's `dinput8.dll` is fine. A renamed `dinput8_next.dll` from another mod may be the cause; move it out to test. |

---

SLAM 1.0.1 — © 2026 Alexiconn2K — free and open source (BSD 2-Clause, see `LICENSE.txt`).
Uses MinHook by Tsuda Kageyu (BSD 2-Clause). Mod makers: see `SDK\SDK.md`.
