# SLAM changelog

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
