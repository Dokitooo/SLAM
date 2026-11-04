# SLAM SDK 1.0 — making mods for NBA 2K14 with SLAM

A SLAM mod is a normal `.asi` (a 32-bit Windows DLL) that includes one header, `slam.h`, and
exports two functions. Everything else is ordinary C or C++.

```
SLAM\MyMod\MyMod.asi      the mod
SLAM\MyMod\MyMod.ini      its settings     (api->mod_ini)
SLAM\MyMod\MyMod.log      its log          (api->log)
SLAM\MyMod\helper.dll     DLLs it needs    (found here first)
```

Contents:
[Minimal mod](#minimal-mod) · [What happens at start](#what-happens-at-start) ·
[API reference](#api-reference) · [Hooks](#hooks-several-mods-on-one-function) ·
[Patches](#patches-and-conflicts) · [Crash reports](#crash-reports-and-your-mod) ·
[Versions](#versions-why-slam-rarely-updates) · [Building](#building) ·
[Porting an existing mod](#porting-an-existing-mod-bodyhook-crowdtune-nosebleedcam-sigext) ·
[Release checklist](#release-checklist)

---

## Minimal mod

```c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SLAM_SDK_IMPLEMENTATION      /* in exactly ONE .c/.cpp file of the mod */
#include "slam.h"

SLAM_MOD("MyMod", "1.0.0", "your name")    /* name = folder name SLAM\MyMod\ */

static const SLAM_API *api;
static SlamMod *me;

static void SLAM_CALL on_ready(void *user)
{
    api->log(me, "the game's code is ready - install hooks here");
}

SLAM_EXPORT int SLAM_CALL SlamModStart(const SLAM_API *a, SlamMod *self)
{
    api = a; me = self;
    api->on_game_ready(me, on_ready, 0);
    return 0;                        /* anything else = "did not start", logged */
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    if (why == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(inst); SlamSdk_OnAttach(inst); }
    return TRUE;
}
```

Copy `examples\ModTemplate` to start. `examples\HelloSLAM` uses every API function.

**Do nothing in DllMain except `SlamSdk_OnAttach`.** Start your mod in `SlamModStart`.

## What happens at start

1. Windows loads `nba2k14.exe` and SLAM (`dinput8.dll`).
2. Just before the game's own code runs, SLAM loads every `.asi` (see the README for the folders
   and order), then calls `SlamModStart` of each SLAM mod in load order.
3. `nba2k14.exe` is **encrypted on disk** and unpacks itself. When its real start-up code runs,
   SLAM calls every `on_game_ready` callback, on the game's main thread, before the game does
   anything. **Find game code and install game hooks there**, not in `SlamModStart`, where
   the game's code isn't unpacked yet.
4. Right after the mod's DllMain (within about 1 s), the SDK part of `slam.h` checks for SLAM:
   - SLAM there and new enough → nothing (or, if another ASI loader loaded your mod, it asks SLAM
     to start it, so `SlamModStart` still runs);
   - SLAM missing or too old → your mod's name goes on a shared list, and **one** window lists
     all such mods ("SLAM not installed, so these mods won't work: …"). The player closes it and
     the game runs without them. `SlamModStart` is never called, so your mod does nothing.

## API reference

All functions are in the `SLAM_API` table passed to `SlamModStart`. `SlamMod *m` is always your
own `self`. Calling convention: `SLAM_CALL` (`__cdecl`).

| Function | What it does |
|---|---|
| `size`, `apiVersion`, `slamVersion` | the running SLAM: table size, API number, `"1.0.0"` |
| `game_dir()` | folder of `nba2k14.exe`, ends with `\` |
| `mod_dir(m)` | folder of your `.asi`, ends with `\` |
| `mod_ini(m)` | `<mod_dir><name>.ini` — use with `GetPrivateProfile*` |
| `log(m, fmt, ...)` | a line in `SLAM\<name>\<name>.log`. Formats: `%d %u %x %X %s %c %p` (wsprintf) and `%f` (3 decimals) |
| `hook(m, target, detour, &original)` | detour a function; `original` = the rest of the chain. See [Hooks](#hooks-several-mods-on-one-function) |
| `unhook(m, target, detour)` | remove your hook; the other mods' hooks keep working |
| `patch(m, addr, bytes, len)` | write bytes (code or data), registered with you as owner. See [Patches](#patches-and-conflicts) |
| `unpatch(m, addr)` | put the original bytes back (where no other mod's patch covers them) |
| `read(addr, out, len)` | copy memory if all of it is readable: 1 = ok, 0 = not readable |
| `on_game_ready(m, cb, user)` | `cb(user)` once the game's code is unpacked (right away if that already happened) |
| `find_mod(name, verOut, cap)` | 1 = that SLAM mod is running; its version is copied to `verOut` |
| `game_base()` | `nba2k14.exe` image base |
| `exe_timestamp()` | `nba2k14.exe` build (PE TimeDateStamp); the usual build is `0x52202C7F` |
| `game_is_ready()` | 1 after `on_game_ready` callbacks ran |
| `hook_import(m, dll, fn, detour, &original)` | hook a function **the game** calls through its import table (e.g. `"KERNEL32.dll"`, `"CreateFileA"`): only nba2k14.exe's own calls go through it. Chained like `hook`. Use this instead of writing into the import table |
| `unhook_import(m, dll, fn, detour)` | remove it |
| `crash_note(m, text)` | one line (≤ 95 chars) shown in the crash report if the game crashes: what your mod is doing right now. `NULL` clears it |

Results of `hook`/`unhook`/`patch`/`unpatch`: `SLAM_OK` (0), `SLAM_ERR_ARGS`,
`SLAM_ERR_CONFLICT` (another mod got there first, see `SLAM.log`), `SLAM_ERR_MEMORY`
(address not mapped/writable), `SLAM_ERR_NOT_FOUND`, `SLAM_ERR_HOOK` (that address can't be hooked).

SLAM shows its mod list for a few seconds at start (hooks on d3d9's `Direct3DCreate9`,
`CreateDevice`, `Present`) and then removes those hooks. A mod that needs the D3D9 device hooks it
itself with `api->hook` (functions) or writes device vtable entries directly (that memory is
d3d9.dll's, not the game's).

**What `patch` is for:** bytes inside `nba2k14.exe` (code, tables, vtables, import slots). Memory the
game allocates at run time (players, models, the crowd graph...) and your own buffers you write
directly - they come and go, so registering them makes no sense.

## Hooks: several mods on one function

```c
/* a game function using "thiscall" (object in ecx): declare it __fastcall with a dummy edx */
typedef int (__fastcall *LoadBody_t)(void *self, void *edx, int id);
static LoadBody_t LoadBody_next;

static int __fastcall my_LoadBody(void *self, void *edx, int id)
{
    int r;
    api->crash_note(me, "loading a body");
    r = LoadBody_next(self, edx, id);       /* ALWAYS call the rest of the chain */
    api->crash_note(me, 0);
    return r;
}
...
/* 0x0064A0C0 = example address; find yours by pattern scan in on_game_ready */
api->hook(me, (void *)0x0064A0C0, (void *)my_LoadBody, (void **)&LoadBody_next);
```

- If two mods hook the same function, **both work**: the mod loaded last runs first, then calls
  `*original`, which runs the other mod's detour, then the game's function. `SLAM.log` notes the
  chain (`hooked by BodyHook and SigExt - chained, SigExt runs first`).
- Always call `*original` unless you really replace the function. If you skip it, the mods
  behind you in the chain don't run.
- `original` is valid as soon as `hook` returns; set it before the game can call your detour (it
  is, because `hook` fills it first).
- Never write a `jmp` into game code yourself: that overwrites other mods' hooks and SLAM can't
  chain or report it. The same goes for the game's import table: use `hook_import`. If something outside SLAM already hooked a function, SLAM still chains in
  front of it and logs that.

## Patches and conflicts

```c
static const unsigned char nop2[2] = { 0x90, 0x90 };
if (api->patch(me, (void *)0x007F13AF, nop2, 2) != SLAM_OK) { /* another mod got there first */ }
```

- Same bytes as another mod's patch on the same addresses → shared, both fine.
- **Different bytes on the same addresses → refused** (`SLAM_ERR_CONFLICT`). Nothing is written,
  and `SLAM.log` names both mods. The first mod keeps working instead of both breaking.
- A patch over the first 5 bytes of a function another mod hooked is refused too, and so is a
  hook on bytes another mod patched.
- Every 10 s SLAM checks that patches are still there and logs it if something outside SLAM
  overwrote one.
- SLAM keeps the true original bytes, so `unpatch` and crash reports are correct even when
  several mods patched near each other.

## Crash reports and your mod

When the game crashes, SLAM writes `SLAM\crash\crash_<date>_<time>.log` (+ `.dmp`) and opens
the crash window. The report shows:

- the module and offset of the crash (`BodyHook.asi+12A5 [SLAM mod BodyHook 1.19.9]`);
- every mod that hooked or patched code right where it crashed, or in the call stack;
- each mod's `crash_note`, all hooks and patches with their owners, conflicts, memory.

The window's plain-language cause is worked out by `SLAMCrash.exe` from the `[facts]` block at
the top of the report. If your mod is to blame, the player is told to send the report to you.
Help them (and you):

- set `crash_note` around risky work (`"building body for player 23"`);
- use `api->hook` / `api->patch`, never raw writes, so the report knows the code is yours;
- don't install your own unhandled-exception filter. SLAM's handles all crashes; yours would be
  chained behind it at best.

## Versions: why SLAM rarely updates

- `SLAM_ModInfo.apiVersion` is set by `SLAM_MOD` to the `SLAM_API_VERSION` of the `slam.h` you
  built with (or to `SLAM_MOD_NEEDS_API`, see below). A SLAM with a lower API refuses to start the mod (logged) and the mod's SDK part
  shows the "SLAM is too old" window.
- `SLAM_API` **only grows at the end**. Nothing is ever removed or changed. A mod built for API 1
  works on every later SLAM 1.x.
- To *use* a newer function but still run on older SLAMs, build with the newer `slam.h`, say
  which API you really need, and check before calling:

  ```c
  #define SLAM_MOD_NEEDS_API 1          /* before #include "slam.h" */
  ...
  if (SLAM_HAS(api, some_new_function)) api->some_new_function(...);
  ```
- A new SLAM is only needed when mods need a new *SLAM feature*. Fixes and features in mods
  never need a new SLAM.

## Building

The mod needs only `slam.h` and kernel32. No C runtime is required (but you may use one).

**Visual Studio** (x86 Native Tools Command Prompt):

```bat
cl /O2 /GS- /I path\to\sdk /LD MyMod.c /Fe:MyMod.asi /link kernel32.lib
```

or `examples\build_msvc.bat`.

**Linux, clang-cl + lld-link** (how SLAM itself is built): see `examples/build_linux.sh`.

**MinGW gcc** (not tested, should work):
`i686-w64-mingw32-gcc -O2 -shared -I sdk -o MyMod.asi MyMod.c`

The build must be **32-bit**. Name it `.asi`. Put it in `SLAM\<name>\` where `<name>` is the
name in `SLAM_MOD`.

## Porting an existing mod (BodyHook, CrowdTune, NosebleedCam, SigExt)

What moves into SLAM and can be **deleted from the mod**:

| In the mod today | With SLAM |
|---|---|
| its own `dinput8.dll` proxy (`DirectInput8Create` forwarding, `dinput8.def`) | gone: SLAM is the `dinput8.dll`. Build only the `.asi`. |
| its own ASI/mod loader (BodyHook `modloader.inc`, `ml_install`, the `NBA2K14_asi_folder_` mutex) | gone: SLAM loads all mods |
| its own crash logger (BodyHook `crash.inc`: `crash_init`, `crash_keep_filter`, memory lines, `BodyHook_crash.dmp`) | gone: SLAM reports all crashes. Use `crash_note` instead |
| "one copy per game" passive guard against dinput8 + .asi double load | gone: SLAM loads each file once |
| waiting for the exe to unpack (polling threads, timeouts) | `api->on_game_ready` |
| `VirtualProtect` + write `E9 …` jumps | `api->hook` |
| `VirtualProtect` + write bytes | `api->patch` |
| `<Mod>.log`/`.ini` next to `nba2k14.exe` | `SLAM\<Mod>\` (`api->mod_dir`, `api->mod_ini`, `api->log`). You can keep your own log file in `mod_dir` if you prefer |

Steps:

1. Add `#define SLAM_SDK_IMPLEMENTATION` + `#include "slam.h"` and `SLAM_MOD("BodyHook", "1.19.9", "Alexiconn2K")`.
2. Move start-up from DllMain into `SlamModStart`, and game patching into an `on_game_ready`
   callback (or keep your own "wait until the code is unpacked" thread, started from
   `SlamModStart`). DllMain keeps only `SlamSdk_OnAttach(inst)`.
3. Replace each code write with `api->hook` / `api->patch`, import-table writes with
   `api->hook_import`. Check the result: on `SLAM_ERR_CONFLICT`, log it and switch that feature
   off instead of crashing.
4. Delete the proxy, loader and crash logger (table above).
5. Mods that need each other: `api->find_mod("BodyHook", ver, sizeof ver)`.
6. Ship one folder the player drags into `SLAM\`: `<Mod>\<Mod>.asi` + `<Mod>.ini` (+ its files). In the
   README: "needs SLAM 1.0 or newer". The shipped .ini is only a default, so don't let it cost the player
   his settings: on the first start take his old .ini from where it was (scripts\ ...), and after an update
   (the new download's .ini replaced his) put his own back. The ported mods do this with a marker line
   `[SLAM] InUse=1` in the .ini and a `<Mod>.ini.backup` (`glue_migrate` in their `slam_glue.c` - copy it).

**Tip for mods built without `<windows.h>`** (a minimal Win32 header of their own): put the SLAM side
in a separate small file that does include `<windows.h>` - `SLAM_MOD`, `DllMain`, `SlamModStart` and
`SLAM_SDK_IMPLEMENTATION` - and have it call one `mod_start()` function in the mod's own code. The
mod's code then only needs `#include "slam.h"` (that part needs no Windows header). BodyHook,
CrowdTune, SigExt and NosebleedCam were ported this way (`slam_glue.c` in their source).

How the four mods were ported (their SLAM editions):
- **BodyHook (1.19.9 and 1.11.21-ML):** dropped `modloader.inc`, `crash.inc`, the dinput8 proxy and
  the one-copy guard. Its single write function `patch_bytes` now calls `api->patch` for every address
  inside nba2k14.exe; `hook_exe_import` (hair styles: `CreateFileA`) became `api->hook_import`. Its own
  files (`BodyHook.ini`, `_Hair.ini`, `_Shorts.ini`, `_cache.ini`, log, png cache) live in
  `SLAM\BodyHook\`; the game's `png*.iff` stay in the game folder.
- **SigExt 1.8.0:** dropped its winhttp/xinput proxies and `asi\` loader; ~120 code spots, the crash
  guard, deep-three table and half-court changes via `api->patch`; its 4 file hooks via
  `api->hook_import` (so BodyHook and SigExt both see `CreateFileA`).
- **CrowdTune 1.5.1:** dropped its proxies and `asi\` loader; 4 call sites, 1 operand and the
  start-group value via `api->patch`.
- **NosebleedCam 1.1:** its two camera tables via `api->patch` (re-applied the same way if the game
  resets them).

## Release checklist

- [ ] 32-bit `.asi`, exports `SlamModInfo` and `SlamModStart` (`dumpbin /exports MyMod.asi`)
- [ ] `SLAM_MOD` name = folder name; version bumped
- [ ] DllMain only calls `SlamSdk_OnAttach`
- [ ] game hooks in `on_game_ready` (or after your own "code is unpacked" check); every `hook`/`patch` result checked
- [ ] tested: with SLAM (mod listed in `SLAM.log`, no `CONFLICT`), without SLAM (one "not installed"
      window, game starts)
- [ ] README says "needs SLAM 1.0 or newer" and where the files go

---

## Building SLAM itself

`source\build\build_linux.sh` (LLVM 18 + MinGW-w64 headers) builds `dinput8.dll`, `SLAM.asi`,
`SLAMCrash.exe` and the examples, runs the crash-cause tests (`tools/test_cause`) and checks the
files (`tools/check_pe.py`). `source\build\build_msvc.bat` builds SLAM with Visual Studio.

Source map: `slam.c` (start, settings, dinput8 proxy) · `loader.inc` (finding/loading/starting mods,
game-ready) · `hooks.inc` (hook chains, patch registry, conflicts) · `api.inc` (`SLAM_API`) ·
`crash.inc` (crash reports) · `crashui\` (`SLAMCrash.exe`, cause rules in `cause.inc`) ·
`log.inc`, `util.inc`. Hook engine: MinHook (`third_party\minhook`, unmodified).
