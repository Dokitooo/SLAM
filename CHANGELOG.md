# SLAM changelog

## 1.1.2
- **The copied report has everything.** Crash reports now end with a `[memory]` block - what the `.dmp`
  holds, as text: the code bytes around the crash, the crashing thread's stack (up to 4 KB, zero runs
  folded), the memory at every register and at the first 24 data pointers on the stack - and one step
  further (what the pointers inside those point to), where every other thread is with its last calls,
  and every loaded module (base, size, date stamp, path). Players only press **Copy report** and paste;
  Discord turns a long paste into a `message.txt` by itself.
- The `.dmp` is still written. **Copy crash file (.zip)** (second button) copies one zip with the report,
  the dump and `SLAM.log` (paste it like a file), for when a mod author asks for the full dump. Old zips are
  cleaned up with the old reports (`[Crash] Keep`).
- Includes 1.1.1 (one graphics wrapper). Mods need no update.

## 1.1.1
- **Two graphics wrappers no longer crash the game.** A ReShade/ENB `d3d9.dll` plus a ReShade/ENB `dxgi.dll` in
  the game folder load as two ReShades on the same Direct3D device - the game crashed. When both are graphics
  wrappers, SLAM now loads Windows' own `dxgi.dll` at the game's start (before Direct3D starts), so the game
  folder's `dxgi.dll` is never loaded; `d3d9.dll` stays. Nothing is moved or deleted. `SLAM.log` names both files.
  A `d3d9.dll` that is an ASI loader or DXVK, or only one wrapper: unchanged.
  New setting `[Graphics] OneWrapper=1` (0 = load both, as before); added to an older `SLAM.ini` by itself.
- Mods need no update.

## 1.1.0
- **Better crash causes, shorter text.** The crash window now knows the NBA 2K14 crashes seen in real reports and
  says what to do in a line or two:
  - MyCareer post-game player table full (interview / recap), the game's file loader on a broken edited file,
    the cloth simulation (BodyHook bench cloth), broken `global.iff`, files saved with cut rows, a team without a
    point guard, a broken created-shoe file, a broken MyCareer save.
  - A game file that could not be opened (missing / renamed / locked) and then used empty - named in the headline.
  - Shot meter installed twice / with old loader files (root `CurvedShotMeter.asi`, `dinput8_next.dll`, the 3.5 MB
    `dinput8.dll`), graphics wrappers in the game folder (two ReShades = d3d9.dll + dxgi.dll), a plain .asi that needs
    a newer Visual C++ runtime.
  - Our mods that are older than the version that fixed a crash are named ("Also: update ...").
- Reports have two new `[facts]`: `files_failed` (game files that could not be opened) and `extra_dlls` (every
  non-Windows DLL/.asi with its size and folder). The crash window no longer counts SigExt's statistics lines,
  "crash guard" lines or BodyHook's "row list is NOT sorted" as problems.
- Mods need no update.

## 1.0.3
- **ReShade, ENB and other `d3d9.dll` wrappers:** SLAM no longer touches them. When a `d3d9.dll` that is not
  Windows' own is in the game folder, SLAM's mod list (overlay) hooks the Windows `d3d9.dll` underneath it, so the
  wrapper keeps working exactly as without SLAM. DXVK is hooked directly. `SLAM.log` says which one was used
  ("overlay: d3d9: ReShade in the game folder ..."). SLAM still installs as `dinput8.dll`. Mods need no update.

## 1.0.2
- **Plain `.asi` mods that need `InitializeASI`** (the Ultimate ASI Loader convention) now start under SLAM:
  SLAM calls it once, in load order, for each plain `.asi` it loaded itself - right after the game's first
  `DirectInput8Create` (game window and Direct3D exist then; that is when the shot meter's own `dinput8.dll` did
  it - calling it at start-up crashed the shot meter inside `d3d9.dll`). Example: `CurvedShotMeter.asi`
  came with its own `dinput8.dll` only to make that call - that `dinput8.dll` replaced SLAM, SLAM then started
  late and GotoShotExt's go-to shots never showed up. Now: keep SLAM's `dinput8.dll`, put `CurvedShotMeter.asi`
  (and its `CurvedShotMeter\` folder) in the game folder. `SLAM.log`: "started CurvedShotMeter.asi (plain .asi:
  InitializeASI called)". Mods need no update.

## 1.0.1
- **NBA 2K14 in Program Files:** the game isn't allowed to write into its own folder there, so Windows put
  `SLAM.log`, the mods' logs, ini and roster files into `%LOCALAPPDATA%\VirtualStore\` (and copies there hid
  the real files). At start, before any mod loads, SLAM now offers to fix it once: `SLAMCrash.exe /grant` runs
  with administrator rights, gives the Users group "Modify" on the NBA 2K14 folder and moves the VirtualStore
  files back (the newer copy wins; `SLAM\SLAM folder permission.log`). No / Cancel (= `[Folder] Ask=0`).
  Mods need no update.

## 1.0.0 — first release
- Loads mods (`.asi`) from `SLAM\<Mod>\`, `scripts\`, `plugins\`, `asi\` and the game folder,
  right before the game's own code starts. `[Mods] LoadFirst` / `Disabled` in `SLAM\SLAM.ini`.
- SLAM mods: version check (SLAM API 1), started in load order, `on_game_ready` once
  `nba2k14.exe` has unpacked itself.
- Shared hooks (several mods on one function, chained) and patches (same bytes shared, clashing
  bytes refused and reported with both mod names). Hook engine: MinHook.
- Mods' own DLLs are found in the mod's folder first; same-name DLL clashes reported.
- Plain `.asi` mods load too. A mod that came as `dinput8.dll` works as `dinput8_next.dll`.
- "SLAM not installed / too old" window from the mods themselves (SDK), game still starts.
- Crash reports for every crash: `SLAM\crash\*.log` + `.dmp`, crash window (`SLAMCrash.exe`)
  with the likely cause in plain words and who to send it to. Test key (default Ctrl+F11, any combination;
  `TestCloses=1` really closes the game).
- Mod list at the top left for 3 s when the game starts (text only; `[Overlay]` in `SLAM.ini`). After
  that SLAM removes its Direct3D hooks again. Everything else goes to `SLAM\SLAM.log`.
- Shared hooks on the game's imported functions (`api->hook_import`), e.g. `CreateFileA`, so mods that
  follow the same file calls (BodyHook, SigExt) chain instead of overwriting each other.
- Recognises the NBA 2K12 builds BodyHook supports (log, crash reports, game-ready detection).
- **File trail:** SLAM remembers every game file the game opens (`.iff .ros .fra .cmg .rda .bik .dat`):
  first/last time, which thread, how often. Crash reports list them newest first (the crashing
  thread's files marked) and in a `[files]` block. `[Crash] FileTrail=1` in `SLAM.ini` (0 = off).
  SLAM's link on `CreateFileA` is the innermost one, so mods that redirect file names still chain in
  front of it and the trail shows the file that was really opened.
- **The crash window checks the files:** edited with RED MC, or changed after the game was installed
  (newer than the date most of the game's `.iff` files have). Listed most recently changed first. A
  crash in the game's own code now names the most recently changed file as the first suspect.
- **The crash window reads every mod's log** (`SLAM\*.log`, `SLAM\<Mod>\*.log` written this session):
  problem lines (error, failed, warning, ...) of the last 3 minutes before the crash are shown with
  the cause; if nothing else points anywhere, the mod that logged the problem is named. The last
  lines of each log written in the last 10 minutes are added to the report.
- Probe / research builds of mods still installed are pointed out.
- The findings are added to the end of the report once (`[findings]` block), so mod authors get them.
