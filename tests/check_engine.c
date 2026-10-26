/* SPDX-License-Identifier: GPL-2.0-or-later
 * White-box check of the grapheme-phoneme rules: isolated accented vowels. */
#include "engine.c"
#include <assert.h>
#include <stdio.h>

static char indexed[NM_CAR_TEX], alpha[NM_CAR_TEX], phon[NM_CAR_TEX];

/* Latin-1 text -> phonemes as letters (code + 65), without word markers. */
static const char *phonemes(const char *latin1) {
    size_t j = 0;
    memset(alpha, 0, sizeof alpha);
    memset(phon, 0, sizeof phon);
    for (const char *p = latin1; *p; p++)
        indexed[j++] = *p;
    indexed[j++] = MARQ_MARQ;
    indexed[j++] = MARQ_INDEX;
    indexed[j] = 0;
    minMajNFois(indexed, alpha);
    graphemePhoneme(alpha, phon);
    j = 0;
    for (const char *p = phon; *p; p++)
        if (*p != INDEX)
            phon[j++] = *p;
    phon[j] = 0;
    return phon;
}
static void expect(const char *latin1, const char *expected) {
    const char *got = phonemes(latin1);
    if (strcmp(got, expected)) {
        fprintf(stderr, "%s: got %s, expected %s\n", latin1, got, expected);
        assert(0);
    }
}

int main(void) {
    s3_init();
    /* Phonemes as letters: e (EU) H, é (EE) I, + (PL) ], "accent" ]ETMF. */
    expect("\xe9", "HH]ETMF]IWC");               /* é: e accent aigu */
    expect("\xc9", "HH]ETMF]IWC");               /* É */
    expect("\xe8", "HH]ETMF]W[EO");              /* è: e accent grave */
    expect("\xea", "HH]ETMF]MD[TGLZITM");        /* ê: e accent circonflexe */
    expect("\xeb", "HH]S[IXE");                  /* ë: e tréma */
    expect("\xe2", "E]ETMF]MD[TGLZITM");         /* â: a accent circonflexe */
    expect("\xf9", "C]ETMF]W[EO");               /* ù: u accent grave */
    expect("\xfa", "C]ETMF]IWC");                /* ú: u accent aigu */
    expect("\xe9, \xe8", "HH]ETMF]IWC\\\\HH]ETMF]W[EO");
    /* Unchanged: words, the preposition à, tréma on other vowels, plain e. */
    expect("\xe0", "E");
    expect("\xe4", "E]S[IXE");                   /* ä: a tréma */
    expect("e", "HH");
    expect("caf\xe9", "TELI");
    expect("\xe9l\xe8ve", "IZIO");
    expect("f\xeate", "LIIS");
    expect("\xe9'", "I]\\TBS\\");
    puts("engine rules OK");
    return 0;
}
