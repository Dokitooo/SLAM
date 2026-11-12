# SLAM changelog

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
