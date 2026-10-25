/* Build with -fsigned-char: tables and phoneme sentinels use signed bytes. */
/*
 * Synthé 3 - A speech synthetizer software for french
 *
 * Copyright (C) 1985-2015 by Michel MOREL <michel.morel@unicaen.fr>.
 *
 * Synthé 3 comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: https://github.com/acceslibreinfo/synthe3
 *
 * This software is maintained by Shérab <Sebastien.Hinderer@ens-lyon.org>.
 */
/* C port: historical phonetics and diphone arithmetic are intentionally retained.
 * One engine instance, owned by the synchronous Speech Dispatcher loop. */
#include "engine.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NM_CAR_TEX 10000
#define NM_CAR_TEX_1 (NM_CAR_TEX - 1)
#define NM_CAR_TEX_2 (NM_CAR_TEX - 2)
#define NM_CAR_TEX_8 (NM_CAR_TEX - 8)
#define MARQ_MARQ ((char)248)
#define MARQ_INDEX ((char)237)
#define HAUTEUR_REF 6
#define DEBIT_REF 3
#define VOLUME_REF 10
#define PI 3.14159
#define FREQ_ECH 10000
#define FREQ_COUPURE 400
#define MUL_AMP 8
#include "voice_data.h"
static int engine_rate, engine_pitch;
/* Optional contours use timings from an unmodulated first pass. */
static struct { size_t end; int ending; } contours[241];
static size_t contour_count, contour_index;
static double contour_strength, contour_pitch;
static int contour_active;
static int (*poll_stop)(void);
static int16_t *pcm;
static size_t pcm_size, pcm_capacity;
static int failed;
/* Existing automatic word indices are silent; retain their PCM positions for
 * server bookmarks without changing the phonetic input or diphone arithmetic. */
static size_t word_bytes[241], word_samples[241], word_count, word_seen;
static long echE_Prec, echS_Prec;
static short readShort(const char *p) {
    return (short)((unsigned char)p[0] | ((unsigned char)p[1] << 8));
}
#define BVRD 1 // teste le mode bavard
#define NABR 2 // teste le mode non abréviations
#define VOYM 3 // voyelle "mouillée"
#define VOY 4  // voyelle
#define LET 5  // lettre
#define CNS 5  // lettre
#define PONC 6 // ponctuation
#define SYMB 7 // symbole
#define CHIF 8 // chiffre
#define TTT 8  // tout
#define PG 80  // programme (retour au cas général)
#define RB 81  // arbre
#define DC 82  // dictionnaire
#define VOYP 0 // voyelles
#define GLIP 1 // glissantes
#define CNSP 2 // consonnes
#define OU 0
#define WW 0
#define OO 1
#define UU 2
#define II 3
#define AA 4
#define AN 5
#define ON 6
#define EU 7
#define EE 8
#define UN 9
#define YY 10
#define FF 11
#define SS 12
#define HH 13
#define VV 14
#define ZZ 15
#define JJ 16
#define PP 17
#define TT 18
#define KK 19
#define BB 20
#define DD 21
#define GG 22
#define MM 23
#define NN 24
#define LL 25
#define RR 26
#define VR 27 // virgule phon
#define PL 28 // allongement
#define INDEX '_'
#define NULP -2 // nul phon

static char *tTab;

static short aTWin;
static short aTCat;
static short aTPhon1;
static short aTPhon2;
static short aTCatP1;
static short aTCatP2;
static short aChLst1;
static short aChLst2;
static short aChLst3;

static short aCh01;
static short aCh1S;
static short aCh100;
static short aCh100Z;
static short aTabVol;
static short aTabVit;
static short aTabHau;
static short aCateg;
static short aFinAmp;
static short aFinTim;
static short aTraAmp;
static short aTraTim;
static short aDebAmp;
static short aDebTim;
static short aTabAmp;
static short aAdAmp;
static short aTabDeb;
static short a_Huit1;
static short a_Dix1;
static short aXChif;
static short lgTab;
static char *texteLec;
static char *texteEcr;
static short iLec;
static short iEcr;
static unsigned char carac;
static unsigned char categ;
static short ctBava;
static char liaison;
static char *ptArbre;
static char nombre[12];

typedef struct Marq {
    bool DSDecroit; // début sous-diphone décroit
    bool DSCroit;   // début sous-diphone croit
    bool FSDecroit; // fin sous-diphone décroit
    bool FSCroit;   // fin sous-diphone croit
} Marq;

typedef struct UChar {
    unsigned char DSDecroit; // début sous-diphone décroit
    unsigned char DSCroit;   // début sous-diphone croit
    unsigned char FSDecroit; // fin sous-diphone décroit
    unsigned char FSCroit;   // fin sous-diphone croit
} UChar;

typedef struct PUChar {
    unsigned char *DSDecroit; // début sous-diphone décroit
    unsigned char *DSCroit;   // début sous-diphone croit
    unsigned char *FSDecroit; // fin sous-diphone décroit
    unsigned char *FSCroit;   // fin sous-diphone croit
} PUChar;

typedef struct PSChar {
    char *DSDecroit; // début sous-diphone décroit
    char *DSCroit;   // début sous-diphone croit
    char *FSDecroit; // fin sous-diphone décroit
    char *FSCroit;   // fin sous-diphone croit
} PSChar;

static float mulHauteur;
static float mulDebit;
static float mulVolume;
static char *texPhon;
static float xEcrEchelleDeLec;
static float allonge;
static long iLecPhon;
static long iLecPhonDecroit;
static long iLecPhonCroit;
static char phonGDecroit;
static char phonGCroit;
static char phonDDecroit;
static char phonDCroit;
static char phonMem;
static char catGDecroit;
static char catGCroit;
static char catDDecroit;
static char catDCroit;
static char nSousDiphDecroit;
static char nSousDiphCroit;
static Marq marq;
static UChar nSeg;
static UChar nSegIni;
static PSChar ptSeg;
static UChar perio;
static float perioBase;
static unsigned char perioResult;
static PUChar ptAmp;
static UChar amp;
static UChar ampAnc;
static short iEch;
static unsigned char FOIS;
static unsigned char carSuiv(void);
static char *voice_segment(unsigned char n) {
    return (char *)voice_segments + readShort((const char *)voice_addresses + n * 2);
}
static char tab_tCat(unsigned char c);
static char *tab_chListUnit(char c);
static char *tab_chListDiz(char c);
static char *tab_chListMil(unsigned short int n);
static char *tab_ch01();
static char *tab_ch1S();
static char *tab_ch100();
static char *tab_ch100Z();
static short tab_tabVol(char n);
static short tab_tabVit(char n);
static short tab_tabHau(char n);
static char tab_categ(unsigned short int phon);
static char *tab_getPtTabDeb(unsigned char c);
static char *tab_getPtArbre(char *ptArbre);
static char *tab_getPtArbreRoot(char *ptArbre, unsigned char carac);
static char *tab__Huit1();
static char *tab__Dix1();
static char *tab_XChif();
static void minMajNFois(char *texteAlphaLec, char *texteAlphaEcr);
static void graphemePhoneme(char *texte, char *texPhon);
static void traiteNombre();
static void finNombre(short unsigned int *iN);
static void phonChif(char *ptChif);
static void ecritPhon(char c);
static void ecrit(char c);
static void traiteTextePhonetique(char *chainePhon);
static bool traiteUnePeriode();
static void initDecroitSurCroit();
static bool perioSuiv(bool *marqDS, bool *marqFS, long *iLecPhonX, char *phonG, char *phonD,
                      char *catG, char *catD, char *nSousDiph, unsigned char *nSegIniDS,
                      unsigned char *nSegIniFS, unsigned char *nSegDS, unsigned char *nSegFS,
                      char **ptSegDS, char **ptSegFS, unsigned char *perioDS,
                      unsigned char *perioFS, unsigned char **ptAmpDS, unsigned char **ptAmpFS,
                      unsigned char *ampDS, unsigned char *ampFS, bool mCroit);
static bool nouveauSousDiph(long *iLecPhonX, char *phonG, char *phonD, char *catG, char *catD,
                            char *nSousDiph, unsigned char *nSegIniDS, unsigned char *nSegDS,
                            unsigned char **ptAmpDS, unsigned char *ampDS);
static char phonSuiv(long *iLecPhonX, char phonG);
static short calculeEchPerioDecroit(short x, char *ptSegDecroit, unsigned char perioDecroit,
                                    unsigned char ampDecroit);
static short calculeEchPerioCroit(short x, char *ptSegCroit, unsigned char perioCroit,
                                  unsigned char ampCroit);
static float fenCroit(float x);
static float fenDecroit(float x);
static unsigned char tab_tWin(unsigned char carac) {
    if (carac < 32)
        carac = 32;
    if (carac >= 'a' && carac <= 'z')
        carac -= 32;
    if (carac < 123)
        return carac;
    return ((unsigned char *)tTab + aTWin)[carac - 123];
}

static char tab_tCat(unsigned char c) {
    if (c < 32 || c > FOIS)
        return PONC;
    return (tTab + aTCat)[c - 32];
}

static char *tab_chListUnit(char c) { return tTab + readShort(tTab + aChLst1 + 2 * (c - 48)); }

static char *tab_chListDiz(char c) { return tTab + readShort(tTab + aChLst2 + 2 * (c - 49)); }

static char *tab_chListMil(unsigned short int n) {
    return tTab + readShort(tTab + aChLst3 + 2 * (n));
}

static char *tab_ch01() { return tTab + aCh01; }

static char *tab_ch1S() { return tTab + aCh1S; }

static char *tab_ch100() { return tTab + aCh100; }

static char *tab_ch100Z() { return tTab + aCh100Z; }

static short tab_tabVol(char n) { return readShort(tTab + aTabVol + 2 * (n + 10)); }

static short tab_tabVit(char n) { return readShort(tTab + aTabVit + 2 * (n + 3)); }

static short tab_tabHau(char n) { return readShort(tTab + aTabHau + 2 * (n + 6)); }

static char tab_categ(unsigned short int phon) { return (tTab + aCateg)[phon]; }

static unsigned char tab_finAmp(char catG, char catD) { return (tTab + aFinAmp)[catG * 10 + catD]; }

static unsigned char tab_finTim(unsigned short int phon) {
    return ((unsigned char *)tTab + aFinTim)[phon];
}

static unsigned char tab_traAmp(char catG, char catD) { return (tTab + aTraAmp)[catG * 10 + catD]; }

static unsigned char tab_traTim(char phonG, char phonD) {
    return ((unsigned char *)tTab + aTraTim)[phonG * 32 + phonD];
}

static unsigned char tab_debAmp(char catG, char catD) { return (tTab + aDebAmp)[catG * 10 + catD]; }

static unsigned char tab_debTim(unsigned short int phon) {
    return ((unsigned char *)tTab + aDebTim)[phon];
}

static unsigned char *tab_getPtAmp(short nAmp) {
    short adr = readShort(tTab + aAdAmp + 2 * (nAmp));
    return (unsigned char *)(tTab + aTabAmp + adr);
}

static char *tab_getPtTabDeb(unsigned char c) {
    short adr = readShort(tTab + aTabDeb + 2 * (c - 32));
    return tTab + adr;
}

static char *tab_getPtArbre(char *ptArbre) {
    short adr = readShort(ptArbre);
    return tTab + adr;
}

static char *tab_getPtArbreRoot(char *ptArbre, unsigned char carac) {
    short adr = readShort(ptArbre);
    adr = readShort(tTab + adr + 2 * (carac - 32));
    return tTab + adr;
}

static char *tab__Huit1() { return tTab + a_Huit1; }

static char *tab__Dix1() { return tTab + a_Dix1; }

static char *tab_XChif() { return tTab + aXChif; }

static void minMajNFois(char *texteAlphaLec, char *texteAlphaEcr) {
    short nbFois;
    char chaine[8]; // pour nbFois
    short iLecCherche;

    texteLec = texteAlphaLec;
    texteEcr = texteAlphaEcr;
    iLec = 0;
    iEcr = 0;
    while (true) {
        carac = (unsigned char)texteLec[iLec];
        if (carac)
            iLec++;
        if ((char)carac == MARQ_MARQ) {
            if (texteLec[iLec] == MARQ_INDEX) {
                if (iEcr < NM_CAR_TEX_2) {
                    texteEcr[iEcr++] = MARQ_MARQ;
                    texteEcr[iEcr++] = MARQ_INDEX;
                }
                iLec++;
                continue;
            }
        }
        if (carac == 0)
            break;
        if ((carac < 48 && carac > 32) || carac > 57) { // pas les chiffres ni esp ni tab
            if (texteLec[iLec] == carac) {
                if (texteLec[iLec + 1] == carac) {
                    nbFois = 3; // déjà trois fois
                    for (iLecCherche = iLec + 2; texteLec[iLecCherche] == carac; iLecCherche++)
                        nbFois++;
                    if (iEcr < NM_CAR_TEX_8) {
                        sprintf(chaine, "%d", nbFois);
                        texteEcr[iEcr] = 0; // pour concaténer
                        strcat(texteEcr, chaine);
                        iEcr += strlen(chaine);
                        texteEcr[iEcr++] = FOIS;
                        texteEcr[iEcr++] = (char)tab_tWin(carac);
                        texteEcr[iEcr++] = ' ';
                        iLec = iLecCherche;
                        continue;
                    }
                }
            }
        }
        carac = tab_tWin(carac);
        ecrit(carac);
    }
    texteEcr[iEcr] = 0;
}

static void graphemePhoneme(char *texte, char *texPhon) {
    char cArbre;
    unsigned char caracAnc = 0;
    unsigned char categAnc = 0;
    char *ptArbreAnc = NULL;
    short iLecAnc = 0;
    short iEcrAnc = 0;
    bool sigle = false; // pas de sigle au départ
    short bavard = 0;
    short abrev = 1 - 0;

    texteLec = texte;
    texteEcr = texPhon;
    ctBava = 0;  // pas de bavard provisoire
    liaison = 0; // normal
    iLec = 0;
    iEcr = 0;
    ptArbre = tab_getPtTabDeb(' '); // comme si on était derrière un espace
    if (!carSuiv()) {
        texPhon[iEcr] = 0;
        return;
    }
    while (true) { // traite caractère par caractère
        if (poll_stop && poll_stop()) {
            texPhon[iEcr] = 0;
            return;
        }
        while (true) { // cherche la règle qui s'applique
            cArbre = *ptArbre++;
            if ((unsigned char)cArbre == carac) {
                iLecAnc = iLec;
                iEcrAnc = iEcr;
                ptArbreAnc = ptArbre;
                caracAnc = carac;
                categAnc = categ; // garde anciennes valeurs pour dico non
                carSuiv();
                break; // caractère trouvé
            }
            if (cArbre <= TTT && cArbre >= categ)
                break; // catégorie trouvée
            if (cArbre == BVRD && bavard)
                break;
            if (cArbre == NABR && !abrev)
                break;
            while (true) { // règle non validée, passer à la suivante
                cArbre = *ptArbre++;
                if (cArbre >= PG)
                    break;
            }
            ptArbre += 2;
        }
        while (true) {     // traite la règle validée et prépare l'adresse suivante
            while (true) { // lit les phonèmes et les écrit
                cArbre = *ptArbre++;
                if (cArbre >= PG)
                    break;
                ecritPhon(cArbre);
            }
            ptArbre = tab_getPtArbre(ptArbre); // prépare adresse suivante
            if (cArbre == RB) {                // arbre : branche
                break;
            } else if (cArbre == PG) { // arbre : racine
                if (*ptArbre > 0)
                    liaison = *ptArbre;
                ptArbre++;
                if (carac == 0) { // fin de l'énoncé
                    liaison = -1;
                    break;
                }
                if (categ == CHIF)
                    traiteNombre();
                else {
                    ptArbre = tab_getPtArbreRoot(ptArbre, carac); // racine
                    if (ptArbre == tab_getPtTabDeb(carac)) {      // début de mot : cherche si sigle
                        if (categ == CNS) {
                            iLecAnc = iLec;
                            iEcrAnc = iEcr;
                            while (true) {
                                carSuiv();
                                if (categ != CNS) {
                                    if (categ == VOY || categ == VOYM)
                                        sigle = false;
                                    else
                                        sigle = true;
                                    break;
                                }
                            }
                            iLec = iLecAnc;
                            iEcr = iEcrAnc;
                        }
                    }
                    carSuiv();
                    if (sigle) {
                        if (categ == LET)
                            categ = PONC;
                        else
                            sigle = false;
                    }
                }
                break;
            } else if (cArbre == DC) {  // dictionnaire
                while (*ptArbre != 0) { // vérifie si la chaine correspond
                    if ((unsigned char)*ptArbre != carac)
                        break;
                    ptArbre++;
                    carSuiv();
                }
                if (*ptArbre == 0) {
                    ptArbre++;
                    sigle = false;
                    continue; // le mot est dans le dico
                } else {      // dico non, restaure les anciennes valeurs
                    carac = caracAnc;
                    categ = categAnc;
                    ptArbre = ptArbreAnc + 3; // début règle suivante
                    iLec = iLecAnc;
                    iEcr = iEcrAnc;
                    break;
                }
            }
        }
        if (liaison < 0)
            break;
    }
    texPhon[iEcr] = 0;
}

static void traiteNombre() {
    unsigned short int nbC = 0;   // nombre de chiffres dans le dernier groupe de 3 chiffres (1 à 3)
    unsigned short int nbGr3 = 0; // nombre de groupes de 3 chiffres (1 à 3)
    unsigned short int iN = 0;    // indice chiffre du nombre
    unsigned short int groupe = 0;    // compte les chiffres dans un groupe continu
    bool petitGroupeInterdit = false; // groupe <3 chiffres interdit (sauf au début)
    short iLecPrec = 0;
    short iEcrPrec = 0;
    short iLecAnc = 0;
    short iEcrAnc = 0;
    unsigned short int nbCAnc = 0;
    unsigned short int nbGr3Anc = 0;
    char c;  // chiffre
    char c1; // chiffre
    char c2; // chiffre

    while (
        true) { // Fabrique un nombre de 12 chiffres max, en enlevant les séparateurs en mode compta
        /* A space may introduce a thousands group, never a punctuation mark
         * or a letter. Speech Dispatcher's symbol pass can insert "5 ,58". */
        if (categ != CHIF) {
            iLec = iLecPrec;
            iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
            break;           // et arrête le nombre
        }
        if (nbC == 3) {
            if (nbGr3 == 3) {              // dépasse la taille max du nombre
                if (petitGroupeInterdit) { // il y a déjà eu au moins un séparateur (mode compta)
                    nbGr3 = nbGr3Anc;
                    nbC = nbCAnc;
                    iLec = iLecAnc;
                    iEcr = iEcrAnc; // reprend les valeurs anciennes
                    break;          // et arrête le nombre
                }
                iLec = iLecPrec;
                iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                break;           // et arrête le nombre
            } else {
                nbGr3++;
                nbC = 1;
            }
        } else
            nbC++;
        nombre[iN++] = carac;
        groupe++;
        iLecPrec = iLec;
        iEcrPrec = iEcr; // sauvegarde
        carSuiv();
        if (1) {
            if (carac == ' ') {                // séparateur
                if (groupe != 3) {             // pas groupe 3 chiffres
                    if (petitGroupeInterdit) { // c'est interdit
                        nbGr3 = nbGr3Anc;
                        nbC = nbCAnc;
                        iLec = iLecAnc;
                        iEcr = iEcrAnc;      // reprend les valeurs anciennes
                        break;               // et arrête le nombre
                    } else if (groupe > 3) { // groupe>3
                        iLec = iLecPrec;
                        iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                        break;           // et arrête le nombre
                    }
                }
                groupe = 0;
                petitGroupeInterdit = true; // maintenant, groupe<3 chiffres interdit
                nbGr3Anc = nbGr3;
                nbCAnc = nbC;
                iLecAnc = iLecPrec;
                iEcrAnc = iEcrPrec; // sauve les valeurs actuelles (avant carSuiv précédent)
                carSuiv();
            } else if (categ != CHIF) {                    // fin obligée
                if (groupe == 3 || !petitGroupeInterdit) { // groupe OK
                    iLec = iLecPrec;
                    iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                    break;           // et arrête le nombre
                } else {             // interdit
                    nbGr3 = nbGr3Anc;
                    nbC = nbCAnc;
                    iLec = iLecAnc;
                    iEcr = iEcrAnc; // reprend les valeurs anciennes
                    break;          // et arrête le nombre
                }
            }
        }
    }
    iN = 0;
    while (nombre[iN] == '0') { // nombre commençant par zéro
        phonChif(tab_ch01());   //"zéro"
        iN++;
        nbC--;
        if (nbC == 0) {
            nbC = 3;
            if (nbGr3 == 0) {
                finNombre(&(iN));
                return;
            } else {
                nbGr3--;
            }
        }
    }
    while (true) {
        c = nombre[iN++]; // nbC=1, 2, 3
        switch (nbC) {
        case 3: //... nnn ...
            nbC--;
            if (c == '0')
                continue;              //... 0nn ...
            else if (c == '1')         //... 1nn ...
                phonChif(tab_ch100()); //"sâ[t]"
            else {
                phonChif(tab_chListUnit(c)); //"de:", "trwa:", "kat", ...
                phonChif(tab_ch100Z());      //"sâ:[z]"
            }
            break;
        case 2: //...nn ...
            nbC--;
            if (c == '0')
                continue;                          //...0n ...
            else if (c == '1' && nombre[iN] < '7') //...10 ... à ...16 ...
                break;
            else {
                phonChif(tab_chListDiz(c)); //"diz", "vî:[t]", "trât", "karâ:t", ...
                if (c == '2') {             //...2n ...
                    if (nombre[iN] > '0')   //...21 ... à ...29 ...
                        ecritPhon(TT);
                } else if ((c == '7' || c == '9') && nombre[iN] > '6')
                    phonChif(tab_chListDiz('1'));
            }
            break;
        case 1: //...n ...
            nbC--;
            c1 = 0;
            c2 = 0;
            if (iN > 1)
                c1 = nombre[iN - 2];
            if (iN > 2)
                c2 = nombre[iN - 3];
            if (c == '1') {
                if (c1 > '1' && c1 < '8') { //...21 ... à ...71 ...
                    ecritPhon(EE);
                    ecritPhon(PL);    //"é:"
                } else if (c1 == '8') //...81 ...
                    ecritPhon(VR);    //","
            }
            if (c < '7' &&
                (c1 == '1' || c1 == '7' ||
                 c1 == '9')) //...10 ... à ...16 ..., ...70 ... à ...76 ..., ...90 ... à ...96 ...
                phonChif(tab_chListUnit(c + 10)); //"di", "ô:z", "dùz", ...
            else if (iN == 1 ||
                     (c1 == '0' && c2 == '0')) { // n, n nnn, n nnn nnn, n nnn nnn nnn, ... 00n ...
                if (c == '1') {                  // 1, 1 nnn, 1 nnn nnn, 1 nnn nnn nnn, ... 001 ...
                    if (nbGr3 != 1)
                        phonChif(tab_ch1S()); //"î:"
                } else
                    phonChif(tab_chListUnit(c)); //"de:", "trwa:", "kat", ...
            } else
                phonChif(tab_chListUnit(c)); //"î", "de:", "trwa:", "kat", ...
            if (nbGr3 == 0) {
                finNombre(&(iN));
                return;
            } else { //... nnn
                nbGr3--;
                nbC = 3;
                if (c != '0' || c1 != '0' || c2 != '0') // sauf ... 000 ...
                    phonChif(tab_chListMil(nbGr3));     //"mil", "milyô", "milya::r"
            }
        }
    }
}

static void finNombre(short unsigned int *iN) {
    char c;
    char c1;

    c = nombre[--(*iN)];
    if (c == '8')
        ptArbre = tab__Huit1();
    else {
        c1 = 0;
        if ((*iN) > 0)
            c1 = nombre[(*iN) - 1];
        if ((c1 == '0' || (c1 > '1' && c1 < '7') || c1 == '8' || c1 == 0) && c == '5')
            ecritPhon(KK);
        if (((c1 == '1' || c1 == '7' || c1 == '9') && c == '0') ||
            (c1 != '1' && c1 != '7' && c1 != '9' && c == '6'))
            ptArbre = tab__Dix1();
        else
            ptArbre = tab_XChif();
    }
    carSuiv();
}

static void phonChif(char *ptChif) {
    char c;

    if (*ptChif == 80)
        return; //...0
    while (true) {
        c = *ptChif++;
        if (c == 80)
            break;
        ecritPhon(c);
    }
    liaison = *ptChif;
}

static unsigned char carSuiv(void) {
    while (true) {
        carac = (unsigned char)texteLec[iLec];
        if (carac)
            iLec++;
        if ((char)carac == MARQ_MARQ) {
            if (texteLec[iLec] == MARQ_INDEX) {
                ecrit(INDEX);
                iLec++;
                continue;
            }
        }
        break;
    }
    categ = tab_tCat(carac);
    return carac;
}

static void ecritPhon(char c) {
    if (liaison > 0) {
        if ((liaison < 10 && c > 9) || (liaison > 9 && c < 10)) { // liaison possible
            ecrit(liaison + 65); // évite 0 et rend visibles les caractères
        }
    }
    ecrit(c + 65); // évite 0 et rend visibles les caractères
    liaison = 0;
}

static void ecrit(char c) {
    if (iEcr < NM_CAR_TEX_1)
        texteEcr[iEcr++] = c;
}

static void traiteTextePhonetique(char *chainePhon) {

    echE_Prec = echS_Prec = 32768; // init car utilisés dès le 1er échantillon

    mulHauteur = (float)tab_tabHau(engine_pitch - HAUTEUR_REF) / 1000;
    mulDebit = (float)tab_tabVit(engine_rate - DEBIT_REF) / 1000;
    mulVolume = (float)tab_tabVol(10 - VOLUME_REF) / 1000;
    texPhon = chainePhon;
    if (!strcmp(texPhon, "IXIXIZTEESI_RI"))
        strcpy(texPhon, "XDNIZXB[IZ\\IZITS[IZ\\[HR[BVCTMKG]J]SI[VDS_");
    iLecPhon = 0; // 1er caractère du texte phonétique
    iLecPhonCroit = iLecPhon;
    phonDCroit = VR; // le texte commence par une virgule (va passer gauche)
    catDCroit = tab_categ(phonDCroit);
    nSousDiphCroit = 2;                       // avant le 1er sous-diphone (prochain=0)
    nSegIni.DSCroit = tab_finTim(phonDCroit); // segment de VR
    nSeg.DSCroit = nSegIni.DSCroit;
    marq.DSCroit = true;  // début de sous-diphone activé
    marq.FSCroit = false; // pas de fin de sous-diphone
    if (!nouveauSousDiph(&(iLecPhonCroit), &(phonGCroit), &(phonDCroit), &(catGCroit), &(catDCroit),
                         &(nSousDiphCroit), &(nSegIni.DSCroit), &(nSeg.DSCroit), &(ptAmp.DSCroit),
                         &(amp.DSCroit))) // fournit ts les DS
        return;
    ptAmp.DSCroit++; // prépare la prochaine période
    initDecroitSurCroit();
    amp.DSDecroit = amp.DSCroit;
    amp.FSDecroit = amp.FSCroit = 0;             // pas de fin de sous-diphone
    ptSeg.DSCroit = voice_segment(nSeg.DSCroit); // segment de VR
    perio.DSDecroit = perio.DSCroit = (unsigned char)*ptSeg.DSCroit++;
    ptSeg.DSDecroit = ptSeg.DSCroit;
    perioResult = (unsigned char)(perio.DSCroit / mulHauteur);
    xEcrEchelleDeLec =
        perioResult / 2 * mulDebit; // simule l'écriture (envoi des éch) à l'échelle de la lecture
    allonge = -xEcrEchelleDeLec;    // pour l'allongement des voyelles

    while (traiteUnePeriode())
        ;
}

static bool traiteUnePeriode() {
    if (poll_stop && poll_stop())
        return false;
    short ech;        // échantillon final à envoyer à la carte son
    short echDecroit; // échantillon de la période Decroit
    short echCroit;   // échantillon de la période Croit
    short echT;
    long echE, echS; // échantillon entrée et sortie du filtre

    while (true) {
        if (xEcrEchelleDeLec < 0)
            break;
        if (!perioSuiv(&(marq.DSCroit), &(marq.FSCroit), &(iLecPhonCroit), &(phonGCroit),
                       &(phonDCroit), &(catGCroit), &(catDCroit), &(nSousDiphCroit),
                       &(nSegIni.DSCroit), &(nSegIni.FSCroit), &(nSeg.DSCroit), &(nSeg.FSCroit),
                       &(ptSeg.DSCroit), &(ptSeg.FSCroit), &(perio.DSCroit), &(perio.FSCroit),
                       &(ptAmp.DSCroit), &(ptAmp.FSCroit), &(amp.DSCroit), &(amp.FSCroit), true))
            break; // fin du texte : on garde la dernière période valide
    }
    for (iEch = 0; iEch < perioResult; iEch++) {
        echDecroit = calculeEchPerioDecroit(iEch, ptSeg.DSDecroit, perio.DSDecroit, amp.DSDecroit) +
                     calculeEchPerioDecroit(iEch, ptSeg.FSDecroit, perio.FSDecroit, amp.FSDecroit);
        echCroit = calculeEchPerioCroit(iEch, ptSeg.DSCroit, perio.DSCroit, amp.DSCroit) +
                   calculeEchPerioCroit(iEch, ptSeg.FSCroit, perio.FSCroit, amp.FSCroit);
        echT = echDecroit + echCroit;
        echE = (long)(echT * mulVolume * MUL_AMP +
                      32768.5); // positif, arrondi sans distorsion (echE au lieu de echS)
        echS =
            echS_Prec + echE - echE_Prec -
            (echS_Prec - 32768) * 2 * PI * FREQ_COUPURE / FREQ_ECH; // formule du filtre des basses
        echS_Prec = echS;                                           // pour le prochain échantillon
        echE_Prec = echE;                                           // idem
        {                                                           // 16 bits
            echS = echS - 32768;
            if (echS > 32766)
                ech = 32766;
            else if (echS < -32766)
                ech = -32766;
            else
                ech = (short)echS;
        }
        if (pcm_size == pcm_capacity) {
            size_t next = pcm_capacity ? pcm_capacity * 2 : 32768;
            if (pcm_capacity >= 4000000) {
                failed = 1;
                return false;
            }
            if (next > 4000000)
                next = 4000000;
            void *p = realloc(pcm, next * sizeof(*pcm));
            if (!p) {
                failed = 1;
                return false;
            }
            pcm = p;
            pcm_capacity = next;
        }
        pcm[pcm_size++] = ech;
    }
    ampAnc = amp; // on repartira de l'amplitude actuelle (4 .valeurs)
    if (allonge > 0)
        allonge -= perioResult * mulDebit; // cas du ':'
    else
        xEcrEchelleDeLec += perioResult * mulDebit; // période écrite
    if (iLecPhonCroit ==
        iLecPhon) { // Croit = courant : on s'y réfère avant de calculer Decroit (cas général)
        initDecroitSurCroit(); // Initialise Décroit sur Croit
        if (!perioSuiv(&(marq.DSDecroit), &(marq.FSDecroit), &(iLecPhonDecroit), &(phonGDecroit),
                       &(phonDDecroit), &(catGDecroit), &(catDDecroit), &(nSousDiphDecroit),
                       &(nSegIni.DSDecroit), &(nSegIni.FSDecroit), &(nSeg.DSDecroit),
                       &(nSeg.FSDecroit), &(ptSeg.DSDecroit), &(ptSeg.FSDecroit),
                       &(perio.DSDecroit), &(perio.FSDecroit), &(ptAmp.DSDecroit),
                       &(ptAmp.FSDecroit), &(amp.DSDecroit), &(amp.FSDecroit), false))
            return false; // fin du message phonétique
    } // sinon le décroit est à jour
    return true;
}

static void initDecroitSurCroit() {
    iLecPhonDecroit = iLecPhonCroit;
    phonDDecroit = phonDCroit;
    phonGDecroit = phonGCroit;
    catDDecroit = catDCroit;
    catGDecroit = catGCroit;
    nSousDiphDecroit = nSousDiphCroit;
    marq.DSDecroit = marq.DSCroit;
    marq.FSDecroit = marq.FSCroit;
    ptAmp.DSDecroit = ptAmp.DSCroit;
    ptAmp.FSDecroit = ptAmp.FSCroit;
    nSegIni.DSDecroit = nSegIni.DSCroit;
    nSegIni.FSDecroit = nSegIni.FSCroit;
    nSeg.DSDecroit = nSeg.DSCroit;
    nSeg.FSDecroit = nSeg.FSCroit;
}

static bool perioSuiv(bool *marqDS, bool *marqFS, long *iLecPhonX, char *phonG, char *phonD,
                      char *catG, char *catD, char *nSousDiph, unsigned char *nSegIniDS,
                      unsigned char *nSegIniFS, unsigned char *nSegDS, unsigned char *nSegFS,
                      char **ptSegDS, char **ptSegFS, unsigned char *perioDS,
                      unsigned char *perioFS, unsigned char **ptAmpDS, unsigned char **ptAmpFS,
                      unsigned char *ampDS, unsigned char *ampFS, bool mCroit) {
    if (!(*marqDS) && !(*marqFS))
        return false;
    (*ampDS) = (*ampFS) = 0; // par défaut si pas marq
    if ((*marqDS)) {
        while ((*marqDS)) {
            (*ampDS) = *(*ptAmpDS);
            if (((*ampDS) & 128) == 128) { // début du sous-phonème suivant
                (*ampDS) -= 128;
                (*marqFS) = (*marqDS);
                (*ptAmpFS) = (*ptAmpDS);
                (*ampFS) = (*ampDS);
                (*nSegIniFS) = (*nSegIniDS);
                (*nSegFS) = (*nSegDS);
                (*ptSegFS) = (*ptSegDS);
                (*perioFS) = (*perioDS);
                (*ampDS) = 0; // par defaut si pas marq
                (*marqDS) =
                    nouveauSousDiph(&((*iLecPhonX)), &((*phonG)), &((*phonD)), &((*catG)),
                                    &((*catD)), &((*nSousDiph)), &((*nSegIniDS)), &((*nSegDS)),
                                    &((*ptAmpDS)), &((*ampDS))); // fournit ts les DS
            } else
                break;
        }
        if (((*ampDS) & 32) == 32) { // phonème avec bruit (fshvzjr), segment suivant
            (*ampDS) -= 32;
            (*nSegDS)++;
        } else
            (*nSegDS) = (*nSegIniDS);
        (*ptSegDS) = voice_segment((*nSegDS));
        (*perioDS) = (unsigned char)*(*ptSegDS)++;
    }
    if ((*marqFS)) {
        (*ampFS) = *(*ptAmpFS) & 127;
        if (((*ampFS) & 32) == 32) { // phonème avec bruit (fshvzjr), segment suivant
            (*ampFS) -= 32;
            (*nSegFS)++;
        } else
            (*nSegFS) = (*nSegIniFS);
        if (((*ampFS) & 64) == 64) { // dernière période du sous-phonème
            (*ampFS) -= 64;
            (*marqFS) = false; // pour le prochain tour
        }
        (*ptSegFS) = voice_segment((*nSegFS));
        (*perioFS) = (unsigned char)*(*ptSegFS)++;
    }
    if ((*marqDS))
        (*ptAmpDS)++;
    if ((*marqFS))
        (*ptAmpFS)++;
    unsigned sum_amp = amp.DSDecroit + amp.FSDecroit + amp.DSCroit + amp.FSCroit;
    perioBase =
        sum_amp ? ((float)perio.DSDecroit * amp.DSDecroit + (float)perio.FSDecroit * amp.FSDecroit +
                   (float)perio.DSCroit * amp.DSCroit + (float)perio.FSCroit * amp.FSCroit) /
                      sum_amp
                : 0;
    if (contour_active && contour_count) {
        while (contour_index + 1 < contour_count && pcm_size >= contours[contour_index].end)
            contour_index++;
        size_t start = contour_index ? contours[contour_index - 1].end : 0;
        size_t end = contours[contour_index].end;
        double u = end > start ? fmin(1, (double)(pcm_size - start) / (end - start)) : 1;
        double tail = fmax(0, (u - .60) / .40);
        tail = tail * tail * (3 - 2 * tail);
        double target = contour_strength * (.45 * sin(PI * u)
                            + contours[contour_index].ending * .25 * tail);
        /* Ease changes at clause boundaries over roughly 20 ms. */
        contour_pitch += (target - contour_pitch) * (1 - exp(-perioBase / 200));
        mulHauteur = (float)((double)tab_tabHau(engine_pitch - HAUTEUR_REF)
                            / 1000 * pow(2, contour_pitch / 12));
        /* Extreme reader settings must still fit the legacy byte period. */
        perioResult = (unsigned char)fmin(250, fmax(2, perioBase / mulHauteur + .5));
    } else
        perioResult = (unsigned char)(perioBase / mulHauteur + .5);
    if (mCroit)
        xEcrEchelleDeLec -= perioResult * mulHauteur; // période lue mais pas forcément écrite
    return true;
}

static bool nouveauSousDiph(long *iLecPhonX, char *phonG, char *phonD, char *catG, char *catD,
                            char *nSousDiph, unsigned char *nSegIniDS, unsigned char *nSegDS,
                            unsigned char **ptAmpDS, unsigned char *ampDS) {
    short nAmp;

    if ((*nSousDiph) == 0) {
        (*nSousDiph)++;
        (*ptAmpDS) = tab_getPtAmp(tab_traAmp((*catG), (*catD)));
        (*ampDS) = *(*ptAmpDS);
        (*nSegIniDS) = tab_traTim((*phonG), (*phonD));
    } else if ((*nSousDiph) == 1) {
        (*nSousDiph)++;
        (*ptAmpDS) = tab_getPtAmp(tab_debAmp((*catG), (*catD)));
        (*ampDS) = *(*ptAmpDS);
        (*nSegIniDS) = tab_debTim((*phonD));
    } else {
        (*nSousDiph) = 0;
        (*phonG) = (*phonD);
        (*catG) = (*catD);
        (*phonD) = phonSuiv(&((*iLecPhonX)), (*phonG));
        if ((*phonD) == -1)
            return false;
        (*catD) = tab_categ((*phonD));
        nAmp = tab_finAmp((*catG), (*catD));
        if (nAmp > 100) {
            nAmp -= 100;
            (*nSousDiph)++;
        }
        (*ptAmpDS) = tab_getPtAmp(nAmp);
        (*ampDS) = *(*ptAmpDS);
        if ((*phonG) == YY && (*phonD) > YY && (*phonD) < VR)
            (*nSegIniDS) = tab_traTim(II, EU);
        else
            (*nSegIniDS) = tab_finTim((*phonG));
    }
    (*nSegDS) = (*nSegIniDS);
    return true;
}

static char phonSuiv(long *iLecPhonX, char phonG) {

    if ((*iLecPhonX) < iLecPhon) { // phonème déjà trouvé pour Croit ou Décroit
        (*iLecPhonX) = iLecPhon;
        return phonMem; // gardé en mémoire
    } // phonème suivant
    while (true) {
        phonMem = texPhon[iLecPhon];
        if (phonMem == 0) {
            phonMem = -1;
            break; // fin du texte phonétique
        }
        phonMem -= 65;
        iLecPhon++;
        if ((phonMem < 0 || phonMem > PL) && phonMem + 65 != INDEX)
            continue;
        if (phonMem < YY && phonMem == phonG)
            phonMem = PL;
        if (phonMem == PL)
            allonge += 250; //+25 ms à débit normal (~25 %)
        else if (phonMem + 65 == INDEX) {
            if (word_seen < word_count)
                word_samples[word_seen++] = pcm_size;
        } else
            break; // rien de particulier donc phonème
    }
    (*iLecPhonX) = iLecPhon;
    if (phonMem == -1 && phonG != VR)
        phonMem = VR;
    return phonMem;
}

static short calculeEchPerioDecroit(short x, char *ptSegDecroit, unsigned char perioDecroit,
                                    unsigned char ampDecroit) {

    if (ampDecroit == 0)
        return 0;
    short xCadre = x + perioDecroit / 4; // cadrage de la fenêtre sur la partie de forte amplitude
    if (xCadre >= perioDecroit)
        xCadre -= perioDecroit;
    if (perioResult <= perioDecroit) { // pério résultante <= pério decroit
        return (short)(fenDecroit((float)x / perioResult) * ptSegDecroit[xCadre] * ampDecroit);
    } else { // pério résultante > pério decroit
        if (x < perioDecroit)
            return (short)(fenDecroit((float)x / perioDecroit) * ptSegDecroit[xCadre] * ampDecroit);
        else
            return 0; // reste nul derrière la fenêtre décroit
    }
}

static short calculeEchPerioCroit(short x, char *ptSegCroit, unsigned char perioCroit,
                                  unsigned char ampCroit) {

    if (ampCroit == 0)
        return 0;
    short xCadre = x + perioCroit - perioResult +
                   perioCroit / 4; // cadrage de la fenêtre sur la partie de forte amplitude
    if (xCadre >= perioCroit)
        xCadre -= perioCroit;
    if (perioResult <= perioCroit) { // pério résultante <= pério croit
        return (short)(fenCroit((float)x / perioResult) * ptSegCroit[xCadre] * ampCroit);
    } else { // pério résultante > pério croit
        if (x >= perioResult - perioCroit)
            return (short)(fenCroit((float)(x + perioCroit - perioResult) / perioCroit) *
                           ptSegCroit[xCadre] * ampCroit);
        else
            return 0; // nul avant la fenêtre croit
    }
}

static float fenCroit(float x) {
    if (x < 0)
        x = 0;
    else if (x > 1)
        x = 1;
    return (1 - (float)cos(x * PI)) / 2;
}

static float fenDecroit(float x) {
    if (x < 0)
        x = 0;
    else if (x > 1)
        x = 1;
    return (1 + (float)cos(x * PI)) / 2;
}

void s3_init(void) {
    tTab = (char *)voice_tables;
    aTWin = readShort(tTab + 0);
    aTCat = readShort(tTab + 2);
    aTPhon1 = readShort(tTab + 4);
    aTPhon2 = readShort(tTab + 6);
    aTCatP1 = readShort(tTab + 8);
    aTCatP2 = readShort(tTab + 10);
    aChLst1 = readShort(tTab + 12);
    aChLst2 = readShort(tTab + 14);
    aChLst3 = readShort(tTab + 16);
    aCh01 = readShort(tTab + 18);
    aCh1S = readShort(tTab + 20);
    aCh100 = readShort(tTab + 22);
    aCh100Z = readShort(tTab + 24);
    aTabVol = readShort(tTab + 26);
    aTabVit = readShort(tTab + 28);
    aTabHau = readShort(tTab + 30);
    aCateg = readShort(tTab + 32);
    aFinAmp = readShort(tTab + 34);
    aFinTim = readShort(tTab + 36);
    aTraAmp = readShort(tTab + 38);
    aTraTim = readShort(tTab + 40);
    aDebAmp = readShort(tTab + 42);
    aDebTim = readShort(tTab + 44);
    aTabAmp = readShort(tTab + 46);
    aAdAmp = readShort(tTab + 48);
    aTabDeb = readShort(tTab + 50);
    a_Huit1 = readShort(tTab + 52);
    a_Dix1 = readShort(tTab + 54);
    aXChif = readShort(tTab + 56);
    lgTab = readShort(tTab + 58);
    FOIS = (unsigned char)tTab[60];
}

static int render_core(const char *latin1, int rate, int pitch, int (*stop)(void), const int16_t **samples,
              size_t *count) {
    char indexed[1024] = {0}, alpha[NM_CAR_TEX] = {0}, phon[NM_CAR_TEX] = {0};
    size_t n = strlen(latin1), j = 0;
    if (n > 240 || rate < 0 || rate > 15 || pitch < 0 || pitch > 15)
        return -1;
    /* Preserve legacy automatic word markers, including their effect on rules. */
    word_count = word_seen = 0;
    bool word = false;
    for (size_t i = 0; i < n; ++i) {
        bool valid = latin1[i] != ' ' && latin1[i] != '\n' && latin1[i] != '\r';
        if (!valid && word) {
            word_bytes[word_count++] = i;
            indexed[j++] = MARQ_MARQ;
            indexed[j++] = MARQ_INDEX;
        }
        word = valid;
        indexed[j++] = latin1[i];
    }
    word_bytes[word_count++] = n;
    indexed[j++] = MARQ_MARQ;
    indexed[j++] = MARQ_INDEX;
    indexed[j] = 0;
    engine_rate = rate;
    engine_pitch = pitch;
    poll_stop = stop;
    pcm_size = 0;
    failed = 0;
    minMajNFois(indexed, alpha);
    graphemePhoneme(alpha, phon);
    if (!poll_stop || !poll_stop())
        traiteTextePhonetique(phon);
    *samples = pcm;
    *count = pcm_size;
    return failed ? -1 : 0;
}
int s3_render(const char *text, int rate, int pitch, int (*stop)(void),
              const int16_t **samples, size_t *count) {
    contour_active = 0;
    return render_core(text, rate, pitch, stop, samples, count);
}
int s3_render_modulated(const char *text, int rate, int pitch, int range,
                        int (*stop)(void), const int16_t **samples, size_t *count) {
    if (range < -100 || range > 100)
        return -1;
    int status = s3_render(text, rate, pitch, stop, samples, count);
    if (status || range <= 0 || !*count || (stop && stop()))
        return status;
    contour_count = contour_index = 0;
    size_t start = 0;
    for (size_t i = 0; text[i]; i++) {
        const char *punct = strchr(".?!,;:", text[i]);
        if (!punct || (text[i + 1] && strchr(".?!,;:", text[i + 1])))
            continue;
        /* Decimal separators are not phrase boundaries. */
        if (i && text[i - 1] >= '0' && text[i - 1] <= '9' &&
            text[i + 1] >= '0' && text[i + 1] <= '9')
            continue;
        size_t end = text[i + 1] ? s3_sample_at(i + 1) : *count;
        if (end <= start)
            continue;
        int ending = text[i] == '?' ? 4 : strchr(",;:", text[i]) ? 1 : -3;
        contours[contour_count].end = end;
        contours[contour_count++].ending = ending;
        start = end;
    }
    if (start < *count) {
        contours[contour_count].end = *count;
        /* No artificial sentence fall at a 240-byte chunk boundary. */
        contours[contour_count++].ending = 0;
    }
    contour_strength = range * .075; /* Orca 6 -> 1.5; 7 -> 3; 10 -> 7.5 semitones. */
    contour_pitch = 0;
    contour_active = 1;
    status = render_core(text, rate, pitch, stop, samples, count);
    contour_active = 0;
    return status;
}
size_t s3_sample_at(size_t byte_offset) {
    size_t sample = 0;
    for (size_t i = 0; i < word_seen && word_bytes[i] <= byte_offset; i++)
        sample = word_samples[i];
    return sample;
}
void s3_close(void) {
    free(pcm);
    pcm = NULL;
    pcm_capacity = pcm_size = 0;
}
