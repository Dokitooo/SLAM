/* Linux test of cause.inc: gcc t_cause.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/crashui/cause.inc"
static int fails;
static void check(const char *name, const char *facts, const char *expectHead, const char *expectBlame)
{
    CauseFacts fx; CauseResult r; char text[4000];
    snprintf(text, sizeof text, "[facts]\r\n%s[/facts]\r\n\r\nHuman part\r\nmodule=WRONG\r\n", facts);
    cause_parse(text, &fx); cause_guess(&fx, &r);
    int ok = strstr(r.headline, expectHead) && strcmp(r.blame, expectBlame) == 0;
    if (!ok) fails++;
    printf("%s %-14s sure=%d blame=[%s]\n   %s\n", ok ? "PASS" : "FAIL", name, r.sure, r.blame, r.headline);
    if (!ok || getenv("V")) printf("%s\n", r.body);
}
#define BASE "code=C0000005\nkind=access violation\neip_in_module=1\nexe_ts=52202C7F\ngame_ready=1\nmem_largest_free_mb=700\nmem_total_mb=4096\n"
int main(void)
{
    check("test", "test=1\n", "only a test", "");
    check("test-close", "test=2\n", "test crash", "");
    check("oom", BASE "module=nba2k14.exe\nmem_largest_free_mb=20\nstack_mods=BodyHook\n", "out of memory", "");
    check("oom-2gb", "code=C0000017\nmodule=nba2k14.exe\nmem_total_mb=2048\nmem_largest_free_mb=40\n", "out of memory", "");
    check("in-mod", BASE "module=CrowdTune.asi\nmodule_mod=CrowdTune\nmodule_is_slam_mod=1\nnotes=CrowdTune: loading crowd for arena 5 | BodyHook: idle\n", "inside the mod \"CrowdTune\".", "CrowdTune");
    check("in-plain", BASE "module=Foo.asi\nmodule_mod=Foo\nmodule_is_slam_mod=0\n", "plain .asi", "Foo");
    check("hooked-here", BASE "module=nba2k14.exe\nhooked_near_eip=BodyHook,SigExt\n", "BodyHook and SigExt changed", "BodyHook,SigExt");
    check("slam", BASE "module=dinput8.dll\nmodule_is_slam=1\nstack_mods=NoseBleed\n", "SLAM's own code", "NoseBleed");
    check("nowhere", BASE "module=\neip_in_module=0\nstack_mods=SigExt\n", "doesn't exist", "SigExt");
    check("gpu", BASE "module=nvd3dum.dll\n", "graphics driver", "");
    check("overlay", BASE "module=RTSSHooks.dll\n", "overlay", "");
    check("stack", BASE "module=nba2k14.exe\nstack_mods=BodyHook\nnotes=BodyHook: building body for player 23\n", "BodyHook was involved", "BodyHook");
    check("nearstack", BASE "module=nba2k14.exe\nhooked_near_stack=CrowdTune\n", "CrowdTune was involved", "CrowdTune");
    check("badexe", BASE "module=nba2k14.exe\nexe_ts=12345678\n", "not the usual version", "");
    check("startup", BASE "module=nba2k14.exe\ngame_ready=0\n", "while starting", "");
    check("null", BASE "module=nba2k14.exe\naccess=read\ntarget=00000010\n", "wasn't there", "");
    check("plain-game", BASE "module=nba2k14.exe\naccess=read\ntarget=1234ABCD\nconflicts=2\nearlier_exceptions=3\n", "no mod was directly", "");
    check("stackovf", "code=C00000FD\nkind=stack overflow\neip_in_module=1\nmodule=ntdll.dll\nexe_ts=52202C7F\n", "out of stack", "");
    check("cpp", "code=E06D7363\neip_in_module=1\nmodule=KERNELBASE.dll\nexe_ts=52202C7F\n", "not handled", "");
    check("empty", "", "no clear cause", "");
    printf("\nFAILURES %d\n", fails);
    return fails != 0;
}
