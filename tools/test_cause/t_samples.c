#include <stdio.h>
#include <stdlib.h>
#include "../../src/crashui/cause.inc"
int main(int c, char **v) { for (int i = 1; i < c; i++) { FILE *f = fopen(v[i], "rb"); static char t[100000]; size_t n = fread(t, 1, sizeof t - 1, f); t[n] = 0; fclose(f);
  CauseFacts fx; CauseResult r; cause_parse(t, &fx); cause_guess(&fx, &r); printf("== %s\nsure=%d blame=%s\n%s\n\n%s\n", v[i], r.sure, r.blame, r.headline, r.body); } }
