/*
 * CENERE DI VELMORA - soulslike originale per PSP (homebrew)
 * Logica e rendering software, indipendenti dalla piattaforma.
 */
#include "game.h"
#include "render.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define RGB(r,g,b) (0xFF000000u | ((uint32_t)(b)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(r))
#define WORLDT 6144       /* lato del mondo in caselle */
#define MW 256            /* finestra in memoria intorno al giocatore */
#define MH 256
#define COREX 2992        /* il cuore del mondo (Velmora e dintorni) */
#define COREY 2992
#define CORESZ 160
#define CX0 40            /* origine della citta' nel mondo */
#define CY0 60
#define CITYW 94
#define CITYH 50
#define WL (-5.0f)        /* livello dell'acqua del lago */
#define TS 16
#define MAXE 96
#define MAXPART 128
#define PI 3.14159265f

/* T_LAMP e' attraversabile; da T_GRAVE in su gli oggetti sono solidi */
enum { T_FLOOR = 0, T_WALL = 1, T_GATE = 2, T_LAMP = 3, T_GRAVE = 4, T_CRATE = 5, T_TREE = 6, T_FIRE = 7,
       T_GRASS = 8, T_DIRT = 9, T_PIER = 10, T_ROCK = 11, T_WATER = 12, T_PINE = 13, T_BOULDER = 14, T_RUIN = 15, T_WELL = 16 };
enum { M_TITLE, M_PLAY, M_DEAD, M_MENU, M_PAUSE, M_WIN, M_STORY, M_TALK, M_END };
enum { SFX_SWING, SFX_HIT, SFX_HURT, SFX_ROLL, SFX_HEAL, SFX_BRAZIER, SFX_PICK, SFX_EDEATH, SFX_ROAR, SFX_SLAM, SFX_BELL, SFX_MENU, SFX_SELECT, SFX_DIE, SFX_VICTORY };
enum { PS_FREE, PS_ATK, PS_ROLL, PS_HEAL, PS_HURT, PS_DEAD };
enum { ES_IDLE, ES_CHASE, ES_WIND, ES_STRIKE, ES_REC, ES_STAG, ES_DEAD };
enum { BS_OFF, BS_INTRO, BS_CHASE, BS_WIND, BS_ACT, BS_REC, BS_ROAR, BS_DEAD };
enum { BA_SWIPE, BA_SLAM, BA_CHARGE };

/* ------------------------------------------------------------------ */
/* stato globale                                                       */
/* ------------------------------------------------------------------ */
static unsigned char map[MH][MW];
static int WX0, WY0;      /* origine della finestra nel mondo (caselle) */
static unsigned rng_s = 1234567u;
static int mode, frame, shake, deadT, winT;
static char msg[48];
static int msgT;
static int gate_closed;
static float camYaw = -PI / 2, camX, camZ, camPitch = 0.36f, camDistS = 62.0f;
static int lTap, lUsed;
static float camGY;
static int menuSel;

static unsigned rnd(void) { rng_s = rng_s * 1103515245u + 12345u; return (rng_s >> 16) & 0x7fff; }
static float frnd(void) { return rnd() / 32767.0f; }

static int cpB = 0;       /* braciere dell'ultimo riposo */

typedef struct {
    int hp; float speed, range; int wind, dmg, rec, reward, poise; float r;
} EKind;
static const EKind EK[4] = {
    /* Reietto: veloce e fragile */
    { 70, 1.05f, 22.0f, 30, 18, 34, 60, 0, 7.0f },
    /* Lanciere: lento, lungo raggio, non si scompone con gli attacchi leggeri */
    { 120, 0.80f, 34.0f, 42, 26, 40, 110, 1, 8.0f },
    /* Lupo cinereo: rapidissimo, balzo in avanti */
    { 60, 1.6f, 20.0f, 22, 16, 26, 70, 0, 6.5f },
    /* Colosso di pietra: lento, colpi devastanti, non si scompone */
    { 320, 0.7f, 34.0f, 52, 42, 52, 450, 1, 11.0f },
};

/* coordinate del mondo (caselle) e tipo di nemico */
#define C(x, y) (x) + CX0, (y) + CY0
static const int spawns[][3] = {
    /* citta' */
    {C(26, 8), 0}, {C(38, 9), 0}, {C(48, 12), 1}, {C(36, 18), 0}, {C(50, 4), 0}, {C(41, 25), 0},
    {C(30, 34), 0}, {C(54, 34), 1}, {C(28, 40), 0}, {C(56, 40), 0}, {C(42, 41), 1}, {C(62, 36), 0},
    /* Bosco delle Ceneri */
    {22, 60, 2}, {14, 72, 2}, {26, 92, 2}, {10, 102, 2}, {20, 118, 2}, {30, 40, 2},
    /* Collina dei Sepolti */
    {78, 24, 0}, {93, 28, 0}, {84, 32, 1}, {70, 40, 0}, {101, 22, 1}, {86, 9, 0},
    /* Lago Nero */
    {70, 120, 0}, {100, 118, 2}, {76, 126, 0}, {96, 125, 2},
    /* strade intorno alle mura */
    {60, 55, 0}, {112, 54, 2}, {110, 115, 0}, {136, 58, 1},
    /* Rovine di Ormval */
    {144, 70, 1}, {150, 101, 1}, {142, 93, 0}, {151, 55, 2}, {146, 120, 3},
    /* Borgo di Salmastra */
    {44, 125, 0}, {58, 125, 1}, {51, 118, 0}, {51, 132, 2}, {48, 127, 0},
    /* Altura di San Vesper */
    {108, 25, 1}, {124, 25, 0}, {116, 17, 0}, {116, 33, 1}, {113, 27, 0},
    /* Cardoverde */
    {11, 31, 2}, {25, 31, 0}, {18, 24, 2}, {18, 38, 0}
};

#define NSPAWN ((int)(sizeof(spawns) / sizeof(spawns[0])))

typedef struct {
    float x, y, fx, fy;
    int kind, hp, state, timer, alive, flash, hitBy, hitDone, deadT, id;
} Enemy;
static Enemy E[MAXE];
static int NE;

static struct {
    float x, y, fx, fy, kx, ky;
    float hp, hpGhost, st;
    int state, timer, atk, atkId, hitDone;
    int iframes, stDelay, flasks, frags;
    int vig, end, str, lvl;
    unsigned buf; int bufT;
    int lock;            /* -1 nessuno, 0..NE-1 nemico, 100 boss */
    float rx, ry, walk;
    int maxFlask, region;
} P;

static struct {
    float x, y, fx, fy, sx, sy, tx, ty;
    int hp, maxhp, state, timer, atk, phase, dead, flash, hitDone, combo;
    float z;
} Bs;

static struct { int active; float x, y; int amount; } drop;

/* ------------------------------------------------------------------ */
/* boss delle regioni (design originali) e Braci Primeve               */
/* ------------------------------------------------------------------ */
enum { RB_MATRIARCA, RB_CAMPANARO, RB_ANNEGATA, RB_RE, NRB };
enum { RA_SWIPE, RA_SLAM, RA_CHARGE, RA_WAVE, RA_BOLTS };
enum { RS_IDLE, RS_INTRO, RS_CHASE, RS_WIND, RS_ACT, RS_REC, RS_ROAR, RS_DEAD };

typedef struct {
    const char *name;
    int ax, ay;            /* centro dell'arena (coordinate del cuore del mondo) */
    float radius;          /* raggio dell'arena in caselle */
    int hp;
    float speed, body;
    int atk[3];            /* attacchi a disposizione */
    int dmg;
} RBDef;

static const RBDef RBD[NRB] = {
    { "LA MATRIARCA DEL BRANCO", 18, 100, 9.0f, 900, 1.9f, 13, { RA_CHARGE, RA_SWIPE, RA_WAVE }, 28 },
    { "IL CAMPANARO CIECO",      88, 10,  6.5f, 1100, 0.9f, 10, { RA_SLAM, RA_SWIPE, RA_WAVE }, 34 },
    { "L'ANNEGATA",              85, 130, 6.0f, 1000, 1.0f, 11, { RA_BOLTS, RA_SLAM, RA_SWIPE }, 30 },
    { "IL RE DI PIETRA",         148, 86, 9.0f, 1500, 0.85f, 16, { RA_SWIPE, RA_SLAM, RA_CHARGE }, 40 },
};

typedef struct {
    float x, y, fx, fy, tx, ty, sx, sy, z;
    int hp, state, timer, atk, phase, flash, hitDone, combo, defeated;
} RBoss;
static RBoss RB[NRB];

/* proiettili e onde d'urto */
typedef struct { float x, y, vx, vy; int life, dmg; } Bolt;
typedef struct { float x, y, r, speed, maxr; int dmg, hit; } Wave;
#define MAXBOLT 16
#define MAXWAVE 6
static Bolt bolts[MAXBOLT];
static Wave waves[MAXWAVE];



typedef struct { float x, y, vx, vy; int life; uint32_t col; } Part;
static Part parts[MAXPART];

/* macchie di sangue sul selciato */
#define MAXBLOOD 40
typedef struct { float x, y, r; int life; } Blood;
static Blood blood[MAXBLOOD];
static int bloodNext;
static void add_blood(float x, float y, float r)
{
    Blood *b = &blood[bloodNext];
    bloodNext = (bloodNext + 1) % MAXBLOOD;
    b->x = x; b->y = y; b->r = r; b->life = 1500;
}
static void build_lights(void);
static void gen_textures(void);
static void sfx(int id);
static void save_game(void);
static void rb_player_hits(float range, int dmg);
static void rb_update(void);
static void rb_reset_all(void);
static void rb_shift(float ox, float oy);
static void rb_draw(void);
static void draw_npc(void);
static int near_npc(void);
static void npc_talk(void);
static int load_game(void);
static int save_exists(void);
static int rbActive = -1;     /* boss della regione in combattimento, -1 nessuno */
static int embers;            /* Braci Primeve raccolte */
static int deaths, playtime, endingSeen, titleSel, hasSave;
static int storyPage, storyT;


/* ------------------------------------------------------------------ */
/* statistiche                                                         */
/* ------------------------------------------------------------------ */
static int maxhp(void) { return 80 + 12 * P.vig; }
static int maxst(void) { return 70 + 8 * P.end; }
static int dmgL(void) { return 18 + 4 * P.str; }
static int dmgH(void) { return 36 + 8 * P.str; }
static int lvlCost(void) { return 80 + P.lvl * 40; }
#define MAXFLASK (P.maxFlask)

/* ------------------------------------------------------------------ */
/* mondo grande: 6144x6144 caselle, finestra di 256x256 intorno al     */
/* giocatore che si sposta con lui (origine mobile)                    */
/* ------------------------------------------------------------------ */
static unsigned hash2(int x, int y);
static float vnoise2(float x, float y, int px, int py, int s);
static float hgt[MH + 1][MW + 1];      /* altezza agli angoli delle caselle (finestra) */
static float hshade[MH + 1][MW + 1];   /* ombreggiatura dei pendii */
static int cl_x0, cl_y0, cl_x1, cl_y1; /* zona della finestra che si sta generando */

static int abs_i(int v) { return v < 0 ? -v : v; }
static float smooth01(float t) { if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }

/* --- scrittura/lettura in coordinate del mondo, limitata alla zona in generazione --- */
static int inclip(int lx, int ly) { return lx >= cl_x0 && ly >= cl_y0 && lx <= cl_x1 && ly <= cl_y1 && lx >= 0 && ly >= 0 && lx < MW && ly < MH; }
static void setw(int wx, int wy, int t) { int lx = wx - WX0, ly = wy - WY0; if (inclip(lx, ly)) map[ly][lx] = (unsigned char)t; }
static int get_tw(int wx, int wy) { int lx = wx - WX0, ly = wy - WY0; if (lx < 0 || ly < 0 || lx >= MW || ly >= MH) return T_ROCK; return map[ly][lx]; }
static void fillW(int x0, int y0, int x1, int y1, int t)
{
    int x, y;
    if (x0 - WX0 < cl_x0) x0 = cl_x0 + WX0;
    if (y0 - WY0 < cl_y0) y0 = cl_y0 + WY0;
    if (x1 - WX0 > cl_x1) x1 = cl_x1 + WX0;
    if (y1 - WY0 > cl_y1) y1 = cl_y1 + WY0;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) map[y - WY0][x - WX0] = (unsigned char)t;
}
#define FK(a, b, c, d, t) fillW((a) + COREX, (b) + COREY, (c) + COREX, (d) + COREY, t)
#define SK(x, y, t) setw((x) + COREX, (y) + COREY, t)

/* --- borghi: tre nel cuore del mondo, gli altri generati per celle --- */
#define CELL 320
#define NCELL ((WORLDT + CELL - 1) / CELL)
typedef struct { int x0, y0, x1, y1; float base; char name[24]; int ok; } Town;   /* coordinate del mondo */
static Town ctown[3];
static Town ptown[NCELL][NCELL];
static const struct { int x0, y0, x1, y1; float base; const char *name; } core_towns[3] = {
    { 42, 116, 60, 134, -2.0f, "BORGO DI SALMASTRA" },     /* villaggio di pescatori sulla riva del lago */
    { 104, 14, 128, 36, 22.0f, "ALTURA DI SAN VESPER" },  /* borgo-monastero sulla collina */
    { 8, 22, 28, 40, 14.0f, "CARDOVERDE" },               /* frazione nel bosco */
};

static int in_core_w(int wx, int wy) { return wx >= COREX && wy >= COREY && wx < COREX + CORESZ && wy < COREY + CORESZ; }
static int in_city_w(int wx, int wy) { int kx = wx - COREX, ky = wy - COREY; return kx >= CX0 && ky >= CY0 && kx < CX0 + CITYW && ky < CY0 + CITYH; }
static const Town *town_at_w(int wx, int wy)
{
    int i, cx = wx / CELL, cy = wy / CELL;
    for (i = 0; i < 3; i++)
        if (wx >= ctown[i].x0 && wx <= ctown[i].x1 && wy >= ctown[i].y0 && wy <= ctown[i].y1) return &ctown[i];
    if (cx >= 0 && cy >= 0 && cx < NCELL && cy < NCELL && ptown[cy][cx].ok) {
        const Town *t = &ptown[cy][cx];
        if (wx >= t->x0 && wx <= t->x1 && wy >= t->y0 && wy <= t->y1) return t;
    }
    return 0;
}

/* versioni in coordinate locali (finestra), usate dal resto del gioco */
static int in_city(int tx, int ty) { return in_city_w(tx + WX0, ty + WY0); }
static int in_town(int tx, int ty) { return in_city(tx, ty) || town_at_w(tx + WX0, ty + WY0) != 0; }
static float town_base(int tx, int ty) { const Town *t = town_at_w(tx + WX0, ty + WY0); return t ? t->base : 0.0f; }
static int in_arena(float x, float y)
{
    int kx = (int)floorf(x / TS) + WX0 - COREX, ky = (int)floorf(y / TS) + WY0 - COREY;
    return kx >= CX0 + 67 && kx <= CX0 + 90 && ky >= CY0 + 26 && ky <= CY0 + 46;
}
static int tile_solid(int t)
{
    return t == T_WALL || t == T_GRAVE || t == T_CRATE || t == T_TREE || t == T_FIRE ||
           t == T_ROCK || t == T_WATER || t == T_PINE || t == T_BOULDER || t == T_RUIN || t == T_WELL;
}
static int solid(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) return 1;
    if (tile_solid(map[ty][tx])) return 1;
    if (map[ty][tx] == T_GATE) return gate_closed;
    return 0;
}

/* --- altezze --- */
static float core_h(int kx, int ky)
{
    float h, ld, n1, n2;
    int i;
    if (kx < 0) kx = 0;
    if (ky < 0) ky = 0;
    if (kx > CORESZ) kx = CORESZ;
    if (ky > CORESZ) ky = CORESZ;
    n1 = vnoise2(kx / 11.0f, ky / 11.0f, 64, 64, 101); n2 = vnoise2(kx / 4.5f, ky / 4.5f, 64, 64, 103);
    h = (n1 - 0.5f) * 26 + (n2 - 0.5f) * 7;
    if (ky < 58) h += (58 - ky) * 0.75f;
    if (kx > 137) h = h * 0.4f + 14 + (kx - 137) * 0.2f;
    { float dx = (kx - 86) / 24.0f, dy = (ky - 137) / 15.0f; ld = sqrtf(dx * dx + dy * dy); }
    if (ld < 1.35f) { float k = smooth01((ld - 0.75f) / 0.6f); h = h * k + (-11.0f) * (1 - k); }
    {
        float dx = kx < CX0 ? CX0 - kx : (kx > CX0 + CITYW ? kx - (CX0 + CITYW) : 0);
        float dy = ky < CY0 ? CY0 - ky : (ky > CY0 + CITYH ? ky - (CY0 + CITYH) : 0);
        float cd = sqrtf(dx * dx + dy * dy);
        h *= smooth01(cd / 9.0f);
        if (cd <= 0.01f) h = 0;
    }
    for (i = 0; i < 3; i++) {
        float ddx = kx < core_towns[i].x0 ? core_towns[i].x0 - kx : (kx > core_towns[i].x1 + 1 ? kx - (core_towns[i].x1 + 1) : 0);
        float ddy = ky < core_towns[i].y0 ? core_towns[i].y0 - ky : (ky > core_towns[i].y1 + 1 ? ky - (core_towns[i].y1 + 1) : 0);
        float k = smooth01((sqrtf(ddx * ddx + ddy * ddy) - 1) / 8.0f);
        h = core_towns[i].base + (h - core_towns[i].base) * k;
    }
    return h;
}

/* terreno procedurale: continenti, catene montuose a cresta, colline, dettaglio */
static float proc_h(int wx, int wy)
{
    float c = vnoise2(wx / 520.0f, wy / 520.0f, 1024, 1024, 201);
    float m = vnoise2(wx / 170.0f, wy / 170.0f, 1024, 1024, 203);
    float hl = vnoise2(wx / 45.0f, wy / 45.0f, 1024, 1024, 205);
    float d1 = vnoise2(wx / 11.0f, wy / 11.0f, 1024, 1024, 101), d2 = vnoise2(wx / 4.5f, wy / 4.5f, 1024, 1024, 103);
    float ridge = 1 - fabsf(m * 2 - 1), mount = smooth01((c - 0.6f) / 0.2f);
    float lake = vnoise2(wx / 130.0f, wy / 130.0f, 1024, 1024, 207);
    float base = (c - 0.3f) * 30 + (hl - 0.5f) * 18 + mount * ridge * ridge * 95;
    float det = (d1 - 0.5f) * 10 + (d2 - 0.5f) * 4;
    /* laghi: conche ampie e lisce, solo dove il rumore dei laghi e' basso */
    if (lake < 0.3f) { float k = smooth01((0.3f - lake) / 0.1f); base = base * (1 - k) + (-12.0f) * k; det *= 1 - k; }
    else if (base < WL + 2) base = WL + 2 + (base - (WL + 2)) * 0.2f;   /* niente pozze sparse fuori dai laghi */
    return base + det * smooth01((base - WL) / 6.0f);
}

static float height_raw(int wx, int wy)
{
    float h, e;
    int kx = wx - COREX, ky = wy - COREY;
    float dx = kx < 0 ? -kx : (kx > CORESZ ? kx - CORESZ : 0), dy = ky < 0 ? -ky : (ky > CORESZ ? ky - CORESZ : 0);
    float dc = sqrtf(dx * dx + dy * dy);
    if (dc <= 0) h = core_h(kx, ky);
    else { float w = smooth01(dc / 28.0f); h = core_h(kx, ky) * (1 - w) + proc_h(wx, wy) * w; }
    e = (float)(wx < wy ? wx : wy);
    if (WORLDT - wx < e) e = (float)(WORLDT - wx);
    if (WORLDT - wy < e) e = (float)(WORLDT - wy);
    if (e < 40) h += (40 - e) * 3;          /* montagne invalicabili ai confini del mondo */
    return h;
}

static float height_at(int wx, int wy)
{
    float h = height_raw(wx, wy);
    int cx = wx / CELL, cy = wy / CELL, i, j;
    for (j = cy - 1; j <= cy + 1; j++)
        for (i = cx - 1; i <= cx + 1; i++) {
            const Town *t;
            float ddx, ddy, k;
            if (i < 0 || j < 0 || i >= NCELL || j >= NCELL || !ptown[j][i].ok) continue;
            t = &ptown[j][i];
            ddx = wx < t->x0 ? t->x0 - wx : (wx > t->x1 + 1 ? wx - (t->x1 + 1) : 0);
            ddy = wy < t->y0 ? t->y0 - wy : (wy > t->y1 + 1 ? wy - (t->y1 + 1) : 0);
            if (ddx > 12 || ddy > 12) continue;
            k = smooth01((sqrtf(ddx * ddx + ddy * ddy) - 1) / 9.0f);
            h = t->base + (h - t->base) * k;
        }
    return h;
}

/* --- elenchi globali: strade, rovine --- */
typedef struct { int x[3], y[3], n, bx0, by0, bx1, by1; } Road;
#define MAXROAD 900
static Road roads[MAXROAD];
static int nroad;
typedef struct { int x0, y0, x1, y1; } Ruin;
#define MAXRUIN 300
static Ruin pruins[MAXRUIN];
static int nruin;

static void add_road(int x0, int y0, int x1, int y1, unsigned h)
{
    Road *r;
    int mx = (x0 + x1) / 2 + (int)(h % 61) - 30, my = (y0 + y1) / 2 + (int)((h >> 8) % 61) - 30, k;
    if (nroad >= MAXROAD) return;
    r = &roads[nroad++];
    r->x[0] = x0; r->y[0] = y0; r->x[1] = mx; r->y[1] = my; r->x[2] = x1; r->y[2] = y1; r->n = 3;
    r->bx0 = r->bx1 = x0; r->by0 = r->by1 = y0;
    for (k = 1; k < 3; k++) {
        if (r->x[k] < r->bx0) r->bx0 = r->x[k];
        if (r->x[k] > r->bx1) r->bx1 = r->x[k];
        if (r->y[k] < r->by0) r->by0 = r->y[k];
        if (r->y[k] > r->by1) r->by1 = r->y[k];
    }
}

static void town_name(char *out, unsigned h)
{
    static const char *pre[] = { "", "", "", "BORGO ", "ROCCA ", "SAN ", "VILLA ", "PIEVE ", "CASALE " };
    static const char *s1[] = { "MOR", "VAL", "CEN", "ARD", "SEL", "BRU", "FOSC", "LUM", "GRAV", "ORT", "CAL", "TEN",
                                "VERM", "ASP", "ROC", "NEB", "SOL", "CRIN", "DAR", "FER", "TORB", "MALV", "SCUR", "PALL" };
    static const char *s2[] = { "ANA", "ELLO", "ORA", "IGNA", "ASCO", "EDA", "URNO", "INA", "ACCIA", "OLO", "ETO", "IANO", "ERA", "UZZO", "ENTO", "ISA" };
    strcpy(out, pre[h % 9]);
    strcat(out, s1[(h >> 4) % 24]);
    strcat(out, s2[(h >> 10) % 16]);
}

/* bracieri e oggetti procedurali, aggiunti agli elenchi globali */
#define MAXBRAZ 320
#define MAXPICK 600
typedef struct { int tx, ty, lit; } Brazier;      /* coordinate del mondo */
typedef struct { int tx, ty, type, amount, taken; } Pick;
static Brazier braz[MAXBRAZ];
static int nbraz;
static Pick picks[MAXPICK];
static int npick;

static void add_pick(int wx, int wy, unsigned h)
{
    Pick *p;
    float d = sqrtf((float)((wx - 3072) * (wx - 3072) + (wy - 3072) * (wy - 3072)));
    if (npick >= MAXPICK) return;
    p = &picks[npick++];
    p->tx = wx; p->ty = wy; p->taken = 0;
    p->type = h % 4 == 0 ? 1 : 0;
    p->amount = 300 + (int)(d * 0.4f) + (int)(h % 5) * 100;
}

static void init_world(void)
{
    int cx, cy, i;
    static const int core_braz[8][2] = { {CX0 + 6, CY0 + 6}, {CX0 + 42, CY0 + 36}, {18, 80}, {88, 20}, {85, 118}, {54, 123}, {119, 23}, {21, 29} };
    static const int core_picks[13][4] = {
        {10, 50, 1, 0}, {25, 126, 0, 400}, {74, 9, 0, 600}, {91, 10, 1, 0}, {86, 121, 0, 500}, {150, 62, 1, 0},
        {153, 112, 0, 900}, {131, 24, 0, 400}, {14, 142, 0, 700}, {CX0 + 88, CY0 + 30, 1, 0},
        {47, 121, 1, 0}, {112, 21, 0, 800}, {14, 27, 0, 500}
    };
    for (i = 0; i < 3; i++) {
        ctown[i].x0 = core_towns[i].x0 + COREX; ctown[i].y0 = core_towns[i].y0 + COREY;
        ctown[i].x1 = core_towns[i].x1 + COREX; ctown[i].y1 = core_towns[i].y1 + COREY;
        ctown[i].base = core_towns[i].base; strcpy(ctown[i].name, core_towns[i].name); ctown[i].ok = 1;
    }
    nbraz = npick = nroad = nruin = 0;
    for (i = 0; i < 8; i++) { braz[nbraz].tx = core_braz[i][0] + COREX; braz[nbraz].ty = core_braz[i][1] + COREY; braz[nbraz].lit = i == 0; nbraz++; }
    for (i = 0; i < 13; i++) {
        Pick *p = &picks[npick++];
        p->tx = core_picks[i][0] + COREX; p->ty = core_picks[i][1] + COREY; p->type = core_picks[i][2]; p->amount = core_picks[i][3]; p->taken = 0;
    }
    /* borghi per cella */
    memset(ptown, 0, sizeof(ptown));
    for (cy = 0; cy < NCELL; cy++)
        for (cx = 0; cx < NCELL; cx++) {
            unsigned h = hash2(cx * 31 + 5, cy * 17 + 9);
            Town *t = &ptown[cy][cx];
            int w = 18 + (int)(h % 14), hh = 16 + (int)((h >> 8) % 12), x0, y0;
            float ccx, ccy;
            if (cx == 0 || cy == 0 || cx >= NCELL - 1 || cy >= NCELL - 1) continue;
            ccx = cx * CELL + CELL * 0.5f - (COREX + CORESZ / 2); ccy = cy * CELL + CELL * 0.5f - (COREY + CORESZ / 2);
            if (fabsf(ccx) < CELL * 1.1f && fabsf(ccy) < CELL * 1.1f) continue;   /* lascia spazio al cuore del mondo */
            if (h % 100 >= 52) continue;
            x0 = cx * CELL + 40 + (int)((h >> 4) % (unsigned)(CELL - 80 - w));
            y0 = cy * CELL + 40 + (int)((h >> 12) % (unsigned)(CELL - 80 - hh));
            t->x0 = x0; t->y0 = y0; t->x1 = x0 + w; t->y1 = y0 + hh;
            t->base = height_raw(x0 + w / 2, y0 + hh / 2);
            if (t->base < WL + 2.5f || t->base > 60) continue;
            town_name(t->name, h * 2654435761u);
            t->ok = 1;
        }
    /* strade tra borghi vicini, bracieri e oggetti nelle piazze */
    for (cy = 0; cy < NCELL; cy++)
        for (cx = 0; cx < NCELL; cx++) {
            Town *t = &ptown[cy][cx];
            int k, tcx, tcy;
            if (!t->ok) continue;
            tcx = (t->x0 + t->x1) / 2; tcy = (t->y0 + t->y1) / 2;
            if (nbraz < MAXBRAZ) { braz[nbraz].tx = tcx + 3; braz[nbraz].ty = tcy - 2; braz[nbraz].lit = 0; nbraz++; }
            add_pick(tcx - 4, tcy - 4, hash2(tcx, tcy));
            for (k = 1; k <= 2; k++) {
                if (cx + k < NCELL && ptown[cy][cx + k].ok) {
                    Town *u = &ptown[cy][cx + k];
                    add_road(t->x1 + 1, tcy, u->x0 - 1, (u->y0 + u->y1) / 2, hash2(cx * 3, cy * 5 + k));
                    break;
                }
            }
            for (k = 1; k <= 2; k++) {
                if (cy + k < NCELL && ptown[cy + k][cx].ok) {
                    Town *u = &ptown[cy + k][cx];
                    add_road(tcx, t->y1 + 1, (u->x0 + u->x1) / 2, u->y0 - 1, hash2(cx * 7, cy * 3 + k));
                    break;
                }
            }
        }
    /* il cuore del mondo si collega ai borghi piu' vicini nelle quattro direzioni */
    {
        static const int ex[4][2] = { {-2, 80}, {80, -2}, {CORESZ + 2, 80}, {80, CORESZ + 2} };
        for (i = 0; i < 4; i++) {
            int bx = ex[i][0] + COREX, by = ex[i][1] + COREY, best = -1;
            float bd = 1e9f;
            for (cy = 0; cy < NCELL; cy++)
                for (cx = 0; cx < NCELL; cx++) {
                    Town *t = &ptown[cy][cx];
                    float dx, dy, d;
                    if (!t->ok) continue;
                    dx = (t->x0 + t->x1) * 0.5f - bx; dy = (t->y0 + t->y1) * 0.5f - by;
                    if ((i == 0 && dx > -100) || (i == 2 && dx < 100) || (i == 1 && dy > -100) || (i == 3 && dy < 100)) continue;
                    d = dx * dx + dy * dy;
                    if (d < bd) { bd = d; best = cy * NCELL + cx; }
                }
            if (best >= 0) {
                Town *t = &ptown[best / NCELL][best % NCELL];
                add_road(bx, by, (t->x0 + t->x1) / 2, (t->y0 + t->y1) / 2, hash2(i, 77));
            }
        }
    }
    /* rovine sparse nelle celle senza borgo */
    for (cy = 1; cy < NCELL - 1; cy++)
        for (cx = 1; cx < NCELL - 1; cx++) {
            unsigned h = hash2(cx * 13 + 1, cy * 29 + 3);
            int w = 10 + (int)(h % 10), hh = 9 + (int)((h >> 8) % 10), x0, y0;
            float b;
            if (ptown[cy][cx].ok || h % 3 != 0 || nruin >= MAXRUIN) continue;
            x0 = cx * CELL + 40 + (int)((h >> 5) % (unsigned)(CELL - 80 - w));
            y0 = cy * CELL + 40 + (int)((h >> 13) % (unsigned)(CELL - 80 - hh));
            if (in_core_w(x0, y0) || in_core_w(x0 + w, y0 + hh)) continue;
            b = height_raw(x0 + w / 2, y0 + hh / 2);
            if (b < WL + 1 || b > 70) continue;
            pruins[nruin].x0 = x0; pruins[nruin].y0 = y0; pruins[nruin].x1 = x0 + w; pruins[nruin].y1 = y0 + hh; nruin++;
            add_pick(x0 + w / 2, y0 + hh / 2, h);
            if (h % 2 && nbraz < MAXBRAZ) { braz[nbraz].tx = x0 + w / 2 + 2; braz[nbraz].ty = y0 + hh / 2; braz[nbraz].lit = 0; nbraz++; }
        }
}

/* --- elementi che scrivono la mappa (tutti in coordinate del mondo, ritagliati) --- */
static void stamp(int cx, int cy, float r, int t)
{
    int x, y, R = (int)r + 1;
    for (y = cy - R; y <= cy + R; y++)
        for (x = cx - R; x <= cx + R; x++) {
            float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            int lx = x - WX0, ly = y - WY0;
            if (d > r || !inclip(lx, ly) || in_town(lx, ly)) continue;
            if (x < 3 || y < 3 || x >= WORLDT - 3 || y >= WORLDT - 3) continue;
            if (map[ly][lx] == T_WATER && t != T_PIER) continue;
            map[ly][lx] = (unsigned char)t;
        }
}

static void path_w(const int *xs, const int *ys, int n)
{
    int i;
    for (i = 0; i + 1 < n; i++) {
        float x0 = (float)xs[i], y0 = (float)ys[i], x1 = (float)xs[i + 1], y1 = (float)ys[i + 1];
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + 0.001f, t;
        float mnx = (x0 < x1 ? x0 : x1) - 4 - WX0, mxx = (x0 > x1 ? x0 : x1) + 4 - WX0;
        float mny = (y0 < y1 ? y0 : y1) - 4 - WY0, mxy = (y0 > y1 ? y0 : y1) + 4 - WY0;
        if (mxx < cl_x0 || mnx > cl_x1 || mxy < cl_y0 || mny > cl_y1) continue;
        for (t = 0; t <= len; t += 0.5f) {
            float k = t / len, px = x0 + (x1 - x0) * k, py = y0 + (y1 - y0) * k;
            float wob;
            if (px - WX0 < cl_x0 - 3 || px - WX0 > cl_x1 + 3 || py - WY0 < cl_y0 - 3 || py - WY0 > cl_y1 + 3) continue;
            wob = (vnoise2(px / 4.0f, py / 4.0f, 1024, 1024, 151) - 0.5f) * 3;
            stamp((int)(px + wob * (y1 - y0) / len), (int)(py - wob * (x1 - x0) / len), 1.5f, T_DIRT);
        }
    }
}

static void path_k(const int (*pts)[2], int n)
{
    int xs[8], ys[8], i;
    for (i = 0; i < n; i++) { xs[i] = pts[i][0] + COREX; ys[i] = pts[i][1] + COREY; }
    path_w(xs, ys, n);
}

static void clear_w(int cx, int cy, int r)
{
    int x, y;
    for (y = cy - r; y <= cy + r; y++)
        for (x = cx - r; x <= cx + r; x++) {
            int lx = x - WX0, ly = y - WY0;
            if (inclip(lx, ly) && !in_town(lx, ly) && tile_solid(map[ly][lx]) && map[ly][lx] != T_WATER) map[ly][lx] = T_GRASS;
        }
}

static void ruin_rect_w(int x0, int y0, int x1, int y1, int seed)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int edge = x == x0 || x == x1 || y == y0 || y == y1;
            if (!edge) { if (get_tw(x, y) != T_WATER) setw(x, y, T_DIRT); continue; }
            setw(x, y, hash2(x * 3 + seed, y) % 5 == 0 ? T_DIRT : T_RUIN);
        }
    setw((x0 + x1) / 2, y1, T_DIRT); setw((x0 + x1) / 2 + 1, y1, T_DIRT);
}

static void build_town_w(const Town *t)
{
    int cx = (t->x0 + t->x1) / 2, cy = (t->y0 + t->y1) / 2, k, x, y;
    if (t->x1 - WX0 < cl_x0 || t->x0 - WX0 > cl_x1 || t->y1 - WY0 < cl_y0 || t->y0 - WY0 > cl_y1) return;
    fillW(t->x0, t->y0, t->x1, t->y1, T_WALL);
    fillW(t->x0, cy - 1, t->x1, cy + 1, T_FLOOR);
    fillW(cx - 1, t->y0, cx + 1, t->y1, T_FLOOR);
    for (k = 7; cy - k > t->y0 + 1 || cy + k < t->y1 - 1; k += 7) {
        if (cy - k > t->y0 + 1) fillW(t->x0 + 1, cy - k, t->x1 - 1, cy - k + 1, T_FLOOR);
        if (cy + k < t->y1 - 1) fillW(t->x0 + 1, cy + k - 1, t->x1 - 1, cy + k, T_FLOOR);
    }
    for (k = 7; cx - k > t->x0 + 1 || cx + k < t->x1 - 1; k += 7) {
        if (cx - k > t->x0 + 1) fillW(cx - k, t->y0 + 1, cx - k + 1, t->y1 - 1, T_FLOOR);
        if (cx + k < t->x1 - 1) fillW(cx + k - 1, t->y0 + 1, cx + k, t->y1 - 1, T_FLOOR);
    }
    fillW(cx - 5, cy - 4, cx + 5, cy + 4, T_FLOOR);
    setw(cx, cy, T_WELL);
    setw(cx - 5, cy - 4, T_LAMP); setw(cx + 5, cy - 4, T_LAMP); setw(cx - 5, cy + 4, T_LAMP); setw(cx + 5, cy + 4, T_LAMP);
    setw(cx - 3, cy + 3, T_TREE); setw(cx + 3, cy - 3, T_TREE);
    for (x = t->x0 + 2; x <= t->x1 - 2; x += 6) if (get_tw(x, cy - 1) == T_FLOOR && (x < cx - 5 || x > cx + 5)) setw(x, cy - 1, T_LAMP);
    for (y = t->y0 + 2; y <= t->y1 - 2; y += 6) if (get_tw(cx + 1, y) == T_FLOOR && (y < cy - 4 || y > cy + 4)) setw(cx + 1, y, T_LAMP);
    for (y = t->y0 + 1; y < t->y1; y++)
        for (x = t->x0 + 1; x < t->x1; x++) {
            int nearw = get_tw(x, y - 1) == T_WALL || get_tw(x, y + 1) == T_WALL || get_tw(x - 1, y) == T_WALL || get_tw(x + 1, y) == T_WALL;
            if (get_tw(x, y) != T_FLOOR || !nearw) continue;
            if (abs_i(x - cx) <= 1 || abs_i(y - cy) <= 1) continue;
            if (x >= cx - 5 && x <= cx + 5 && y >= cy - 4 && y <= cy + 4) continue;
            if (hash2(x * 5, y * 3) % 17 == 0) setw(x, y, T_CRATE);
        }
}

/* --- tipo di base di una casella --- */
static int base_tile(int wx, int wy, float a, float b, float c, float d)
{
    float mx = a, mn = a;
    unsigned h = hash2(wx, wy);
    int kx = wx - COREX, ky = wy - COREY;
    if (b > mx) mx = b; if (c > mx) mx = c; if (d > mx) mx = d;
    if (b < mn) mn = b; if (c < mn) mn = c; if (d < mn) mn = d;
    if (mx < WL - 0.3f) return T_WATER;
    if (mx - mn > 8.5f || wx < 3 || wy < 3 || wx >= WORLDT - 3 || wy >= WORLDT - 3 || mn > 80) return T_ROCK;
    if (in_core_w(wx, wy)) {
        if (kx < 38) {
            float f = vnoise2(kx / 3.0f, ky / 3.0f, 64, 64, 107);
            if (f > 0.42f && h % 3 != 0) return T_PINE;
            if (h % 29 == 0) return T_TREE;
            if (h % 37 == 0) return T_BOULDER;
        } else if (ky < 58) {
            if (h % 23 == 0) return T_TREE;
            if (h % 41 == 0) return T_BOULDER;
        } else if (kx > 137) {
            if (h % 31 == 0) return T_BOULDER;
            if (h % 47 == 0) return T_RUIN;
        } else {
            if (h % 53 == 0) return T_BOULDER;
            if (h % 61 == 0) return T_TREE;
            if (h % 19 == 0 && ky > 112) return T_PINE;
        }
        return T_GRASS;
    }
    {
        float forest = vnoise2(wx / 90.0f, wy / 90.0f, 1024, 1024, 211), dead = vnoise2(wx / 150.0f, wy / 150.0f, 1024, 1024, 213);
        if (forest > 0.58f) {
            float f = vnoise2(wx / 3.0f, wy / 3.0f, 1024, 1024, 107);
            if (f > 0.40f && h % 3 != 0) return T_PINE;
            if (h % 29 == 0) return T_TREE;
            if (h % 37 == 0) return T_BOULDER;
        } else if (dead > 0.62f) {
            if (h % 9 == 0) return T_TREE;
            if (h % 17 == 0) return T_BOULDER;
            if (h % 97 == 0) return T_RUIN;
        } else if (mn > 40) {
            if (h % 11 == 0) return T_BOULDER;
            if (h % 43 == 0) return T_PINE;
        } else {
            if (h % 53 == 0) return T_BOULDER;
            if (h % 61 == 0) return T_TREE;
            if (h % 71 == 0) return T_PINE;
        }
    }
    return T_GRASS;
}

/* --- genera una zona della finestra (coordinate locali, estremi inclusi) --- */
static void build_region(int x0, int y0, int x1, int y1)
{
    int x, y, i;
    cl_x0 = x0; cl_y0 = y0; cl_x1 = x1; cl_y1 = y1;
    /* 1. altezze agli angoli */
    for (y = y0; y <= y1 + 1 && y <= MH; y++)
        for (x = x0; x <= x1 + 1 && x <= MW; x++)
            hgt[y][x] = height_at(x + WX0, y + WY0);
    /* 2. terreno */
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            map[y][x] = (unsigned char)base_tile(x + WX0, y + WY0, hgt[y][x], hgt[y][x + 1], hgt[y + 1][x + 1], hgt[y + 1][x]);
    /* 3. luoghi del cuore del mondo */
    if (!(x1 + WX0 < COREX - 4 || x0 + WX0 > COREX + CORESZ + 4 || y1 + WY0 < COREY - 4 || y0 + WY0 > COREY + CORESZ + 4)) {
        for (y = 18; y <= 36; y += 3)
            for (x = 66; x <= 106; x += 3)
                if (hash2(x + COREX, y + COREY) % 4 && get_tw(x + COREX, y + COREY) != T_ROCK && !(x >= 84 && x <= 89)) SK(x, y, T_GRAVE);
        ruin_rect_w(82 + COREX, 6 + COREY, 94 + COREX, 17 + COREY, 1);
        SK(85, 11, T_RUIN); SK(91, 11, T_RUIN);
        ruin_rect_w(140 + COREX, 44 + COREY, 154 + COREX, 58 + COREY, 2);
        ruin_rect_w(141 + COREX, 76 + COREY, 155 + COREX, 96 + COREY, 3);
        ruin_rect_w(139 + COREX, 104 + COREY, 152 + COREX, 124 + COREY, 4);
        for (i = 0; i < 6; i++) { SK(144, 80 + i * 3, T_RUIN); SK(152, 80 + i * 3, T_RUIN); }
        for (y = 117; y <= 126; y++) { SK(85, y, T_PIER); SK(86, y, T_PIER); }
        FK(80, 126, 91, 134, T_PIER);
        {
            static const int pw[][2] = { {39, 67}, {31, 70}, {18, 80} };
            static const int pn[][2] = { {81, 59}, {83, 46}, {87, 33}, {88, 18} };
            static const int ps[][2] = { {82, 110}, {84, 114}, {85, 117} };
            static const int r1[][2] = { {39, 67}, {35, 57}, {60, 55}, {81, 59}, {110, 55}, {136, 58}, {147, 66}, {148, 76} };
            static const int r2[][2] = { {82, 110}, {110, 115}, {137, 113}, {146, 104}, {148, 97} };
            static const int r3[][2] = { {18, 80}, {22, 112}, {50, 116}, {82, 110} };
            static const int r4[][2] = { {18, 80}, {12, 48}, {30, 32}, {60, 30}, {84, 30} };
            static const int r5[][2] = { {136, 58}, {147, 59} };
            static const int r6[][2] = { {18, 80}, {-2, 80} };
            static const int r7[][2] = { {84, 30}, {82, 4}, {80, -2} };
            static const int r8[][2] = { {148, 76}, {158, 80}, {162, 80} };
            static const int r9[][2] = { {51, 135}, {60, 150}, {80, 162} };
            path_k(pw, 3); path_k(pn, 4); path_k(ps, 3); path_k(r1, 8); path_k(r2, 5); path_k(r3, 4); path_k(r4, 5); path_k(r5, 2);
            path_k(r6, 2); path_k(r7, 3); path_k(r8, 3); path_k(r9, 3);
        }
        clear_w(COREX + 18, COREY + 100, 9);   /* radura della Matriarca */
        for (i = 0; i < 3; i++) build_town_w(&ctown[i]);
        {
            static const int a1[][2] = { {61, 125}, {70, 119}, {80, 116} };
            static const int a2[][2] = { {41, 125}, {30, 118}, {22, 112} };
            static const int a3[][2] = { {51, 135}, {52, 146} };
            static const int b1[][2] = { {116, 37}, {113, 46}, {112, 54} };
            static const int b2[][2] = { {103, 25}, {96, 19}, {94, 14} };
            static const int c1[][2] = { {18, 41}, {14, 46}, {12, 48} };
            static const int c2[][2] = { {29, 31}, {33, 31} };
            path_k(a1, 3); path_k(a2, 3); path_k(a3, 2); path_k(b1, 3); path_k(b2, 3); path_k(c1, 3); path_k(c2, 2);
        }
        /* Velmora */
        FK(CX0, CY0, CX0 + CITYW - 1, CY0 + CITYH - 1, T_WALL);
#define FT(a, b, c, d, t) FK(CX0 + (a), CY0 + (b), CX0 + (c), CY0 + (d), t)
        FT(2, 2, 18, 14, T_FLOOR); FT(19, 7, 29, 9, T_FLOOR); FT(30, 2, 52, 20, T_FLOOR); FT(40, 21, 43, 29, T_FLOOR);
        FT(24, 30, 60, 42, T_FLOOR); FT(61, 35, 65, 37, T_FLOOR); FT(66, 35, 66, 37, T_GATE); FT(67, 26, 90, 46, T_FLOOR);
        FT(0, 6, 1, 8, T_FLOOR); FT(40, 0, 42, 1, T_FLOOR); FT(41, 43, 43, 49, T_FLOOR);
        FT(35, 5, 36, 6, T_WALL); FT(45, 5, 46, 6, T_WALL); FT(35, 15, 36, 16, T_WALL); FT(45, 15, 46, 16, T_WALL);
        FT(32, 33, 33, 34, T_WALL); FT(51, 33, 52, 34, T_WALL); FT(32, 39, 33, 40, T_WALL); FT(51, 39, 52, 40, T_WALL);
        FT(71, 30, 72, 31, T_WALL); FT(85, 30, 86, 31, T_WALL); FT(71, 41, 72, 42, T_WALL); FT(85, 41, 86, 42, T_WALL);
        FT(9, 10, 10, 11, T_WALL);
#undef FT
        {
            static const int lamps[][2] = { {3,3},{17,3},{3,13},{17,13},{24,7},{31,3},{51,3},{31,19},{51,19},{41,11},{40,22},{43,28},{25,31},{59,31},{25,41},{59,41},{63,35} };
            static const int graves[] = { 27, 30, 36, 47, 53, 56 };
            static const int crates[][2] = { {33,10},{34,10},{49,17},{50,8},{15,11},{4,9},{58,41},{24,32} };
            static const int fires[][2] = { {68,27},{89,27},{68,45},{89,45} };
            for (i = 0; i < (int)(sizeof(lamps) / sizeof(lamps[0])); i++) SK(CX0 + lamps[i][0], CY0 + lamps[i][1], T_LAMP);
            for (i = 0; i < 6; i++) { SK(CX0 + graves[i], CY0 + 37, T_GRAVE); SK(CX0 + graves[i] + 1, CY0 + 32, T_GRAVE); }
            for (i = 0; i < 8; i++) SK(CX0 + crates[i][0], CY0 + crates[i][1], T_CRATE);
            for (i = 0; i < 4; i++) SK(CX0 + fires[i][0], CY0 + fires[i][1], T_FIRE);
            SK(CX0 + 26, CY0 + 36, T_TREE); SK(CX0 + 58, CY0 + 36, T_TREE); SK(CX0 + 12, CY0 + 12, T_TREE);
        }
        SK(37, 66, T_LAMP); SK(80, 57, T_LAMP); SK(84, 112, T_LAMP);
        for (i = 0; i < NSPAWN; i++) if (!in_city_w(spawns[i][0] + COREX, spawns[i][1] + COREY)) clear_w(spawns[i][0] + COREX, spawns[i][1] + COREY, 1);
    }
    /* 4. mondo procedurale: rovine, strade, borghi */
    for (i = 0; i < nruin; i++) {
        Ruin *r = &pruins[i];
        if (r->x1 - WX0 < cl_x0 || r->x0 - WX0 > cl_x1 || r->y1 - WY0 < cl_y0 || r->y0 - WY0 > cl_y1) continue;
        ruin_rect_w(r->x0, r->y0, r->x1, r->y1, i + 10);
    }
    for (i = 0; i < nroad; i++) {
        Road *r = &roads[i];
        if (r->bx1 + 4 - WX0 < cl_x0 || r->bx0 - 4 - WX0 > cl_x1 || r->by1 + 4 - WY0 < cl_y0 || r->by0 - 4 - WY0 > cl_y1) continue;
        path_w(r->x, r->y, r->n);
    }
    {
        int cx0 = (x0 + WX0) / CELL - 1, cx1 = (x1 + WX0) / CELL + 1, cy0 = (y0 + WY0) / CELL - 1, cy1 = (y1 + WY0) / CELL + 1, cx, cy;
        for (cy = cy0; cy <= cy1; cy++)
            for (cx = cx0; cx <= cx1; cx++)
                if (cx >= 0 && cy >= 0 && cx < NCELL && cy < NCELL && ptown[cy][cx].ok) build_town_w(&ptown[cy][cx]);
    }
    /* 5. punti importanti sempre raggiungibili */
    for (i = 0; i < nbraz; i++) {
        int lx = braz[i].tx - WX0, ly = braz[i].ty - WY0;
        if (lx < x0 - 3 || lx > x1 + 3 || ly < y0 - 3 || ly > y1 + 3) continue;
        if (!in_town(lx, ly)) clear_w(braz[i].tx, braz[i].ty, 2);
        if (get_tw(braz[i].tx, braz[i].ty) == T_WATER) setw(braz[i].tx, braz[i].ty, T_PIER);
    }
    for (i = 0; i < npick; i++) {
        int lx = picks[i].tx - WX0, ly = picks[i].ty - WY0;
        if (lx < x0 - 2 || lx > x1 + 2 || ly < y0 - 2 || ly > y1 + 2) continue;
        if (!in_city(lx, ly)) clear_w(picks[i].tx, picks[i].ty, 1);
    }
    /* 6. ombreggiatura dei pendii */
    for (y = y0 > 0 ? y0 - 1 : 0; y <= y1 + 2 && y <= MH; y++)
        for (x = x0 > 0 ? x0 - 1 : 0; x <= x1 + 2 && x <= MW; x++) {
            float hx = hgt[y][x < MW ? x + 1 : x] - hgt[y][x > 0 ? x - 1 : x];
            float hz = hgt[y < MH ? y + 1 : y][x] - hgt[y > 0 ? y - 1 : y][x];
            float nx = -hx / (2 * TS), nz = -hz / (2 * TS), ny = 1, nl = sqrtf(nx * nx + ny * ny + nz * nz);
            float dl = (nx * 0.4f + ny * 0.8f + nz * -0.45f) / nl;
            hshade[y][x] = 0.55f + 0.5f * (dl > 0 ? dl : 0);
        }
}

static void build_map(void) { build_region(0, 0, MW - 1, MH - 1); }

/* altezza del terreno in un punto (coordinate locali in unita') */
static float ground_h(float x, float z)
{
    float fx = x / TS, fz = z / TS, ax, az;
    int tx = (int)floorf(fx), tz = (int)floorf(fz);
    if (tx < 0 || tz < 0 || tx >= MW || tz >= MH) return 0;
    if (map[tz][tx] == T_PIER) return WL + 0.9f;
    ax = fx - tx; az = fz - tz;
    return (hgt[tz][tx] * (1 - ax) + hgt[tz][tx + 1] * ax) * (1 - az) + (hgt[tz + 1][tx] * (1 - ax) + hgt[tz + 1][tx + 1] * ax) * az;
}

/* sposta la finestra: copia la parte che resta, genera solo le strisce nuove */
static unsigned char shift_tmp[(MH + 1) * (MW + 1) * 4];
static void shift_grid(void *base, int esz, int w, int h, int dx, int dy)
{
    unsigned char *b = (unsigned char *)base;
    int y;
    memcpy(shift_tmp, b, (size_t)w * h * esz);
    for (y = 0; y < h; y++) {
        int sy = y + dy, x0 = dx > 0 ? 0 : -dx, x1 = dx > 0 ? w - dx : w;
        if (sy < 0 || sy >= h || x1 <= x0) continue;
        memcpy(b + ((size_t)y * w + x0) * esz, shift_tmp + ((size_t)sy * w + x0 + dx) * esz, (size_t)(x1 - x0) * esz);
    }
}

static int collides(float x, float y, float r)
{
    int tx0 = (int)floorf((x - r) / TS), tx1 = (int)floorf((x + r) / TS);
    int ty0 = (int)floorf((y - r) / TS), ty1 = (int)floorf((y + r) / TS);
    int tx, ty;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++) {
            float cx, cy, dx, dy;
            if (!solid(tx, ty)) continue;
            cx = x < tx * TS ? tx * TS : (x > tx * TS + TS ? tx * TS + TS : x);
            cy = y < ty * TS ? ty * TS : (y > ty * TS + TS ? ty * TS + TS : y);
            dx = x - cx; dy = y - cy;
            if (dx * dx + dy * dy < r * r) return 1;
        }
    return 0;
}

/* ritorna 1 se il movimento e' stato bloccato */
static int move_circle(float *x, float *y, float r, float dx, float dy)
{
    int blocked = 0;
    if (dx != 0) { if (!collides(*x + dx, *y, r)) *x += dx; else blocked = 1; }
    if (dy != 0) { if (!collides(*x, *y + dy, r)) *y += dy; else blocked = 1; }
    return blocked;
}

/* ------------------------------------------------------------------ */
/* particelle e messaggi                                               */
/* ------------------------------------------------------------------ */
static void spawn_parts(float x, float y, int n, uint32_t col, float spd)
{
    int i;
    for (i = 0; i < MAXPART && n > 0; i++)
        if (parts[i].life <= 0) {
            float a = frnd() * 2 * PI, s = spd * (0.3f + frnd());
            parts[i].x = x; parts[i].y = y;
            parts[i].vx = cosf(a) * s; parts[i].vy = sinf(a) * s;
            parts[i].life = 14 + (int)(rnd() % 14);
            parts[i].col = col;
            n--;
        }
}

static void set_msg(const char *s, int t)
{
    strncpy(msg, s, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgT = t;
}

/* ------------------------------------------------------------------ */
/* reset del mondo                                                     */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* nemici nella finestra: fissi nel cuore del mondo, generati altrove  */
/* ------------------------------------------------------------------ */
#define MAXKILLED 512
static int killed[MAXKILLED];
static int nkilled;

static int is_killed(int id)
{
    int i;
    for (i = 0; i < nkilled; i++) if (killed[i] == id) return 1;
    return 0;
}
static void mark_killed(int id)
{
    if (nkilled < MAXKILLED) killed[nkilled++] = id;
    else killed[(unsigned)id % MAXKILLED] = id;
}

static int enemy_present(int id)
{
    int i;
    for (i = 0; i < NE; i++) if (E[i].alive && E[i].id == id) return 1;
    return 0;
}

static void add_enemy(int id, int kind, int lx, int ly)
{
    int i;
    Enemy *e = 0;
    if (lx < 1 || ly < 1 || lx >= MW - 1 || ly >= MH - 1) return;
    if (is_killed(id) || enemy_present(id) || solid(lx, ly)) return;
    for (i = 0; i < NE; i++) if (!E[i].alive) { e = &E[i]; break; }
    if (!e) { if (NE >= MAXE) return; e = &E[NE++]; }
    memset(e, 0, sizeof(*e));
    e->id = id;
    e->x = lx * TS + TS / 2; e->y = ly * TS + TS / 2;
    e->kind = kind; e->hp = EK[kind].hp;
    e->alive = 1; e->state = ES_IDLE;
    e->fx = 0; e->fy = 1; e->hitBy = -1;
}

/* tipo di nemico adatto al luogo */
static int biome_kind(int wx, int wy, unsigned h)
{
    float forest = vnoise2(wx / 90.0f, wy / 90.0f, 1024, 1024, 211), dead = vnoise2(wx / 150.0f, wy / 150.0f, 1024, 1024, 213);
    float hh = height_raw(wx, wy);
    if (hh > 38 && h % 5 == 0) return 3;
    if (forest > 0.58f) return h % 4 ? 2 : 0;
    if (dead > 0.62f) return h % 3 ? 0 : 1;
    if (h % 19 == 0) return 3;
    return (int)(h % 3) == 0 ? 2 : (h % 4 == 0 ? 1 : 0);
}

static void spawn_region(int x0, int y0, int x1, int y1)
{
    int i, bx, by;
    /* nemici fissi del cuore del mondo */
    for (i = 0; i < NSPAWN; i++) {
        int lx = spawns[i][0] + COREX - WX0, ly = spawns[i][1] + COREY - WY0;
        if (lx < x0 || lx > x1 || ly < y0 || ly > y1) continue;
        add_enemy(-1 - i, spawns[i][2], lx, ly);
    }
    /* guardie dei borghi procedurali */
    {
        int cx0 = (x0 + WX0) / CELL - 1, cx1 = (x1 + WX0) / CELL + 1, cy0 = (y0 + WY0) / CELL - 1, cy1 = (y1 + WY0) / CELL + 1, cx, cy;
        for (cy = cy0; cy <= cy1; cy++)
            for (cx = cx0; cx <= cx1; cx++) {
                const Town *t;
                int tcx, tcy, k;
                static const int off[5][2] = { {-7, 0}, {7, 0}, {0, -6}, {0, 6}, {-3, 2} };
                if (cx < 0 || cy < 0 || cx >= NCELL || cy >= NCELL || !ptown[cy][cx].ok) continue;
                t = &ptown[cy][cx];
                tcx = (t->x0 + t->x1) / 2; tcy = (t->y0 + t->y1) / 2;
                for (k = 0; k < 5; k++) {
                    int wx = tcx + off[k][0], wy = tcy + off[k][1], lx = wx - WX0, ly = wy - WY0;
                    unsigned h = hash2(wx, wy);
                    if (lx < x0 || lx > x1 || ly < y0 || ly > y1) continue;
                    add_enemy(wy * WORLDT + wx, (int)(h % 5 == 0 ? 1 : (h % 3 == 0 ? 2 : 0)), lx, ly);
                }
            }
    }
    /* creature selvatiche: una ogni tanto, a blocchi di 24 caselle */
    for (by = (y0 + WY0) / 24; by <= (y1 + WY0) / 24; by++)
        for (bx = (x0 + WX0) / 24; bx <= (x1 + WX0) / 24; bx++) {
            unsigned h = hash2(bx * 7 + 3, by * 11 + 1);
            int wx, wy, lx, ly;
            if (h % 4 != 0) continue;
            wx = bx * 24 + 4 + (int)(h % 16); wy = by * 24 + 4 + (int)((h >> 8) % 16);
            lx = wx - WX0; ly = wy - WY0;
            if (lx < x0 || lx > x1 || ly < y0 || ly > y1) continue;
            if (in_core_w(wx, wy) || in_town(lx, ly)) continue;
            add_enemy(wy * WORLDT + wx, biome_kind(wx, wy, h), lx, ly);
            if (h % 12 == 0) add_enemy(wy * WORLDT + wx + 2, biome_kind(wx, wy, h), lx + 2, ly + 1);   /* a volte in coppia */
        }
}

static void reset_enemies(void)
{
    nkilled = 0;
    NE = 0;
    memset(E, 0, sizeof(E));
    spawn_region(0, 0, MW - 1, MH - 1);
}

static void reset_boss(void)
{
    int keepDead = Bs.dead;
    memset(&Bs, 0, sizeof(Bs));
    Bs.dead = keepDead;
    Bs.x = (CX0 + 82 + COREX - WX0) * TS; Bs.y = (CY0 + 36 + COREY - WY0) * TS + 8;
    Bs.fx = -1; Bs.fy = 0;
    Bs.maxhp = 1000; Bs.hp = Bs.maxhp;
    Bs.state = keepDead ? BS_DEAD : BS_OFF;
}

/* sposta la finestra di (dx,dy) caselle: dati, entita' e nuove zone */
static void move_window(int nx0, int ny0)
{
    int dx, dy, i;
    float ox, oy;
    if (nx0 < 0) nx0 = 0;
    if (ny0 < 0) ny0 = 0;
    if (nx0 > WORLDT - MW) nx0 = WORLDT - MW;
    if (ny0 > WORLDT - MH) ny0 = WORLDT - MH;
    dx = nx0 - WX0; dy = ny0 - WY0;
    if (!dx && !dy) return;
    ox = (float)(dx * TS); oy = (float)(dy * TS);
    if (abs_i(dx) >= MW || abs_i(dy) >= MH) {
        WX0 = nx0; WY0 = ny0;
        build_map();
    } else {
        shift_grid(map, 1, MW, MH, dx, dy);
        shift_grid(hgt, sizeof(float), MW + 1, MH + 1, dx, dy);
        shift_grid(hshade, sizeof(float), MW + 1, MH + 1, dx, dy);
        WX0 = nx0; WY0 = ny0;
        if (dx > 0) build_region(MW - dx, 0, MW - 1, MH - 1);
        if (dx < 0) build_region(0, 0, -dx - 1, MH - 1);
        if (dy > 0) build_region(0, MH - dy, MW - 1, MH - 1);
        if (dy < 0) build_region(0, 0, MW - 1, -dy - 1);
    }
    /* tutto cio' che vive nella finestra si sposta con lei */
    P.x -= ox; P.y -= oy; camX -= ox; camZ -= oy;
    Bs.x -= ox; Bs.y -= oy; Bs.sx -= ox; Bs.sy -= oy; Bs.tx -= ox; Bs.ty -= oy;
    drop.x -= ox; drop.y -= oy;
    for (i = 0; i < MAXPART; i++) { parts[i].x -= ox; parts[i].y -= oy; }
    for (i = 0; i < MAXBLOOD; i++) { blood[i].x -= ox; blood[i].y -= oy; }
    rb_shift(ox, oy);
    for (i = 0; i < NE; i++) {
        if (!E[i].alive) continue;
        E[i].x -= ox; E[i].y -= oy;
        if (E[i].x < TS || E[i].y < TS || E[i].x > (MW - 1) * TS || E[i].y > (MH - 1) * TS) {
            E[i].alive = 0;
            if (P.lock == i) P.lock = -1;
        }
    }
    if (dx > 0) spawn_region(MW - dx, 0, MW - 1, MH - 1);
    if (dx < 0) spawn_region(0, 0, -dx - 1, MH - 1);
    if (dy > 0) spawn_region(0, MH - dy, MW - 1, MH - 1);
    if (dy < 0) spawn_region(0, 0, MW - 1, -dy - 1);
    build_lights();
}

/* tiene il giocatore vicino al centro della finestra */
static void keep_centered(void)
{
    int px = (int)(P.x / TS), py = (int)(P.y / TS);
    if (px < 80 || px > MW - 80 || py < 80 || py > MH - 80)
        move_window(WX0 + px - MW / 2, WY0 + py - MH / 2);
}

static void respawn(void)
{
    Brazier *b = &braz[cpB];
    int lx = b->tx - WX0, ly = b->ty - WY0;
    if (lx < 64 || lx > MW - 64 || ly < 64 || ly > MH - 64) {
        WX0 = b->tx - MW / 2; WY0 = b->ty - MH / 2;
        if (WX0 < 0) WX0 = 0;
        if (WY0 < 0) WY0 = 0;
        if (WX0 > WORLDT - MW) WX0 = WORLDT - MW;
        if (WY0 > WORLDT - MH) WY0 = WORLDT - MH;
        build_map();
    }
    P.x = (b->tx - WX0) * TS + 8; P.y = (b->ty - WY0) * TS + 30;
    if (solid((int)(P.x / TS), (int)(P.y / TS))) P.y -= 44;
    P.hp = P.hpGhost = (float)maxhp();
    P.st = (float)maxst();
    P.flasks = P.maxFlask;
    P.state = PS_FREE; P.timer = 0; P.iframes = 30;
    P.fx = 0; P.fy = 1; P.kx = P.ky = 0;
    P.lock = -1; P.buf = 0; P.bufT = 0;
    camX = P.x; camZ = P.y; camDistS = 62.0f; camGY = ground_h(P.x, P.y);
    gate_closed = embers < 4;
    reset_enemies();
    reset_boss();
    rb_reset_all();
    memset(parts, 0, sizeof(parts));
    memset(blood, 0, sizeof(blood));
    build_lights();
}

void game_init(void)
{
    memset(&P, 0, sizeof(P));
    memset(&Bs, 0, sizeof(Bs));
    init_world();
    cpB = 0;
    WX0 = braz[0].tx - MW / 2; WY0 = braz[0].ty - MH / 2;
    build_map();
    gen_textures();
    P.vig = P.end = P.str = 3;
    P.lvl = 1;
    P.maxFlask = 4;
    P.region = 0;
    P.frags = 0;
    P.atkId = 1;
    drop.active = 0;
    respawn();
    mode = M_TITLE;
    frame = 0;
    hasSave = save_exists();
    titleSel = 0;
}

/* ------------------------------------------------------------------ */
/* danno al giocatore                                                  */
/* ------------------------------------------------------------------ */
static void hurt_player(int dmg, float sx, float sy)
{
    float dx, dy, d;
    if (P.iframes > 0 || P.state == PS_DEAD || mode != M_PLAY) return;
    P.hp -= dmg;
    shake = 8;
    spawn_parts(P.x, P.y, 12, RGB(130, 10, 18), 2.0f);
    add_blood(P.x + frnd() * 6 - 3, P.y + frnd() * 6 - 3, 3 + frnd() * 3);
    dx = P.x - sx; dy = P.y - sy; d = sqrtf(dx * dx + dy * dy) + 0.001f;
    if (P.hp <= 0) {
        P.hp = 0;
        P.state = PS_DEAD;
        mode = M_DEAD;
        deadT = 170;
        /* i frammenti restano dove sei caduto; quelli vecchi vanno persi */
        if (P.frags > 0) { drop.active = 1; drop.x = P.x; drop.y = P.y; drop.amount = P.frags; }
        else drop.active = 0;
        P.frags = 0;
        deaths++;
        sfx(SFX_DIE);
        save_game();
        return;
    }
    P.state = PS_HURT; P.timer = 0;
    sfx(SFX_HURT);
    P.kx = dx / d * 3.0f; P.ky = dy / d * 3.0f;
    P.iframes = 22;
}

/* ------------------------------------------------------------------ */
/* attacchi del giocatore                                              */
/* ------------------------------------------------------------------ */
static const int AW[2] = { 7, 18 }, AA[2] = { 6, 7 }, AR[2] = { 13, 20 };

static void player_hits(void)
{
    float range = P.atk ? 38.0f : 31.0f;
    int dmg = P.atk ? dmgH() : dmgL();
    int i;
    rb_player_hits(range, dmg);
    for (i = 0; i < NE; i++) {
        Enemy *e = &E[i];
        float dx, dy, d;
        if (!e->alive || e->state == ES_DEAD || e->hitBy == P.atkId) continue;
        dx = e->x - P.x; dy = e->y - P.y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (d < range + EK[e->kind].r && (dx * P.fx + dy * P.fy) / d > 0.25f) {
            e->hitBy = P.atkId;
            e->hp -= dmg;
            e->flash = 6;
            spawn_parts(e->x, e->y, 10, RGB(140, 12, 20), 2.2f);
            sfx(SFX_HIT);
            add_blood(e->x + dx / d * 6, e->y + dy / d * 6, 2.5f + frnd() * 3);
            move_circle(&e->x, &e->y, EK[e->kind].r, dx / d * 4, dy / d * 4);
            if (e->hp <= 0) {
                e->state = ES_DEAD; e->deadT = 40;
                mark_killed(e->id);
                sfx(SFX_EDEATH);
                P.frags += EK[e->kind].reward;
                if (P.lock == i) P.lock = -1;
                spawn_parts(e->x, e->y, 16, RGB(110, 10, 16), 1.6f);
                add_blood(e->x, e->y, 7);
            } else {
                if ((!EK[e->kind].poise || P.atk == 1) && e->state != ES_STRIKE) {
                    e->state = ES_STAG; e->timer = P.atk ? 22 : 14;
                } else if (e->state == ES_IDLE) e->state = ES_CHASE;
            }
        }
    }
    if (Bs.state != BS_OFF && Bs.state != BS_DEAD && !Bs.hitDone && Bs.z < 6) {
        float dx = Bs.x - P.x, dy = Bs.y - P.y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (d < range + 16 && (dx * P.fx + dy * P.fy) / d > 0.2f) {
            Bs.hitDone = 1;
            Bs.hp -= dmg;
            Bs.flash = 6;
            spawn_parts(Bs.x - dx / d * 14, Bs.y - dy / d * 14, 10, RGB(255, 150, 60), 2.4f);
            add_blood(Bs.x - dx / d * 12, Bs.y - dy / d * 12, 3 + frnd() * 3);
            if (Bs.hp <= 0) {
                Bs.hp = 0; Bs.state = BS_DEAD; Bs.dead = 1; Bs.timer = 150;
                P.frags += 2500;
                gate_closed = 0;
                sfx(SFX_VICTORY);
                if (P.lock == 100) P.lock = -1;
                shake = 30;
                spawn_parts(Bs.x, Bs.y, 60, RGB(255, 140, 40), 3.0f);
            }
        }
    }
}

static int find_lock_target(void)
{
    float best = 170 * 170;
    int i, idx = -1;
    for (i = 0; i < NE; i++) {
        float dx, dy, d2;
        if (!E[i].alive || E[i].state == ES_DEAD) continue;
        dx = E[i].x - P.x; dy = E[i].y - P.y; d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; idx = i; }
    }
    if (Bs.state != BS_OFF && Bs.state != BS_DEAD) {
        float dx = Bs.x - P.x, dy = Bs.y - P.y;
        if (dx * dx + dy * dy < 260 * 260) idx = 100;
    }
    if (rbActive >= 0) idx = 200 + rbActive;
    return idx;
}

static int lock_pos(float *x, float *y)
{
    if (P.lock >= 200) {
        RBoss *b = &RB[P.lock - 200];
        if (b->state == RS_IDLE || b->state == RS_DEAD) return 0;
        *x = b->x; *y = b->y; return 1;
    }
    if (P.lock == 100) {
        if (Bs.state == BS_OFF || Bs.state == BS_DEAD) return 0;
        *x = Bs.x; *y = Bs.y; return 1;
    }
    if (P.lock >= 0 && P.lock < NE && E[P.lock].alive && E[P.lock].state != ES_DEAD) {
        *x = E[P.lock].x; *y = E[P.lock].y; return 1;
    }
    return 0;
}

static int near_brazier(void)
{
    int i;
    for (i = 0; i < nbraz; i++) {
        float dx = (braz[i].tx - WX0) * TS + 8 - P.x, dy = (braz[i].ty - WY0) * TS + 8 - P.y;
        if (dx * dx + dy * dy < 28 * 28) return i;
    }
    return -1;
}

static void start_action(unsigned b, float mx, float my, float ml)
{
    if ((b & B_CROSS) && P.st > 0) {
        P.state = PS_ROLL; P.timer = 0;
        if (ml > 0) { P.rx = mx / ml; P.ry = my / ml; } else { P.rx = -P.fx; P.ry = -P.fy; }
        P.st -= 20; P.stDelay = 28; P.iframes = 14;
        sfx(SFX_ROLL);
    } else if ((b & (B_SQUARE | B_TRIANGLE)) && P.st > 0) {
        P.state = PS_ATK; P.timer = 0;
        P.atk = (b & B_SQUARE) ? 0 : 1;
        P.atkId++; P.hitDone = 0;
        Bs.hitDone = 0;
        if (ml > 0 && P.lock < 0) { P.fx = mx / ml; P.fy = my / ml; }
        P.st -= P.atk ? 32 : 16; P.stDelay = 30;
        sfx(SFX_SWING);
    } else if ((b & B_CIRCLE) && P.flasks > 0 && P.hp < maxhp()) {
        P.state = PS_HEAL; P.timer = 0;
    } else return;
    if (P.st < 0) P.st = 0;
    P.buf = 0; P.bufT = 0;
}

static void player_update(const Input *in)
{
    float mx = in->ax, my = in->ay, ml, lx, ly;
    unsigned act = in->pressed & (B_SQUARE | B_TRIANGLE | B_CROSS | B_CIRCLE);

    if (in->held & B_LEFT) mx = -1;
    if (in->held & B_RIGHT) mx = 1;
    if (in->held & B_UP) my = -1;
    if (in->held & B_DOWN) my = 1;
    ml = sqrtf(mx * mx + my * my);
    if (ml < 0.25f) { mx = my = 0; ml = 0; }
    else if (ml > 1) { mx /= ml; my /= ml; ml = 1; }

    /* buffer degli input: un tasto premuto poco prima viene eseguito appena possibile */
    if (act) { P.buf = act; P.bufT = 10; }
    else if (P.bufT > 0 && --P.bufT == 0) P.buf = 0;

    if (in->pressed & B_R) P.lock = (P.lock == -1) ? find_lock_target() : -1;
    if (P.lock != -1 && !lock_pos(&lx, &ly)) P.lock = -1;

    if (P.iframes > 0) P.iframes--;
    if (P.state == PS_FREE || P.state == PS_HEAL) {
        if (P.stDelay > 0) P.stDelay--;
        else { P.st += 1.2f; if (P.st > maxst()) P.st = (float)maxst(); }
    }
    if (P.hpGhost > P.hp) P.hpGhost -= 0.6f; else P.hpGhost = P.hp;

    if (P.lock != -1 && lock_pos(&lx, &ly) && P.state != PS_ROLL) {
        float dx = lx - P.x, dy = ly - P.y, d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (P.state != PS_ATK || P.timer < AW[P.atk]) { P.fx = dx / d; P.fy = dy / d; }
    }

    switch (P.state) {
    case PS_FREE:
        if (ml > 0) {
            move_circle(&P.x, &P.y, 6, mx * 1.75f * ml, my * 1.75f * ml);
            P.walk += 0.3f * ml;
            if (P.lock == -1) { P.fx = mx / ml; P.fy = my / ml; }
        }
        if (P.buf) start_action(P.buf, mx, my, ml);
        break;
    case PS_ATK: {
        int W = AW[P.atk], A = AA[P.atk], R = AR[P.atk];
        P.timer++;
        if (P.timer > W && P.timer <= W + A) {
            move_circle(&P.x, &P.y, 6, P.fx * 1.3f, P.fy * 1.3f);
            player_hits();
        }
        if (P.timer >= W + A + R - 7 && P.buf) { P.state = PS_FREE; start_action(P.buf, mx, my, ml); }
        else if (P.timer >= W + A + R) P.state = PS_FREE;
        break;
    }
    case PS_ROLL: {
        float s = P.timer < 12 ? 3.4f : 1.0f;
        P.timer++;
        move_circle(&P.x, &P.y, 6, P.rx * s, P.ry * s);
        if (P.timer >= 20) {
            P.state = PS_FREE;
            if (P.buf) start_action(P.buf, mx, my, ml);
        }
        break;
    }
    case PS_HEAL:
        P.timer++;
        if (ml > 0) move_circle(&P.x, &P.y, 6, mx * 0.45f, my * 0.45f);
        if (P.timer == 26) {
            P.flasks--;
            P.hp += maxhp() * 0.45f;
            if (P.hp > maxhp()) P.hp = (float)maxhp();
            P.hpGhost = P.hp;
            spawn_parts(P.x, P.y, 14, RGB(255, 170, 60), 1.4f);
            sfx(SFX_HEAL);
        }
        if (P.timer >= 44) P.state = PS_FREE;
        break;
    case PS_HURT:
        P.timer++;
        move_circle(&P.x, &P.y, 6, P.kx, P.ky);
        P.kx *= 0.8f; P.ky *= 0.8f;
        if (P.timer >= 16) P.state = PS_FREE;
        break;
    default: break;
    }

    /* recupero frammenti */
    if (drop.active) {
        float dx = drop.x - P.x, dy = drop.y - P.y;
        if (dx * dx + dy * dy < 14 * 14) {
            P.frags += drop.amount; drop.active = 0;
            set_msg("FRAMMENTI RECUPERATI", 120);
            sfx(SFX_PICK);
            spawn_parts(P.x, P.y, 20, RGB(120, 220, 255), 1.8f);
        }
    }

    /* oggetti nel mondo */
    {
        int i;
        for (i = 0; i < npick; i++) {
            float dx, dy;
            if (picks[i].taken) continue;
            dx = (picks[i].tx - WX0) * TS + 8 - P.x; dy = (picks[i].ty - WY0) * TS + 8 - P.y;
            if (dx * dx + dy * dy < 13 * 13) {
                picks[i].taken = 1;
                if (picks[i].type == 1) { P.maxFlask++; P.flasks++; set_msg("BRACE ANTICA  +1 FIALA", 150); }
                else { char b[40], n[12]; int v = picks[i].amount, k = 0, j; char t[12];
                    P.frags += v;
                    while (v > 0) { t[k++] = (char)('0' + v % 10); v /= 10; }
                    for (j = 0; j < k; j++) n[j] = t[k - 1 - j];
                    n[k] = 0;
                    strcpy(b, "FRAMMENTI +"); strcat(b, n); set_msg(b, 120); }
                spawn_parts(P.x, P.y, 24, RGB(255, 200, 120), 1.8f);
                sfx(SFX_PICK);
            }
        }
    }
    /* nome della regione quando ci entri (controllato ogni 15 frame) */
    if (frame % 15 == 0) {
        int tx = (int)(P.x / TS), ty = (int)(P.y / TS), wx = tx + WX0, wy = ty + WY0, kx = wx - COREX, ky = wy - COREY, r;
        const Town *tw = town_at_w(wx, wy);
        static const char *names[12] = { "VELMORA", "BOSCO DELLE CENERI", "COLLINA DEI SEPOLTI", "LAGO NERO", "ROVINE DI ORMVAL",
                                         "MURA DI VELMORA", "FORESTA DEI PINI NERI", "BOSCO MORTO", "MONTI DI GRAVA",
                                         "PIANA CINEREA", "ACQUITRINI", "CONFINI DEL MONDO" };
        const char *nm;
        if (in_city(tx, ty)) r = 0;
        else if (tw) r = 1000 + (int)(tw - &ptown[0][0]) + (tw >= &ctown[0] && tw <= &ctown[2] ? 50000 : 0);
        else if (in_core_w(wx, wy)) r = kx < 38 ? 1 : (ky < 56 ? 2 : (ky > 112 ? 3 : (kx > 137 ? 4 : 5)));
        else {
            float forest = vnoise2(wx / 90.0f, wy / 90.0f, 1024, 1024, 211), dead = vnoise2(wx / 150.0f, wy / 150.0f, 1024, 1024, 213);
            float hh = ground_h(P.x, P.y);
            int e = wx < wy ? wx : wy;
            if (WORLDT - wx < e) e = WORLDT - wx;
            if (WORLDT - wy < e) e = WORLDT - wy;
            r = e < 80 ? 11 : (forest > 0.58f ? 6 : (dead > 0.62f ? 7 : (hh > 34 ? 8 : (hh < WL + 4 ? 10 : 9))));
        }
        if (r != P.region) {
            P.region = r;
            nm = r >= 1000 ? tw->name : names[r];
            set_msg(nm, 150);
        }
    }

    /* braciere */
    if (in->pressed & B_SELECT) {
        int b = near_brazier();
        if (b >= 0 && P.state == PS_FREE && !gate_closed) {
            if (!braz[b].lit) { braz[b].lit = 1; set_msg("BRACIERE ACCESO", 120); }
            cpB = b;
            respawn();
            mode = M_MENU; menuSel = 0;
            sfx(SFX_BRAZIER);
            save_game();
        } else if (b < 0 && near_npc() && P.state == PS_FREE) npc_talk();
    }
}

/* ------------------------------------------------------------------ */
/* nemici                                                              */
/* ------------------------------------------------------------------ */
static void enemies_update(void)
{
    int i, j;
    for (i = 0; i < NE; i++) {
        Enemy *e = &E[i];
        const EKind *k = &EK[e->kind];
        float dx, dy, d;
        if (!e->alive) continue;
        if (e->state == ES_DEAD) { if (--e->deadT <= 0) e->alive = 0; continue; }
        if (e->flash > 0) e->flash--;
        dx = P.x - e->x; dy = P.y - e->y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
        switch (e->state) {
        case ES_IDLE:
            if (d < 115 && P.state != PS_DEAD) e->state = ES_CHASE;
            break;
        case ES_CHASE:
            e->fx = dx / d; e->fy = dy / d;
            if (d > 320) e->state = ES_IDLE;
            else if (d > k->range + 2) move_circle(&e->x, &e->y, k->r, e->fx * k->speed, e->fy * k->speed);
            else { e->state = ES_WIND; e->timer = k->wind; }
            break;
        case ES_WIND:
            if (e->timer > k->wind / 3) { e->fx = dx / d; e->fy = dy / d; }
            if (--e->timer <= 0) { e->state = ES_STRIKE; e->timer = 9; e->hitDone = 0; }
            break;
        case ES_STRIKE:
            move_circle(&e->x, &e->y, k->r, e->fx * 2.0f, e->fy * 2.0f);
            if (!e->hitDone && e->timer <= 7) {
                float ex = P.x - e->x, ey = P.y - e->y, ed = sqrtf(ex * ex + ey * ey) + 0.001f;
                e->hitDone = 1;
                if (ed < k->range + 10 && (ex * e->fx + ey * e->fy) / ed > 0.2f) hurt_player(k->dmg, e->x, e->y);
            }
            if (--e->timer <= 0) { e->state = ES_REC; e->timer = k->rec; }
            break;
        case ES_REC:
        case ES_STAG:
            if (--e->timer <= 0) e->state = ES_CHASE;
            break;
        }
        /* non si sovrappongono al giocatore */
        if (d < k->r + 6) move_circle(&e->x, &e->y, k->r, -dx / d * (k->r + 6 - d), -dy / d * (k->r + 6 - d));
    }
    for (i = 0; i < NE; i++)
        for (j = i + 1; j < NE; j++) {
            Enemy *a = &E[i], *b = &E[j];
            float dx, dy, d, m;
            if (!a->alive || !b->alive || a->state == ES_DEAD || b->state == ES_DEAD) continue;
            dx = b->x - a->x; dy = b->y - a->y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
            m = EK[a->kind].r + EK[b->kind].r;
            if (d < m) {
                float p = (m - d) * 0.5f;
                move_circle(&a->x, &a->y, EK[a->kind].r, -dx / d * p, -dy / d * p);
                move_circle(&b->x, &b->y, EK[b->kind].r, dx / d * p, dy / d * p);
            }
        }
}

/* ------------------------------------------------------------------ */
/* boss: il Custode Cinereo                                            */
/* ------------------------------------------------------------------ */
static int boss_wind_time(int atk)
{
    static const int w[3] = { 36, 52, 34 };
    return Bs.phase ? w[atk] * 7 / 10 : w[atk];
}

static void boss_start(int atk)
{
    Bs.atk = atk;
    Bs.state = BS_WIND;
    Bs.timer = boss_wind_time(atk);
    Bs.sx = Bs.x; Bs.sy = Bs.y;
    Bs.tx = P.x; Bs.ty = P.y;
    Bs.hitDone = 0;
}

static void boss_update(void)
{
    float dx, dy, d;
    if (Bs.state == BS_OFF) {
        if (!Bs.dead && in_arena(P.x, P.y) && P.state != PS_DEAD) {
            gate_closed = 1;
            Bs.state = BS_INTRO; Bs.timer = 80;
            set_msg("IL CUSTODE CINEREO", 120);
            sfx(SFX_ROAR);
        }
        return;
    }
    if (Bs.state == BS_DEAD) {
        if (Bs.timer > 0 && --Bs.timer == 0) { mode = M_END; storyPage = 0; storyT = 0; endingSeen = 1; save_game(); }
        return;
    }
    if (Bs.flash > 0) Bs.flash--;
    dx = P.x - Bs.x; dy = P.y - Bs.y; d = sqrtf(dx * dx + dy * dy) + 0.001f;

    if (!Bs.phase && Bs.hp < Bs.maxhp / 2 && (Bs.state == BS_CHASE || Bs.state == BS_REC)) {
        Bs.phase = 1; Bs.state = BS_ROAR; Bs.timer = 60; shake = 40;
        set_msg("LA BRACE SI RISVEGLIA", 100);
    }

    switch (Bs.state) {
    case BS_INTRO:
    case BS_ROAR:
        Bs.fx = dx / d; Bs.fy = dy / d;
        if (Bs.state == BS_ROAR && Bs.timer == 30 && d < 60) hurt_player(10, Bs.x, Bs.y);
        if (--Bs.timer <= 0) { Bs.state = BS_CHASE; Bs.timer = 20; }
        break;
    case BS_CHASE: {
        float sp = Bs.phase ? 1.35f : 1.0f;
        Bs.fx = dx / d; Bs.fy = dy / d;
        if (d > 46) move_circle(&Bs.x, &Bs.y, 16, Bs.fx * sp, Bs.fy * sp);
        if (--Bs.timer <= 0 || d < 44) {
            float r = frnd();
            if (d < 64) boss_start(r < 0.65f ? BA_SWIPE : BA_SLAM);
            else if (d > 120) boss_start(r < 0.55f ? BA_CHARGE : BA_SLAM);
            else if (r < 0.35f) boss_start(BA_SLAM);
            else Bs.timer = 12;
        }
        break;
    }
    case BS_WIND: {
        int W = boss_wind_time(Bs.atk);
        float t = 1.0f - (float)Bs.timer / W;
        if (Bs.atk == BA_SLAM) {
            /* salta verso il punto bersaglio */
            if (t < 0.55f) { Bs.tx = P.x; Bs.ty = P.y; }
            Bs.x = Bs.sx + (Bs.tx - Bs.sx) * t;
            Bs.y = Bs.sy + (Bs.ty - Bs.sy) * t;
            Bs.z = sinf(t * PI) * 40;
        } else if (t < 0.7f) { Bs.fx = dx / d; Bs.fy = dy / d; }
        if (--Bs.timer <= 0) {
            Bs.state = BS_ACT; Bs.z = 0;
            if (Bs.atk == BA_SLAM) {
                float ex = P.x - Bs.tx, ey = P.y - Bs.ty;
                if (collides(Bs.x, Bs.y, 16)) { Bs.x = Bs.sx; Bs.y = Bs.sy; }
                Bs.timer = 8; shake = 14;
                spawn_parts(Bs.tx, Bs.ty, 30, RGB(255, 120, 40), 3.2f);
                sfx(SFX_SLAM);
                if (ex * ex + ey * ey < 52 * 52) hurt_player(45, Bs.tx, Bs.ty);
            } else if (Bs.atk == BA_SWIPE) Bs.timer = 12;
            else Bs.timer = 34;
        }
        break;
    }
    case BS_ACT:
        if (Bs.atk == BA_SWIPE) {
            move_circle(&Bs.x, &Bs.y, 16, Bs.fx * 1.6f, Bs.fy * 1.6f);
            if (!Bs.hitDone && Bs.timer <= 10) {
                Bs.hitDone = 1;
                if (d < 58 && (dx * Bs.fx + dy * Bs.fy) / d > 0.05f) hurt_player(32, Bs.x, Bs.y);
            }
        } else if (Bs.atk == BA_CHARGE) {
            if (move_circle(&Bs.x, &Bs.y, 16, Bs.fx * 4.6f, Bs.fy * 4.6f)) { Bs.timer = 1; shake = 10; }
            if (frame % 2 == 0) spawn_parts(Bs.x, Bs.y, 2, RGB(140, 120, 100), 1.0f);
            if (!Bs.hitDone && d < 30) { Bs.hitDone = 1; hurt_player(30, Bs.x, Bs.y); }
        }
        if (--Bs.timer <= 0) {
            if (Bs.phase && Bs.atk == BA_SWIPE && Bs.combo == 0 && frnd() < 0.55f) {
                Bs.combo = 1; boss_start(BA_SWIPE); Bs.timer = 16;
            } else {
                Bs.combo = 0;
                Bs.state = BS_REC;
                Bs.timer = Bs.atk == BA_SLAM ? 46 : (Bs.atk == BA_CHARGE ? 40 : 30);
                if (Bs.phase) Bs.timer = Bs.timer * 3 / 4;
            }
        }
        break;
    case BS_REC:
        if (--Bs.timer <= 0) { Bs.state = BS_CHASE; Bs.timer = 10 + (int)(rnd() % 20); }
        break;
    }
    /* il corpo del boss spinge il giocatore */
    dx = P.x - Bs.x; dy = P.y - Bs.y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
    if (Bs.z < 6 && d < 24) move_circle(&P.x, &P.y, 6, dx / d * (24 - d), dy / d * (24 - d));
}

static void rb_center(int i, float *x, float *y)
{
    *x = (RBD[i].ax + COREX - WX0) * TS + 8.0f;
    *y = (RBD[i].ay + COREY - WY0) * TS + 8.0f;
}

static void rb_reset(int i)
{
    RBoss *b = &RB[i];
    int def = b->defeated;
    memset(b, 0, sizeof(*b));
    b->defeated = def;
    rb_center(i, &b->x, &b->y);
    b->fx = 0; b->fy = 1;
    b->hp = RBD[i].hp;
    b->state = def ? RS_DEAD : RS_IDLE;
}

static void rb_reset_all(void)
{
    int i;
    for (i = 0; i < NRB; i++) rb_reset(i);
    rbActive = -1;
    memset(bolts, 0, sizeof(bolts));
    memset(waves, 0, sizeof(waves));
}

static void add_wave(float x, float y, float speed, float maxr, int dmg)
{
    int i;
    for (i = 0; i < MAXWAVE; i++)
        if (waves[i].maxr <= 0) { waves[i].x = x; waves[i].y = y; waves[i].r = 4; waves[i].speed = speed; waves[i].maxr = maxr; waves[i].dmg = dmg; waves[i].hit = 0; return; }
}

static void add_bolt(float x, float y, float ang, float sp, int dmg)
{
    int i;
    for (i = 0; i < MAXBOLT; i++)
        if (bolts[i].life <= 0) { bolts[i].x = x; bolts[i].y = y; bolts[i].vx = cosf(ang) * sp; bolts[i].vy = sinf(ang) * sp; bolts[i].life = 110; bolts[i].dmg = dmg; return; }
}

static int rb_wind_time(int i, int atk)
{
    static const int w[5] = { 30, 46, 30, 40, 36 };
    int t = w[atk];
    if (i == RB_MATRIARCA) t = t * 3 / 4;
    if (RB[i].phase) t = t * 7 / 10;
    return t;
}

static void rb_start(int i, int atk)
{
    RBoss *b = &RB[i];
    b->atk = atk; b->state = RS_WIND; b->timer = rb_wind_time(i, atk);
    b->sx = b->x; b->sy = b->y; b->tx = P.x; b->ty = P.y; b->hitDone = 0;
}

static void rb_defeat(int i)
{
    RBoss *b = &RB[i];
    b->state = RS_DEAD; b->defeated = 1; b->timer = 120;
    rbActive = -1;
    embers++;
    P.frags += 2000 + i * 500;
    shake = 30;
    spawn_parts(b->x, b->y, 60, RGB(255, 150, 60), 3.0f);
    set_msg("BRACE PRIMEVA OTTENUTA", 200);
    sfx(SFX_VICTORY);
    save_game();
}

/* il giocatore colpisce i boss delle regioni */
static void rb_player_hits(float range, int dmg)
{
    int i;
    for (i = 0; i < NRB; i++) {
        RBoss *b = &RB[i];
        float dx, dy, d;
        if (b->state == RS_IDLE || b->state == RS_DEAD || b->hitDone == P.atkId || b->z > 6) continue;
        dx = b->x - P.x; dy = b->y - P.y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (d < range + RBD[i].body && (dx * P.fx + dy * P.fy) / d > 0.2f) {
            b->hitDone = P.atkId;
            b->hp -= dmg; b->flash = 6;
            spawn_parts(b->x - dx / d * RBD[i].body, b->y - dy / d * RBD[i].body, 10, RGB(255, 170, 90), 2.2f);
            sfx(SFX_HIT);
            if (b->hp <= 0) { b->hp = 0; rb_defeat(i); }
        }
    }
}

static void rb_update(void)
{
    int i, k;
    /* proiettili */
    for (k = 0; k < MAXBOLT; k++) {
        Bolt *o = &bolts[k];
        float dx, dy;
        if (o->life <= 0) continue;
        o->x += o->vx; o->y += o->vy; o->life--;
        if (solid((int)(o->x / TS), (int)(o->y / TS)) && map[(int)(o->y / TS) & (MH - 1)][(int)(o->x / TS) & (MW - 1)] != T_WATER) o->life = 0;
        dx = P.x - o->x; dy = P.y - o->y;
        if (dx * dx + dy * dy < 7 * 7) { hurt_player(o->dmg, o->x, o->y); o->life = 0; }
        if (frame % 2 == 0) spawn_parts(o->x, o->y, 1, RGB(120, 190, 230), 0.3f);
    }
    /* onde d'urto: si schivano rotolando attraverso l'anello */
    for (k = 0; k < MAXWAVE; k++) {
        Wave *w = &waves[k];
        float d;
        if (w->maxr <= 0) continue;
        w->r += w->speed;
        d = sqrtf((P.x - w->x) * (P.x - w->x) + (P.y - w->y) * (P.y - w->y));
        if (!w->hit && fabsf(d - w->r) < 4.5f) { w->hit = 1; hurt_player(w->dmg, w->x, w->y); }
        if (w->r >= w->maxr) w->maxr = 0;
    }
    for (i = 0; i < NRB; i++) {
        RBoss *b = &RB[i];
        const RBDef *D = &RBD[i];
        float cx, cy, dx, dy, d, pd;
        if (b->state == RS_DEAD) { if (b->timer > 0) b->timer--; continue; }
        rb_center(i, &cx, &cy);
        pd = sqrtf((P.x - cx) * (P.x - cx) + (P.y - cy) * (P.y - cy)) / TS;
        if (b->state == RS_IDLE) {
            /* inizia lo scontro quando entri nell'arena */
            if (rbActive < 0 && pd < D->radius && P.state != PS_DEAD && mode == M_PLAY) {
                b->state = RS_INTRO; b->timer = 80; rbActive = i;
                set_msg(D->name, 150);
                sfx(SFX_ROAR);
            }
            continue;
        }
        /* se scappi lontano dall'arena il boss torna al suo posto */
        if (pd > D->radius + 14) { rb_reset(i); if (rbActive == i) rbActive = -1; continue; }
        if (b->flash > 0) b->flash--;
        dx = P.x - b->x; dy = P.y - b->y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (!b->phase && b->hp < D->hp / 2 && (b->state == RS_CHASE || b->state == RS_REC)) {
            b->phase = 1; b->state = RS_ROAR; b->timer = 60; shake = 30;
            sfx(SFX_ROAR);
            if (i == RB_MATRIARCA) {   /* chiama il branco */
                add_enemy(900000 + frame, 2, (int)(b->x / TS) + 3, (int)(b->y / TS));
                add_enemy(900001 + frame, 2, (int)(b->x / TS) - 3, (int)(b->y / TS));
            }
        }
        switch (b->state) {
        case RS_INTRO:
        case RS_ROAR:
            b->fx = dx / d; b->fy = dy / d;
            if (b->state == RS_ROAR && b->timer == 30) add_wave(b->x, b->y, 2.2f, 70, 12);
            if (--b->timer <= 0) { b->state = RS_CHASE; b->timer = 20; }
            break;
        case RS_CHASE: {
            float sp = D->speed * (b->phase ? 1.25f : 1.0f);
            b->fx = dx / d; b->fy = dy / d;
            if (d > D->body + 30) move_circle(&b->x, &b->y, D->body * 0.8f, b->fx * sp, b->fy * sp);
            if (--b->timer <= 0 || d < D->body + 26) {
                float r = frnd();
                int a;
                if (d < D->body + 40) a = r < 0.6f ? D->atk[1] : D->atk[2];
                else if (d > 110) a = r < 0.6f ? D->atk[0] : D->atk[2];
                else a = D->atk[(int)(r * 3) % 3];
                if (a == RA_SWIPE && d > D->body + 50) a = D->atk[0];
                rb_start(i, a);
            }
            break;
        }
        case RS_WIND: {
            int W = rb_wind_time(i, b->atk);
            float t = 1.0f - (float)b->timer / W;
            if (b->atk == RA_SLAM && i != RB_CAMPANARO) {
                if (t < 0.55f) { b->tx = P.x; b->ty = P.y; }
                b->x = b->sx + (b->tx - b->sx) * t; b->y = b->sy + (b->ty - b->sy) * t;
                b->z = sinf(t * PI) * 34;
            } else if (b->atk == RA_SLAM) { b->tx = b->x + b->fx * 26; b->ty = b->y + b->fy * 26; if (t < 0.6f) { b->fx = dx / d; b->fy = dy / d; } }
            else if (t < 0.7f) { b->fx = dx / d; b->fy = dy / d; }
            if (--b->timer <= 0) {
                b->state = RS_ACT; b->z = 0;
                switch (b->atk) {
                case RA_SWIPE: b->timer = 12; sfx(SFX_SWING); break;
                case RA_SLAM: {
                    float ex = P.x - b->tx, ey = P.y - b->ty, rr = i == RB_CAMPANARO ? 58.0f : 48.0f;
                    if (collides(b->x, b->y, D->body * 0.8f)) { b->x = b->sx; b->y = b->sy; }
                    b->timer = 8; shake = 16;
                    spawn_parts(b->tx, b->ty, 30, i == RB_ANNEGATA ? RGB(120, 180, 230) : RGB(220, 200, 170), 3.0f);
                    if (ex * ex + ey * ey < rr * rr) hurt_player(D->dmg + 10, b->tx, b->ty);
                    if (b->phase || i == RB_CAMPANARO) add_wave(b->tx, b->ty, 2.4f, 90, D->dmg / 2);
                    sfx(i == RB_CAMPANARO ? SFX_BELL : SFX_SLAM);
                    break;
                }
                case RA_CHARGE: b->timer = 30; sfx(SFX_ROAR); break;
                case RA_WAVE:
                    b->timer = 20; add_wave(b->x, b->y, 2.6f, 110, D->dmg / 2);
                    if (b->phase) add_wave(b->x, b->y, 1.7f, 110, D->dmg / 2);
                    sfx(i == RB_CAMPANARO ? SFX_BELL : SFX_ROAR);
                    break;
                case RA_BOLTS: {
                    float a0 = atan2f(dy, dx);
                    int n = b->phase ? 5 : 3, q;
                    for (q = 0; q < n; q++) add_bolt(b->x, b->y, a0 + (q - (n - 1) * 0.5f) * 0.28f, 2.6f, D->dmg - 6);
                    b->timer = 14; sfx(SFX_SWING);
                    break;
                }
                }
            }
            break;
        }
        case RS_ACT:
            if (b->atk == RA_SWIPE) {
                move_circle(&b->x, &b->y, D->body * 0.8f, b->fx * 1.6f, b->fy * 1.6f);
                if (!b->hitDone && b->timer <= 10) {
                    b->hitDone = -1;
                    if (d < D->body + 44 && (dx * b->fx + dy * b->fy) / d > 0.0f) hurt_player(D->dmg, b->x, b->y);
                }
            } else if (b->atk == RA_CHARGE) {
                float sp = i == RB_MATRIARCA ? 5.5f : 4.4f;
                if (move_circle(&b->x, &b->y, D->body * 0.8f, b->fx * sp, b->fy * sp)) { b->timer = 1; shake = 10; }
                if (frame % 2 == 0) spawn_parts(b->x, b->y, 2, RGB(140, 120, 100), 1.0f);
                if (b->hitDone != -1 && d < D->body + 10) { b->hitDone = -1; hurt_player(D->dmg, b->x, b->y); }
            }
            if (--b->timer <= 0) {
                if (b->phase && b->atk == RA_SWIPE && b->combo == 0 && frnd() < 0.5f) { b->combo = 1; rb_start(i, RA_SWIPE); b->timer = 14; }
                else {
                    b->combo = 0; b->state = RS_REC;
                    b->timer = b->atk == RA_SLAM ? 44 : (b->atk == RA_CHARGE ? 38 : 30);
                    if (b->phase) b->timer = b->timer * 3 / 4;
                }
            }
            break;
        case RS_REC:
            if (--b->timer <= 0) { b->state = RS_CHASE; b->timer = 10 + (int)(rnd() % 20); }
            break;
        }
        /* il corpo del boss spinge il giocatore */
        dx = P.x - b->x; dy = P.y - b->y; d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (b->z < 6 && d < D->body + 6) move_circle(&P.x, &P.y, 6, dx / d * (D->body + 6 - d), dy / d * (D->body + 6 - d));
    }
}

static void rb_shift(float ox, float oy)
{
    int i;
    for (i = 0; i < NRB; i++) { RB[i].x -= ox; RB[i].y -= oy; RB[i].tx -= ox; RB[i].ty -= oy; RB[i].sx -= ox; RB[i].sy -= oy; }
    for (i = 0; i < MAXBOLT; i++) { bolts[i].x -= ox; bolts[i].y -= oy; }
    for (i = 0; i < MAXWAVE; i++) { waves[i].x -= ox; waves[i].y -= oy; }
}

/* ------------------------------------------------------------------ */
/* audio: effetti e musica generati dal codice (composizione originale) */
/* ------------------------------------------------------------------ */
#define AUDIO_RATE 44100
#define SINE_N 1024
static int16_t sine_tab[SINE_N];
static volatile int sfxq[32];
static volatile int sfxw, sfxr;
static volatile int music_mode;      /* 0 titolo, 1 esplorazione, 2 boss, 3 finale */

static void sfx(int id)
{
    int n = (sfxw + 1) & 31;
    if (n != sfxr) { sfxq[sfxw] = id; sfxw = n; }
}

typedef struct { int on; float ph, f0, f1, t, dur, vol, noise, pan; int wave; } Voice;
#define NVOICE 14
static Voice voices[NVOICE];
static unsigned anoise = 22222u;
static float rev_buf[16384];
static int rev_pos;
static float mel_ph, mel_env, mel_f, pad_ph[4], kick_env, kick_ph;
static int mus_step;

static float tsin(float ph) { return sine_tab[((int)(ph * SINE_N)) & (SINE_N - 1)] * (1.0f / 32767.0f); }
static float nrand(void) { anoise = anoise * 1664525u + 1013904223u; return ((anoise >> 9) & 0xffff) / 32768.0f - 1.0f; }

static void voice_start(float f0, float f1, float dur, float vol, float noise, int wave, float pan)
{
    int i, best = 0;
    float bt = -1;
    for (i = 0; i < NVOICE; i++) {
        if (!voices[i].on) { best = i; break; }
        if (voices[i].t / voices[i].dur > bt) { bt = voices[i].t / voices[i].dur; best = i; }
    }
    voices[best].on = 1; voices[best].ph = 0; voices[best].f0 = f0; voices[best].f1 = f1; voices[best].t = 0;
    voices[best].dur = dur; voices[best].vol = vol; voices[best].noise = noise; voices[best].wave = wave; voices[best].pan = pan;
}

static void sfx_play(int id)
{
    switch (id) {
    case SFX_SWING:   voice_start(900, 300, 0.16f, 0.35f, 0.85f, 0, 0.5f); break;
    case SFX_HIT:     voice_start(180, 70, 0.14f, 0.6f, 0.5f, 1, 0.5f); voice_start(2400, 800, 0.05f, 0.25f, 1, 0, 0.5f); break;
    case SFX_HURT:    voice_start(220, 90, 0.25f, 0.6f, 0.35f, 1, 0.5f); break;
    case SFX_ROLL:    voice_start(400, 150, 0.22f, 0.3f, 1, 0, 0.5f); break;
    case SFX_HEAL:    voice_start(440, 660, 0.5f, 0.3f, 0, 0, 0.4f); voice_start(660, 990, 0.6f, 0.2f, 0, 0, 0.6f); break;
    case SFX_BRAZIER: voice_start(110, 110, 1.6f, 0.35f, 0.6f, 0, 0.5f); voice_start(330, 330, 1.2f, 0.25f, 0, 2, 0.5f); break;
    case SFX_PICK:    voice_start(880, 880, 0.15f, 0.3f, 0, 2, 0.5f); voice_start(1320, 1320, 0.4f, 0.25f, 0, 2, 0.5f); break;
    case SFX_EDEATH:  voice_start(160, 40, 0.5f, 0.45f, 0.4f, 1, 0.5f); break;
    case SFX_ROAR:    voice_start(90, 55, 1.2f, 0.7f, 0.7f, 1, 0.5f); voice_start(140, 70, 1.0f, 0.4f, 0.3f, 1, 0.5f); break;
    case SFX_SLAM:    voice_start(80, 30, 0.7f, 0.9f, 0.6f, 1, 0.5f); break;
    case SFX_BELL:    voice_start(196, 196, 2.5f, 0.6f, 0, 2, 0.5f); voice_start(523, 523, 2.0f, 0.3f, 0, 2, 0.5f); voice_start(81, 60, 0.6f, 0.6f, 0.4f, 1, 0.5f); break;
    case SFX_MENU:    voice_start(660, 660, 0.06f, 0.2f, 0, 2, 0.5f); break;
    case SFX_SELECT:  voice_start(520, 780, 0.15f, 0.25f, 0, 2, 0.5f); break;
    case SFX_DIE:     voice_start(220, 55, 2.2f, 0.5f, 0.2f, 1, 0.5f); voice_start(330, 82, 2.2f, 0.3f, 0, 0, 0.5f); break;
    case SFX_VICTORY: voice_start(220, 220, 2.5f, 0.3f, 0, 2, 0.4f); voice_start(330, 330, 2.5f, 0.25f, 0, 2, 0.6f); voice_start(440, 440, 2.8f, 0.2f, 0, 2, 0.5f); break;
    }
}

/* scala di La minore naturale: frequenze delle note usate dalla musica */
static float note_hz(int n) { return 220.0f * powf(2.0f, n / 12.0f); }   /* chiamata solo una volta per blocco */

void game_audio_fill(int16_t *out, int frames)
{
    static const int chords[4][3] = { {0, 3, 7}, {-4, 0, 3}, {3, 7, 10}, {-2, 2, 5} };   /* Lam, Fa, Do, Sol */
    static const int scale[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static unsigned samples;            /* contatore di campioni (intero: niente doppia precisione) */
    static float pad_f[4], pad_amp[3];
    int i, k, chord, step, mm = music_mode;
    float bpm = mm == 2 ? 112.0f : 58.0f, beat, dt = 2.0f / AUDIO_RATE;   /* sintesi a 22 kHz */
    float mdecay = mm == 2 ? 0.9997f : 0.99986f, kdecay = 0.9988f, gain = mm == 0 ? 0.8f : (mm == 3 ? 1.1f : 1.0f);
    if (!sine_tab[SINE_N / 4]) for (i = 0; i < SINE_N; i++) sine_tab[i] = (int16_t)(sinf(i * 2 * PI / SINE_N) * 32767);
    while (sfxr != sfxw) { sfx_play(sfxq[sfxr]); sfxr = (sfxr + 1) & 31; }
    /* parametri della musica calcolati una volta per blocco */
    beat = (float)(samples % (AUDIO_RATE * 600u)) * bpm / (60.0f * AUDIO_RATE);
    chord = ((int)(beat / 8)) & 3;
    step = (int)(beat * 2);
    for (k = 0; k < 3; k++) {
        pad_f[k] = note_hz(chords[chord][k] - 12);
        pad_amp[k] = 0.045f + 0.02f * tsin(beat * 0.05f + k * 0.33f);
    }
    pad_f[3] = note_hz(chords[chord][0] - 24);
    if (step != mus_step) {
        unsigned h = (unsigned)step * 2654435761u;
        mus_step = step;
        if ((mm == 2 && (h >> 7) % 2 == 0) || (mm != 2 && (h >> 9) % 3 == 0)) {
            int deg = (int)((h >> 13) % 7), oct = (int)((h >> 17) % 2);
            mel_f = note_hz(scale[deg] + 12 * oct + (mm == 2 ? -12 : 0));
            mel_env = 1;
        }
        if (mm == 2 && step % 2 == 0) { kick_env = 1; kick_ph = 0; }
    }
    for (i = 0; i < frames; i += 2) {
        float l = 0, r = 0, m = 0;
        for (k = 0; k < 3; k++) {
            pad_ph[k] += pad_f[k] * dt; if (pad_ph[k] > 1) pad_ph[k] -= 1;
            m += tsin(pad_ph[k]) * pad_amp[k];
        }
        pad_ph[3] += pad_f[3] * dt; if (pad_ph[3] > 1) pad_ph[3] -= 1;
        m += tsin(pad_ph[3]) * 0.06f;                                 /* bordone grave */
        mel_ph += mel_f * dt; if (mel_ph > 1) mel_ph -= 1;
        m += (tsin(mel_ph) + 0.3f * tsin(mel_ph * 2.0f)) * mel_env * 0.09f;
        mel_env *= mdecay;
        if (kick_env > 0.001f) {   /* tamburo del combattimento */
            kick_ph += (50 + 90 * kick_env) * dt; if (kick_ph > 1) kick_ph -= 1;
            m += tsin(kick_ph) * kick_env * 0.35f;
            kick_env *= kdecay;
        }
        m *= gain;
        l = m; r = m;
        /* effetti */
        for (k = 0; k < NVOICE; k++) {
            Voice *v = &voices[k];
            float env, f, s;
            if (!v->on) continue;
            env = 1 - v->t / v->dur;
            if (env <= 0) { v->on = 0; continue; }
            if (v->t < 0.005f) env *= v->t / 0.005f;
            f = v->f0 + (v->f1 - v->f0) * (v->t / v->dur);
            v->ph += f * dt; if (v->ph > 1) v->ph -= 1;
            if (v->wave == 1) s = v->ph < 0.5f ? 0.6f : -0.6f;
            else if (v->wave == 2) s = tsin(v->ph) + 0.4f * tsin(v->ph * 2.76f);
            else s = tsin(v->ph);
            if (v->noise > 0) s = s * (1 - v->noise) + nrand() * v->noise;
            s *= env * env * v->vol;
            l += s * (1.2f - v->pan); r += s * (0.2f + v->pan);
            v->t += dt;
        }
        /* riverbero semplice */
        {
            float e = rev_buf[rev_pos];
            rev_buf[rev_pos] = (l + r) * 0.25f + e * 0.55f;
            rev_pos++; if (rev_pos >= 6615) rev_pos = 0;
            l += e * 0.4f; r += e * 0.4f;
        }
        if (l > 1) l = 1; if (l < -1) l = -1;
        if (r > 1) r = 1; if (r < -1) r = -1;
        out[i * 2] = out[i * 2 + 2] = (int16_t)(l * 26000);
        out[i * 2 + 1] = out[i * 2 + 3] = (int16_t)(r * 26000);
    }
    samples += (unsigned)frames;
}

/* ------------------------------------------------------------------ */
/* salvataggio sulla memory stick (file accanto a EBOOT.PBP)           */
/* ------------------------------------------------------------------ */
#define SAVE_PATH "save.dat"
#define SAVE_MAGIC 0x564c4d31u     /* "VLM1" */
typedef struct {
    unsigned magic, version;
    int vig, end, str, lvl, frags, maxFlask, cpB, embers, defeated[NRB], custode, ending;
    int dropActive, dropWX, dropWY, dropAmount;
    int deaths, time;
    unsigned char lit[MAXBRAZ / 8 + 1], taken[MAXPICK / 8 + 1];
} SaveData;


static void save_game(void)
{
    SaveData d;
    FILE *f;
    int i;
    memset(&d, 0, sizeof(d));
    d.magic = SAVE_MAGIC; d.version = 1;
    d.vig = P.vig; d.end = P.end; d.str = P.str; d.lvl = P.lvl; d.frags = P.frags; d.maxFlask = P.maxFlask;
    d.cpB = cpB; d.embers = embers; d.custode = Bs.dead; d.ending = endingSeen;
    for (i = 0; i < NRB; i++) d.defeated[i] = RB[i].defeated;
    d.dropActive = drop.active; d.dropAmount = drop.amount;
    d.dropWX = (int)(drop.x / TS) + WX0; d.dropWY = (int)(drop.y / TS) + WY0;
    d.deaths = deaths; d.time = playtime;
    for (i = 0; i < nbraz; i++) if (braz[i].lit) d.lit[i / 8] |= (unsigned char)(1 << (i & 7));
    for (i = 0; i < npick; i++) if (picks[i].taken) d.taken[i / 8] |= (unsigned char)(1 << (i & 7));
    f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    fwrite(&d, sizeof(d), 1, f);
    fclose(f);
}

static int save_exists(void)
{
    FILE *f = fopen(SAVE_PATH, "rb");
    SaveData d;
    int ok = 0;
    if (!f) return 0;
    if (fread(&d, sizeof(d), 1, f) == 1 && d.magic == SAVE_MAGIC) ok = 1;
    fclose(f);
    return ok;
}

static int load_game(void)
{
    SaveData d;
    FILE *f = fopen(SAVE_PATH, "rb");
    int i;
    if (!f) return 0;
    if (fread(&d, sizeof(d), 1, f) != 1 || d.magic != SAVE_MAGIC) { fclose(f); return 0; }
    fclose(f);
    P.vig = d.vig; P.end = d.end; P.str = d.str; P.lvl = d.lvl; P.frags = d.frags; P.maxFlask = d.maxFlask;
    cpB = d.cpB < nbraz ? d.cpB : 0; embers = d.embers; Bs.dead = d.custode; endingSeen = d.ending;
    for (i = 0; i < NRB; i++) RB[i].defeated = d.defeated[i];
    deaths = d.deaths; playtime = d.time;
    for (i = 0; i < nbraz; i++) braz[i].lit = (d.lit[i / 8] >> (i & 7)) & 1;
    for (i = 0; i < npick; i++) picks[i].taken = (d.taken[i / 8] >> (i & 7)) & 1;
    respawn();
    drop.active = d.dropActive; drop.amount = d.dropAmount;
    drop.x = (d.dropWX - WX0) * TS + 8.0f; drop.y = (d.dropWY - WY0) * TS + 8.0f;
    return 1;
}

/* ------------------------------------------------------------------ */
/* storia: introduzione, la Guardiana Ilde, finale (testi originali)  */
/* ------------------------------------------------------------------ */
static const char *intro_text[4][2] = {
    { "UN TEMPO LA GRANDE BRACE ARDEVA A VELMORA", "E LA SUA LUCE TENEVA LONTANA LA CENERE." },
    { "POI LA FIAMMA SI SPENSE. LA CENERE CADDE", "E CIO' CHE VIVEVA SI FECE BESTIA O PIETRA." },
    { "QUATTRO GUARDIANI CUSTODISCONO LE BRACI", "PRIMEVE: NEL BOSCO, SULLA COLLINA, NEL LAGO" },
    { "E TRA LE ROVINE. RACCOGLILE, CAVALIERE,", "E RIACCENDI LA GRANDE BRACE." },
};
static const char *ending_text[5][2] = {
    { "IL CUSTODE CINEREO CADE, E CON LUI", "L'ULTIMA OMBRA DI VELMORA." },
    { "PORTI LE QUATTRO BRACI PRIMEVE", "AL CUORE DELLA CITTA'." },
    { "LA GRANDE BRACE SI RIACCENDE.", "LA CENERE SMETTE DI CADERE." },
    { "E PER LA PRIMA VOLTA DOPO SECOLI,", "SU VELMORA SORGE L'ALBA." },
    { "FINE", "" },
};
static const char *talk_a, *talk_b;

/* la Guardiana vicino al primo braciere */
static float npc_x(void) { return (CX0 + 9 + COREX - WX0) * TS + 8.0f; }
static float npc_y(void) { return (CY0 + 5 + COREY - WY0) * TS + 8.0f; }
static int near_npc(void) { float dx = npc_x() - P.x, dy = npc_y() - P.y; return dx * dx + dy * dy < 36 * 36; }

static void npc_talk(void)
{
    static char line[48];
    if (Bs.dead) { talk_a = "LA GRANDE BRACE ARDE DI NUOVO."; talk_b = "GRAZIE, CAVALIERE. ORA RIPOSA."; }
    else if (embers >= 4) { talk_a = "LE QUATTRO BRACI SONO TUE. IL CANCELLO"; talk_b = "DEL CUSTODE ORA SI APRE. VAI."; }
    else if (embers == 0) { talk_a = "SEI SVEGLIO, CAVALIERE? CERCA I GUARDIANI"; talk_b = "E PORTAMI LE LORO BRACI PRIMEVE."; }
    else {
        strcpy(line, "BRACI PRIMEVE: ");
        line[15] = (char)('0' + embers); line[16] = '/'; line[17] = '4'; line[18] = 0;
        talk_a = "SENTO IL LORO CALORE. NE MANCANO ANCORA.";
        talk_b = line;
    }
    mode = M_TALK;
    sfx(SFX_SELECT);
}


/* ------------------------------------------------------------------ */
/* update principale                                                   */
/* ------------------------------------------------------------------ */
void game_update(const Input *in)
{
    int i;
    frame++;
    if (msgT > 0) msgT--;
    if (shake > 0) shake--;
    for (i = 0; i < MAXPART; i++)
        if (parts[i].life > 0) {
            parts[i].x += parts[i].vx; parts[i].y += parts[i].vy;
            parts[i].vx *= 0.9f; parts[i].vy *= 0.9f;
            parts[i].life--;
        }

    for (i = 0; i < MAXBLOOD; i++) if (blood[i].life > 0) blood[i].life--;
    camX += (P.x - camX) * 0.18f;
    camZ += (P.y - camZ) * 0.18f;
    music_mode = (mode == M_TITLE || mode == M_STORY) ? 0 : (mode == M_END ? 3 :
                 ((rbActive >= 0 || (Bs.state != BS_OFF && Bs.state != BS_DEAD)) ? 2 : 1));
    switch (mode) {
    case M_TITLE: {
        int nopt = hasSave ? 2 : 1;
        if (in->pressed & (B_UP | B_DOWN)) { titleSel = (titleSel + 1) % nopt; sfx(SFX_MENU); }
        if (in->pressed & (B_START | B_CROSS)) {
            sfx(SFX_SELECT);
            if (hasSave && titleSel == 0 && load_game()) { mode = M_PLAY; set_msg("CENERE DI VELMORA", 150); }
            else { mode = M_STORY; storyPage = 0; storyT = 0; }
        }
        break;
    }
    case M_STORY:
        storyT++;
        if (storyT > 30 && (in->pressed & (B_CROSS | B_START))) {
            storyPage++; storyT = 0; sfx(SFX_MENU);
            if (storyPage >= 4) { mode = M_PLAY; set_msg("CENERE DI VELMORA", 150); }
        }
        break;
    case M_TALK:
        if (in->pressed & (B_CROSS | B_CIRCLE | B_SELECT | B_START)) mode = M_PLAY;
        break;
    case M_END:
        storyT++;
        if (storyT > 60 && (in->pressed & (B_CROSS | B_START))) {
            storyPage++; storyT = 0;
            if (storyPage >= 6) { mode = M_PLAY; set_msg("VELMORA ALL'ALBA", 200); }
        }
        break;
    case M_PLAY:
        if (in->pressed & B_START) { mode = M_PAUSE; break; }
        {
            /* telecamera in terza persona: croce = ruota/inclina la visuale (la PSP non ha la levetta destra),
             * L + levetta = ruota, tocco di L = ricentra dietro le spalle; la levetta muove il personaggio */
            Input wi = *in;
            float sx = in->ax, sy = in->ay, fx, fz, lx, ly, ml;
            int manual = 0;
            if (in->held & B_LEFT) { camYaw -= 0.045f; manual = 1; }
            if (in->held & B_RIGHT) { camYaw += 0.045f; manual = 1; }
            if (in->held & B_UP) { camPitch += 0.015f; if (camPitch > 0.95f) camPitch = 0.95f; }
            if (in->held & B_DOWN) { camPitch -= 0.015f; if (camPitch < 0.12f) camPitch = 0.12f; }
            if (in->pressed & B_L) { lTap = 1; lUsed = 0; }
            if (in->held & B_L) {
                if (sx > 0.25f || sx < -0.25f) { camYaw += sx * 0.045f; lUsed = 1; manual = 1; }
                sx = 0;
            } else if (lTap) {
                lTap = 0;
                if (!lUsed) camYaw = atan2f(P.fy, P.fx);   /* ricentra */
            }
            if (P.lock != -1 && lock_pos(&lx, &ly)) {
                float ta = atan2f(ly - P.y, lx - P.x), d = ta - camYaw;
                while (d > PI) d -= 2 * PI;
                while (d < -PI) d += 2 * PI;
                camYaw += d * 0.08f;
            } else if (!manual) {
                /* segue dolcemente la direzione di marcia, senza girarsi se torni verso la telecamera */
                ml = sqrtf(sx * sx + sy * sy);
                if (ml > 0.3f && P.state == PS_FREE) {
                    float ma = atan2f(sy, sx) + PI / 2;      /* angolo dell'input rispetto ad "avanti" */
                    while (ma > PI) ma -= 2 * PI;
                    while (ma < -PI) ma += 2 * PI;
                    if (ma < 2.0f && ma > -2.0f) camYaw += ma * 0.012f * ml;
                }
            }
            fx = cosf(camYaw); fz = sinf(camYaw);
            wi.ax = fx * (-sy) + (-fz) * sx;
            wi.ay = fz * (-sy) + fx * sx;
            wi.held &= ~(unsigned)(B_UP | B_DOWN | B_LEFT | B_RIGHT);
            player_update(&wi);
        }
        keep_centered();
        if (mode != M_PLAY) break;
        enemies_update();
        boss_update();
        rb_update();
        playtime++;
        break;
    case M_DEAD:
        enemies_update();
        if (--deadT <= 0) { respawn(); mode = M_PLAY; }
        break;
    case M_PAUSE:
        if (in->pressed & (B_START | B_CIRCLE)) mode = M_PLAY;
        break;
    case M_MENU:
        if (in->pressed & B_UP) menuSel = (menuSel + 3) % 4;
        if (in->pressed & B_DOWN) menuSel = (menuSel + 1) % 4;
        if (in->pressed & B_CROSS) {
            if (menuSel == 3) mode = M_PLAY;
            else if (P.frags >= lvlCost()) {
                P.frags -= lvlCost();
                P.lvl++;
                if (menuSel == 0) P.vig++;
                else if (menuSel == 1) P.end++;
                else P.str++;
                P.hp = P.hpGhost = (float)maxhp();
                P.st = (float)maxst();
            }
        }
        if (in->pressed & (B_CIRCLE | B_START)) mode = M_PLAY;
        break;
    case M_WIN:
        winT++;
        if (winT > 90 && (in->pressed & (B_START | B_CROSS))) mode = M_PLAY;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* rendering 3D - gotico vittoriano, texture procedurali (originale)   */
/* ------------------------------------------------------------------ */
#define CAM_DIST 138.0f
#define CAM_PITCH 0.96f
#define FOGC RGB(26, 30, 42)

enum { TX_WHITE, TX_COBBLE, TX_STONE, TX_WOOD, TX_SLATE, TX_CLOTH, TX_METAL, TX_BARK, TX_GLOW, TX_WINDOW, TX_BRICK, TX_PLASTER, TX_FLAG, TX_GRASS, TX_DIRT, TX_ROCK, NTEX };
#define TSZ 64

static V3 v3(float x, float y, float z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
static uint32_t rgba(uint32_t c, int a) { return (c & 0x00FFFFFFu) | ((uint32_t)a << 24); }

static uint32_t shade(uint32_t c, float f)
{
    int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    uint32_t a = c & 0xFF000000u;
    if (f < 0) f = 0;
    r = (int)(r * f); g = (int)(g * f); b = (int)(b * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return a | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static unsigned hash2(int x, int y) { unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }

/* ------------------------------------------------------------------ */
/* texture procedurali (generate all'avvio, 64x64, ripetibili)         */
/* ------------------------------------------------------------------ */
static uint32_t texbuf[NTEX * TSZ * TSZ] __attribute__((aligned(16)));

static float hashf(int x, int y, int s) { return (float)(hash2(x * 7 + s * 131, y * 13 + s * 17) & 0xffff) / 65535.0f; }

static float vnoise2(float x, float y, int px, int py, int s)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy, a, b, c, d;
    int x0 = ((ix % px) + px) % px, x1 = (x0 + 1) % px, y0 = ((iy % py) + py) % py, y1 = (y0 + 1) % py;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    a = hashf(x0, y0, s); b = hashf(x1, y0, s); c = hashf(x0, y1, s); d = hashf(x1, y1, s);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

/* rumore frattale ripetibile: P celle sulla texture alla prima ottava */
static float fbm(int x, int y, int P, int s)
{
    float a = 0, amp = 0.5f;
    int o;
    for (o = 0; o < 4; o++) {
        a += amp * vnoise2(x * P / (float)TSZ, y * P / (float)TSZ, P, P, s + o);
        P *= 2; amp *= 0.5f;
    }
    return a / 0.9375f;
}

static uint32_t pack(float r, float g, float b, float a)
{
    int R = (int)(r * 255), G = (int)(g * 255), B = (int)(b * 255), A = (int)(a * 255);
    if (R < 0) R = 0; if (R > 255) R = 255;
    if (G < 0) G = 0; if (G > 255) G = 255;
    if (B < 0) B = 0; if (B > 255) B = 255;
    if (A < 0) A = 0; if (A > 255) A = 255;
    return ((uint32_t)A << 24) | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
}

static void gen_textures(void)
{
    int x, y, t;
    for (t = 0; t < NTEX; t++)
        for (y = 0; y < TSZ; y++)
            for (x = 0; x < TSZ; x++) {
                float b = 1, r, g, bl, a = 1;
                uint32_t *px = &texbuf[t * TSZ * TSZ + y * TSZ + x];
                switch (t) {
                case TX_COBBLE: {
                    /* ciottoli irregolari (Voronoi) con fughe e riflessi bagnati */
                    float f1 = 1e9f, f2 = 1e9f;
                    int cx = x / 16, cy = y / 16, i, j, id = 0;
                    for (j = cy - 1; j <= cy + 1; j++)
                        for (i = cx - 1; i <= cx + 1; i++) {
                            int wi = (i + 4) % 4, wj = (j + 4) % 4;
                            float ppx = i * 16 + 3 + hashf(wi, wj, 1) * 10, ppy = j * 16 + 3 + hashf(wi, wj, 2) * 10;
                            float d = sqrtf((x - ppx) * (x - ppx) + (y - ppy) * (y - ppy));
                            if (d < f1) { f2 = f1; f1 = d; id = wj * 4 + wi; }
                            else if (d < f2) f2 = d;
                        }
                    {
                        float e = f2 - f1, bev = e / 6 > 1 ? 1 : e / 6;
                        b = 0.52f + 0.22f * hashf(id, 0, 3) + 0.3f * (fbm(x, y, 8, 5) - 0.5f);
                        b *= 0.6f + 0.4f * bev;
                        if (e < 1.6f) b = 0.16f + 0.1f * fbm(x, y, 16, 7);
                        if (hashf(x, y, 9) > 0.986f) b += 0.3f;
                        r = b * 0.94f; g = b * 0.96f; bl = b * 1.04f;
                    }
                    break;
                }
                case TX_STONE: {
                    /* conci di pietra sfalsati, crepe e muschio */
                    int row = y / 16, off = (row & 1) * 16, lx = (x + off) % 32, ly = y % 16, bi = ((x + off) % 64) / 32;
                    float n = vnoise2(x / 4.0f, y / 4.0f, 16, 16, 7), m = fbm(x, y, 4, 11);
                    if (lx < 2 || ly < 2) b = 0.2f + 0.08f * fbm(x, y, 16, 3);
                    else {
                        b = 0.58f + 0.18f * hashf(bi, row, 4) + 0.32f * (fbm(x, y, 8, 13) - 0.5f);
                        if (lx < 4 || ly < 4) b *= 0.85f;
                        if (lx > 29 || ly > 13) b *= 1.08f;
                        if (fabsf(n - 0.5f) < 0.014f) b *= 0.5f;
                    }
                    r = b; g = b * 0.97f; bl = b * 0.95f;
                    if (m > 0.6f) { r *= 0.8f; g *= 1.05f; bl *= 0.75f; }
                    break;
                }
                case TX_WOOD: {
                    int pl = x / 16, lx = x % 16;
                    float gr = 0.5f + 0.5f * sinf(y * 0.22f + fbm(x, y, 4, 15) * 9 + pl * 3);
                    b = 0.46f + 0.16f * gr + 0.18f * hashf(pl, 0, 5) + 0.14f * (vnoise2(x * 8 / 64.0f, y * 1 / 64.0f, 8, 1, 6) - 0.5f);
                    if (lx == 0) b = 0.14f;
                    if (lx == 1) b *= 0.75f;
                    r = b; g = b * 0.88f; bl = b * 0.76f;
                    break;
                }
                case TX_SLATE: {
                    int row = y / 8, off = (row & 1) * 4, sx = (x + off) % 8, ly = y % 8;
                    b = 0.45f + 0.22f * hashf((x + off) / 8, row, 8) + 0.2f * (fbm(x, y, 8, 17) - 0.5f);
                    b *= 0.7f + 0.3f * (ly / 7.0f);
                    if (ly == 7 || sx == 0) b = 0.14f;
                    r = b * 0.9f; g = b * 0.95f; bl = b * 1.06f;
                    break;
                }
                case TX_CLOTH:
                    b = 0.68f + 0.26f * (fbm(x, y, 4, 21) - 0.5f) + 0.05f * ((x + y) & 1) + 0.1f * sinf(x * 0.2f + fbm(x, y, 2, 23) * 4);
                    if (hashf(x, y, 25) > 0.97f) b *= 0.8f;
                    r = g = bl = b;
                    break;
                case TX_METAL: {
                    int lx = x % 32, ly = y % 32;
                    b = 0.66f + 0.18f * (vnoise2(x * 4 / 64.0f, y * 32 / 64.0f, 4, 32, 31) - 0.5f) + 0.14f * (fbm(x, y, 4, 33) - 0.5f);
                    if (lx == 0 || ly == 0) b = 0.3f;
                    if (lx == 1 || ly == 1) b *= 1.2f;
                    {
                        int rx = lx - 5, ry = ly - 5;
                        if (rx * rx + ry * ry <= 3) b = 0.95f;
                        else if (rx * rx + ry * ry <= 6) b = 0.35f;
                    }
                    if (fabsf(vnoise2(x / 8.0f, y / 8.0f, 8, 8, 35) - 0.5f) < 0.01f) b *= 1.3f;
                    r = b * 0.95f; g = b * 0.97f; bl = b;
                    break;
                }
                case TX_BARK:
                    b = 0.5f + 0.32f * (vnoise2(x * 8 / 64.0f, y * 2 / 64.0f, 8, 2, 41) - 0.5f) + 0.2f * (fbm(x, y, 8, 43) - 0.5f);
                    if (vnoise2(x * 16 / 64.0f, y * 2 / 64.0f, 16, 2, 45) < 0.28f) b *= 0.5f;
                    r = b; g = b * 0.92f; bl = b * 0.85f;
                    break;
                case TX_BRICK: {
                    /* mattoni con fughe, alcuni scheggiati o anneriti */
                    int row = y / 8, off = (row & 1) * 8, lx = (x + off) % 16, ly = y % 8, bi = (x + off) / 16;
                    if (lx == 0 || ly == 0) b = 0.3f + 0.1f * fbm(x, y, 16, 61);
                    else {
                        b = 0.55f + 0.22f * hashf(bi, row, 62) + 0.22f * (fbm(x, y, 16, 63) - 0.5f);
                        if (lx == 1 || ly == 1) b *= 1.1f;
                        if (lx == 15 || ly == 7) b *= 0.82f;
                        if (hashf(bi, row, 64) > 0.9f) b *= 0.65f;
                    }
                    r = b; g = b * 0.9f; bl = b * 0.86f;
                    break;
                }
                case TX_PLASTER: {
                    /* intonaco macchiato e scrostato, con mattoni a vista */
                    float n = fbm(x, y, 4, 71), st = fbm(x, y, 8, 73);
                    b = 0.72f + 0.16f * (st - 0.5f) - 0.12f * (y / 63.0f) * fbm(x, y, 2, 75);
                    r = b; g = b * 0.97f; bl = b * 0.9f;
                    if (n > 0.66f) {
                        int row = y / 6, lx = (x + (row & 1) * 6) % 12;
                        float bb = (lx == 0 || y % 6 == 0) ? 0.3f : 0.5f + 0.15f * hashf((x + (row & 1) * 6) / 12, row, 77);
                        r = bb * 1.05f; g = bb * 0.78f; bl = bb * 0.68f;
                    } else if (n > 0.63f) { r *= 0.7f; g *= 0.7f; bl *= 0.7f; }
                    if (fabsf(vnoise2(x / 6.0f, y / 6.0f, 11, 11, 79) - 0.5f) < 0.01f) { r *= 0.6f; g *= 0.6f; bl *= 0.6f; }
                    break;
                }
                case TX_FLAG: {
                    /* lastre del marciapiede */
                    int row = y / 21, off = (row % 2) * 13, lx = (x + off) % 32, ly = y % 21;
                    if (lx == 0 || ly == 0 || y >= 63) b = 0.2f;
                    else {
                        b = 0.6f + 0.18f * hashf((x + off) / 32, row, 81) + 0.25f * (fbm(x, y, 8, 83) - 0.5f);
                        if (lx == 1 || ly == 1) b *= 1.12f;
                        if (fabsf(vnoise2(x / 5.0f, y / 5.0f, 13, 13, 85) - 0.5f) < 0.012f) b *= 0.55f;
                    }
                    r = b * 0.97f; g = b * 0.97f; bl = b;
                    break;
                }
                case TX_GRASS:
                    b = 0.55f + 0.25f * (fbm(x, y, 8, 91) - 0.5f) + 0.22f * (vnoise2(x * 32 / 64.0f, y * 8 / 64.0f, 32, 8, 93) - 0.5f);
                    if (hashf(x, y, 95) > 0.93f) b *= 1.25f;
                    if (fbm(x, y, 4, 97) > 0.68f) { r = b * 0.95f; g = b * 0.85f; bl = b * 0.6f; break; }   /* chiazze secche */
                    r = b * 0.82f; g = b; bl = b * 0.74f;
                    break;
                case TX_DIRT:
                    b = 0.5f + 0.25f * (fbm(x, y, 8, 101) - 0.5f);
                    if (hashf(x, y, 103) > 0.965f) b += 0.25f;
                    if (hashf(x, y, 105) > 0.97f) b *= 0.6f;
                    if (fabsf(vnoise2(x / 8.0f, y / 8.0f, 8, 8, 107) - 0.5f) < 0.02f) b *= 0.75f;   /* solchi */
                    r = b; g = b * 0.88f; bl = b * 0.74f;
                    break;
                case TX_ROCK:
                    b = 0.55f + 0.22f * (fbm(x, y, 8, 111) - 0.5f) + 0.13f * sinf(y * 0.4f + fbm(x, y, 4, 113) * 7);
                    if (fabsf(vnoise2(x / 6.0f, y / 6.0f, 11, 11, 115) - 0.5f) < 0.015f) b *= 0.45f;
                    if (fbm(x, y, 4, 117) > 0.66f) { r = b * 0.82f; g = b * 0.95f; bl = b * 0.75f; break; }  /* licheni */
                    r = b * 0.96f; g = b * 0.97f; bl = b;
                    break;
                case TX_GLOW: {
                    float dx = (x - 31.5f) / 31.5f, dy = (y - 31.5f) / 31.5f, d = sqrtf(dx * dx + dy * dy);
                    float k = 1 - d;
                    if (k < 0) k = 0;
                    r = g = bl = 1; a = k * k;
                    break;
                }
                case TX_WINDOW: {
                    /* vetrata ad arco acuto con piombature */
                    int inside, dx = x - 32;
                    float fy = y / 63.0f, dist;
                    if (y >= 22) { inside = dx >= -26 && dx <= 26; dist = 26 - (float)(dx < 0 ? -dx : dx); }
                    else {
                        float dl = sqrtf((float)((x - 58) * (x - 58) + (y - 22) * (y - 22)));
                        float dr = sqrtf((float)((x - 6) * (x - 6) + (y - 22) * (y - 22)));
                        dist = 52 - (dx < 0 ? dl : dr);
                        inside = dist > 0 && y > 0;
                    }
                    if (y > 61) { inside = 0; }
                    if (!inside || dist < 3) { r = g = bl = 0.1f; break; }
                    b = 0.8f + 0.2f * fbm(x, y, 4, 51);
                    r = b; g = b * (0.7f + 0.2f * fy); bl = b * (0.35f + 0.25f * fy);
                    if ((dx < 2 && dx > -2) || (y - 38 < 2 && y - 38 > -2)) { r = g = bl = 0.08f; }
                    else if (x % 8 == 0 || y % 9 == 0) { r *= 0.55f; g *= 0.55f; bl *= 0.55f; }
                    break;
                }
                default:
                    r = g = bl = 1;
                }
                *px = pack(r, g, bl, a);
            }
    r_textures(texbuf, NTEX, TSZ);
}

/* ------------------------------------------------------------------ */
/* luce: luna fredda + luci calde, sfumata, con occlusione agli angoli */
/* ------------------------------------------------------------------ */
typedef struct { float r, g, b; } Lt;
static Lt cur_lt;
static float lampL[MH][MW][3];
static int cur_tex = TX_WHITE;
static float cur_ts = 32;
static V3 camR, camU;   /* assi della telecamera per i billboard */
static float cur_gy;    /* quota del suolo per l'oggetto che si sta disegnando */
static V3 camEye;
#define TEXS(t, s) (cur_tex = (t), cur_ts = (s))

static void add_light(float lx, float lz, float R, float s, float r, float g, float b)
{
    int tx0 = (int)((lx - R) / TS), tx1 = (int)((lx + R) / TS);
    int ty0 = (int)((lz - R) / TS), ty1 = (int)((lz + R) / TS), tx, ty;
    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++) {
            float dx, dz, d, k;
            if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) continue;
            dx = tx * TS + 8 - lx; dz = ty * TS + 8 - lz; d = sqrtf(dx * dx + dz * dz);
            if (d >= R) continue;
            k = 1 - d / R; k = k * k * s;
            lampL[ty][tx][0] += k * r; lampL[ty][tx][1] += k * g; lampL[ty][tx][2] += k * b;
        }
}

static void build_lights(void)
{
    int tx, ty, i;
    memset(lampL, 0, sizeof(lampL));
    for (ty = 0; ty < MH; ty++)
        for (tx = 0; tx < MW; tx++) {
            if (map[ty][tx] == T_LAMP) add_light(tx * TS + 8.0f, ty * TS + 8.0f, 100, 0.95f, 1.0f, 0.72f, 0.42f);
            if (map[ty][tx] == T_FIRE) add_light(tx * TS + 8.0f, ty * TS + 8.0f, 120, 1.05f, 1.0f, 0.5f, 0.2f);
        }
    for (i = 0; i < nbraz; i++)
        if (braz[i].lit) add_light((braz[i].tx - WX0) * TS + 8.0f, (braz[i].ty - WY0) * TS + 8.0f, 150, 1.1f, 1.0f, 0.55f, 0.25f);
}

static Lt light_point(float wx, float wz, float ao)
{
    float fx = (wx - 8) / TS, fz = (wz - 8) / TS, ax, az, dx, dz, k, w[4];
    int tx = (int)floorf(fx), tz = (int)floorf(fz), i;
    Lt l = { 0.17f, 0.20f, 0.31f };
    if (endingSeen) { l.r = 0.36f; l.g = 0.30f; l.b = 0.30f; }   /* luce dell'alba */
    ax = fx - tx; az = fz - tz;
    w[0] = (1 - ax) * (1 - az); w[1] = ax * (1 - az); w[2] = (1 - ax) * az; w[3] = ax * az;
    for (i = 0; i < 4; i++) {
        int sx = tx + (i & 1), sz = tz + (i >> 1);
        if (sx < 0 || sz < 0 || sx >= MW || sz >= MH) continue;
        l.r += lampL[sz][sx][0] * w[i]; l.g += lampL[sz][sx][1] * w[i]; l.b += lampL[sz][sx][2] * w[i];
    }
    dx = wx - P.x; dz = wz - P.y;
    k = 1 - sqrtf(dx * dx + dz * dz) / 125.0f;
    if (k > 0) { k *= k; l.r += k * 0.95f; l.g += k * 0.74f; l.b += k * 0.48f; }
    if (Bs.phase && in_arena(wx, wz)) { l.r += 0.18f; l.g += 0.06f; }
    if (!in_town((int)(wx / TS), (int)(wz / TS))) { l.r += 0.03f; l.g += 0.05f; l.b += 0.08f; }   /* luna all'aperto */
    l.r *= ao; l.g *= ao; l.b *= ao;
    if (l.r > 1.3f) l.r = 1.3f;
    if (l.g > 1.25f) l.g = 1.25f;
    if (l.b > 1.25f) l.b = 1.25f;
    return l;
}

static float light_at(float wx, float wz) { cur_lt = light_point(wx, wz, 1); return 1.0f; }

static uint32_t litv(uint32_t c, Lt l, float f)
{
    float gain = cur_tex == TX_WHITE ? 1.0f : 1.45f;
    int r = (int)((c & 255) * l.r * f * gain), g = (int)(((c >> 8) & 255) * l.g * f * gain), b = (int)(((c >> 16) & 255) * l.b * f * gain);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return (c & 0xFF000000u) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}
static uint32_t lit(uint32_t c, float f) { return litv(c, cur_lt, f); }

/* ------------------------------------------------------------------ */
/* primitive                                                           */
/* ------------------------------------------------------------------ */
static void emit_quad(V3 p0, V3 p1, V3 p2, V3 p3, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
                      float u0, float v0, float u1, float v1, float u2, float v2, float u3, float v3_, int tex, int blend)
{
    RV a, b, c, d;
    a.x = p0.x; a.y = p0.y; a.z = p0.z; a.u = u0; a.v = v0; a.c = c0;
    b.x = p1.x; b.y = p1.y; b.z = p1.z; b.u = u1; b.v = v1; b.c = c1;
    c.x = p2.x; c.y = p2.y; c.z = p2.z; c.u = u2; c.v = v2; c.c = c2;
    d.x = p3.x; d.y = p3.y; d.z = p3.z; d.u = u3; d.v = v3_; d.c = c3;
    r_tri(&a, &b, &c, tex, blend);
    r_tri(&a, &c, &d, tex, blend);
}

static int auto_blend(uint32_t c) { return (c >> 24) < 255 ? RB_ALPHA : RB_OPAQUE; }

static void quad(V3 a, V3 b, V3 c, V3 d, uint32_t col)
{
    emit_quad(a, b, c, d, col, col, col, col, 0, 0, 1, 0, 1, 1, 0, 1, TX_WHITE, auto_blend(col));
}

/* scatola orientata: x,z centro; y0 base; hl mezza lunghezza (avanti), hw mezza larghezza.
 * usa la texture corrente (TEXS); emis=1: colore pieno senza luce */
static void box(float x, float y0, float z, float hl, float h, float hw, float yaw,
                uint32_t side, uint32_t top, float L, int emis)
{
    float c = cosf(yaw), s = sinf(yaw), ts = cur_ts;
    float lx[4] = { hl, hl, -hl, -hl }, lz[4] = { hw, -hw, -hw, hw };
    float aob = y0 < 1 ? 0.55f : 0.85f;
    int tex = cur_tex, blend = auto_blend(side), i;
    y0 += cur_gy;
    V3 p[8];
    uint32_t ct;
    for (i = 0; i < 4; i++) {
        float wx = x + c * lx[i] - s * lz[i], wz = z + s * lx[i] + c * lz[i];
        p[i] = v3(wx, y0, wz);
        p[i + 4] = v3(wx, y0 + h, wz);
    }
    ct = emis ? top : lit(top, L);
    if (ts > 0)
        emit_quad(p[4], p[5], p[6], p[7], ct, ct, ct, ct, p[4].x / ts, p[4].z / ts, p[5].x / ts, p[5].z / ts,
                  p[6].x / ts, p[6].z / ts, p[7].x / ts, p[7].z / ts, tex, blend);
    else
        emit_quad(p[4], p[5], p[6], p[7], ct, ct, ct, ct, 0, 0, 1, 0, 1, 1, 0, 1, tex, blend);
    for (i = 0; i < 4; i++) {
        int j = (i + 1) & 3;
        float nx = (p[i].x + p[j].x) * 0.5f - x, nz = (p[i].z + p[j].z) * 0.5f - z;
        float nl = sqrtf(nx * nx + nz * nz) + 0.0001f;
        float f = 0.55f + 0.34f * (0.5f + 0.5f * (nx * 0.55f + nz * 0.83f) / nl);
        uint32_t cT = emis ? side : lit(side, L * f), cB = emis ? side : lit(side, L * f * aob);
        if (ts > 0) {
            float ex = p[j].x - p[i].x, ez = p[j].z - p[i].z, el = sqrtf(ex * ex + ez * ez) + 0.0001f;
            float ui = (p[i].x * ex + p[i].z * ez) / el / ts, uj = ui + el / ts;
            float vb = -y0 / ts, vt = -(y0 + h) / ts;
            emit_quad(p[i], p[j], p[j + 4], p[i + 4], cB, cB, cT, cT, ui, vb, uj, vb, uj, vt, ui, vt, tex, blend);
        } else
            emit_quad(p[i], p[j], p[j + 4], p[i + 4], cB, cB, cT, cT, 0, 1, 1, 1, 1, 0, 0, 0, tex, blend);
    }
}

static void cube(float x, float y0, float z, float r, float yaw, uint32_t col, float L, int emis)
{
    int t = cur_tex; float s = cur_ts;
    TEXS(TX_WHITE, 32);
    box(x, y0, z, r, r * 2, r, yaw, col, col, L, emis);
    TEXS(t, s);
}

static void fan(float x, float z, float y, float r0, float r1, float a0, float a1, uint32_t col, int seg)
{
    int i, bl = auto_blend(col);
    y += cur_gy;
    for (i = 0; i < seg; i++) {
        float aa = a0 + (a1 - a0) * i / seg, ab = a0 + (a1 - a0) * (i + 1) / seg;
        float ca = cosf(aa), sa = sinf(aa), cb = cosf(ab), sb = sinf(ab);
        V3 p0 = v3(x + ca * r0, y, z + sa * r0), p1 = v3(x + ca * r1, y, z + sa * r1);
        V3 p2 = v3(x + cb * r1, y, z + sb * r1), p3 = v3(x + cb * r0, y, z + sb * r0);
        emit_quad(p0, p1, p2, p3, col, col, col, col, 0, 0, 1, 0, 1, 1, 0, 1, TX_WHITE, bl);
    }
}

/* alone luminoso rivolto alla telecamera (additivo) */
static void glow(float x, float y, float z, float s, uint32_t col)
{
    V3 p0;
    V3 p1, p2, p3;
    y += cur_gy;
    p0 = v3(x - camR.x * s - camU.x * s, y - camR.y * s - camU.y * s, z - camR.z * s - camU.z * s);
    p1 = v3(x + camR.x * s - camU.x * s, y + camR.y * s - camU.y * s, z + camR.z * s - camU.z * s);
    p2 = v3(x + camR.x * s + camU.x * s, y + camR.y * s + camU.y * s, z + camR.z * s + camU.z * s);
    p3 = v3(x - camR.x * s + camU.x * s, y - camR.y * s + camU.y * s, z - camR.z * s + camU.z * s);
    emit_quad(p0, p1, p2, p3, col, col, col, col, 0, 0, 1, 0, 1, 1, 0, 1, TX_GLOW, RB_ADD);
}

/* macchia morbida a terra: luce (additiva) o ombra (alpha) */
static void ground_spot(float x, float z, float r, float y, uint32_t col, int blend)
{
    y += cur_gy;
    emit_quad(v3(x - r, y, z - r), v3(x + r, y, z - r), v3(x + r, y, z + r), v3(x - r, y, z + r),
              col, col, col, col, 0, 0, 1, 0, 1, 1, 0, 1, TX_GLOW, blend);
}

static void shadow(float x, float z, float r) { ground_spot(x, z, r * 1.7f, 0.4f, rgba(0, 200), RB_ALPHA); }

static void flame(float x, float y, float z, float sz, int seed)
{
    float f = (float)(hash2(frame / 4, seed) % 4);
    int t = cur_tex; float s = cur_ts;
    TEXS(TX_WHITE, 32);
    box(x, y, z, sz, sz * 1.6f + f * 0.5f, sz, frame * 0.05f + seed, RGB(255, 120, 30), RGB(255, 180, 70), 1, 1);
    box(x, y + sz * 0.4f, z, sz * 0.5f, sz * 2.0f + f * 0.6f, sz * 0.5f, -frame * 0.07f, RGB(255, 225, 150), RGB(255, 245, 200), 1, 1);
    TEXS(t, s);
    glow(x, y + sz * 1.5f, z, sz * 5 + f, rgba(RGB(255, 140, 50), 150));
    glow(x, y + sz * 1.2f, z, sz * 2.2f, rgba(RGB(255, 220, 150), 200));
    if ((frame + seed) % 7 == 0) spawn_parts(x, z, 1, RGB(255, 150, 50), 0.35f);
}

/* ------------------------------------------------------------------ */
/* mondo                                                               */
/* ------------------------------------------------------------------ */
#define GR 14
static Lt cgrid[2 * GR + 3][2 * GR + 3];
static int cg_x0, cg_z0;

static void build_corner_grid(int tcx, int tcz)
{
    int i, j;
    cg_x0 = tcx - GR; cg_z0 = tcz - GR;
    for (j = 0; j < 2 * GR + 3; j++)
        for (i = 0; i < 2 * GR + 3; i++) {
            int cx = cg_x0 + i, cz = cg_z0 + j, walls = 0, dx, dz;
            for (dz = -1; dz <= 0; dz++)
                for (dx = -1; dx <= 0; dx++) {
                    int tx = cx + dx, tz = cz + dz;
                    if (tx < 0 || tz < 0 || tx >= MW || tz >= MH || map[tz][tx] == T_WALL) walls++;
                }
            cgrid[j][i] = light_point(cx * (float)TS, cz * (float)TS, walls >= 4 ? 1.0f : 1.0f - 0.2f * walls);
        }
}

static Lt cgl(int cx, int cz)
{
    int i = cx - cg_x0, j = cz - cg_z0;
    if (i < 0) i = 0;
    if (j < 0) j = 0;
    if (i > 2 * GR + 2) i = 2 * GR + 2;
    if (j > 2 * GR + 2) j = 2 * GR + 2;
    return cgrid[j][i];
}

static float pb_x, pb_z, pb_c, pb_s, pb_roll, pb_y;
static float mvAmt, lastWalk;
static int pb_rolling;

static V3 L2W(float lf, float lu, float ls)
{
    if (pb_rolling) {
        float df = lf, du = lu - 5.5f, ca = cosf(pb_roll), sa = sinf(pb_roll);
        du *= 0.75f;
        lf = df * ca + du * sa;
        lu = 5.5f + (-df * sa + du * ca);
    }
    return v3(pb_x + pb_c * lf - pb_s * ls, lu + pb_y, pb_z + pb_s * lf + pb_c * ls);
}

/* --- forme arrotondate per il corpo --- */
static float limb_taper = 1.0f;
static int mat_metal;      /* 1 = piastre d'acciaio con riflessi */
static int hi_detail;      /* 1 = piu' poligoni (personaggio) */

static float soft_shade(float nx, float ny, float nz)
{
    float dl = nx * 0.33f + ny * 0.72f + nz * 0.6f;
    float rim = 1.0f - fabsf(nx * camR.z - nz * camR.x) ;
    (void)rim;
    return 0.4f + 0.55f * (dl > 0 ? dl : 0) + 0.12f * (ny > 0 ? ny : 0) + 0.06f;
}

/* colore di una superficie metallica: diffusa scura + riflesso speculare verso la telecamera */
static uint32_t metal_col(uint32_t col, float nx, float ny, float nz, V3 p)
{
    float vx = camEye.x - p.x, vy = camEye.y - p.y, vz = camEye.z - p.z, vl = sqrtf(vx * vx + vy * vy + vz * vz) + 0.001f;
    float hx = 0.33f + vx / vl, hy = 0.72f + vy / vl, hz = 0.6f + vz / vl, hl = sqrtf(hx * hx + hy * hy + hz * hz) + 0.001f;
    float sp = (nx * hx + ny * hy + nz * hz) / hl, dl = nx * 0.33f + ny * 0.72f + nz * 0.6f, rim;
    uint32_t c;
    int r, g, b, add;
    if (sp < 0) sp = 0;
    sp = sp * sp; sp = sp * sp; sp = sp * sp;          /* esponente 8 */
    rim = 1 - fabsf((nx * vx + ny * vy + nz * vz) / vl);
    c = lit(col, 0.28f + 0.5f * (dl > 0 ? dl : 0) + 0.18f * rim * rim);
    add = (int)(sp * 190);
    r = (int)(c & 255) + (int)(add * cur_lt.r); g = (int)((c >> 8) & 255) + (int)(add * cur_lt.g); b = (int)((c >> 16) & 255) + (int)(add * cur_lt.b);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static void tri_v(V3 a, uint32_t ca, float ua, float va, V3 b, uint32_t cb, float ub, float vb,
                  V3 c, uint32_t cc, float uc, float vc, int tex)
{
    RV A, B, C;
    A.x = a.x; A.y = a.y; A.z = a.z; A.u = ua; A.v = va; A.c = ca;
    B.x = b.x; B.y = b.y; B.z = b.z; B.u = ub; B.v = vb; B.c = cb;
    C.x = c.x; C.y = c.y; C.z = c.z; C.u = uc; C.v = vc; C.c = cc;
    r_tri(&A, &B, &C, tex, RB_OPAQUE);
}

/* cilindro a sezione ellittica lungo un segmento (w = semiasse laterale, d = frontale),
 * che si assottiglia di limb_taper verso b; ombreggiatura morbida per vertice */
static void limb(V3 a, V3 b, float w, float d, uint32_t col)
{
    float ax = b.x - a.x, ay = b.y - a.y, az = b.z - a.z, len = sqrtf(ax * ax + ay * ay + az * az) + 0.0001f;
    float rx = -pb_s, rz = pb_c, dp, sx, sy, sz, sl, fx, fy, fz, tp = limb_taper;
    int seg = hi_detail ? ((w + d > 1.5f) ? 14 : 10) : ((w + d > 1.5f) ? 10 : 7), k, tex = cur_tex;
    float ts = cur_ts, vlen = ts > 0 ? len / ts : 1;
    V3 ra[15], rb[15];
    uint32_t ca[15], cb[15];
    ax /= len; ay /= len; az /= len;
    dp = rx * ax + rz * az;
    sx = rx - ax * dp; sy = -ay * dp; sz = rz - az * dp;
    sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl < 0.01f) { sx = 1; sy = 0; sz = 0; sl = 1; }
    sx /= sl; sy /= sl; sz /= sl;
    fx = ay * sz - az * sy; fy = az * sx - ax * sz; fz = ax * sy - ay * sx;
    for (k = 0; k <= seg; k++) {
        float t = 2 * PI * k / seg, c = cosf(t), sn = sinf(t);
        float ox = sx * w * c + fx * d * sn, oy = sy * w * c + fy * d * sn, oz = sz * w * c + fz * d * sn;
        float nx = sx * c / w + fx * sn / d, ny = sy * c / w + fy * sn / d, nz = sz * c / w + fz * sn / d;
        float nl = sqrtf(nx * nx + ny * ny + nz * nz) + 0.0001f, sh;
        nx /= nl; ny /= nl; nz /= nl;
        sh = soft_shade(nx, ny, nz);
        ra[k] = v3(a.x + ox, a.y + oy, a.z + oz);
        rb[k] = v3(b.x + ox * tp, b.y + oy * tp, b.z + oz * tp);
        if (mat_metal) { ca[k] = metal_col(col, nx, ny, nz, ra[k]); cb[k] = metal_col(col, nx, ny, nz, rb[k]); }
        else { ca[k] = lit(col, sh * 0.9f); cb[k] = lit(col, sh); }
    }
    for (k = 0; k < seg; k++) {
        float u0 = (float)k / seg, u1 = (float)(k + 1) / seg;
        tri_v(ra[k], ca[k], u0, vlen, ra[k + 1], ca[k + 1], u1, vlen, rb[k + 1], cb[k + 1], u1, 0, tex);
        tri_v(ra[k], ca[k], u0, vlen, rb[k + 1], cb[k + 1], u1, 0, rb[k], cb[k], u0, 0, tex);
    }
    {   /* tappi */
        uint32_t ct = lit(col, soft_shade(ax, ay, az)), cbt = lit(col, soft_shade(-ax, -ay, -az));
        for (k = 0; k < seg; k++) {
            tri_v(b, ct, 0.5f, 0.5f, rb[k], cb[k], 0, 0, rb[k + 1], cb[k + 1], 1, 0, tex);
            tri_v(a, cbt, 0.5f, 0.5f, ra[k + 1], ca[k + 1], 1, 0, ra[k], ca[k], 0, 0, tex);
        }
    }
}

/* ellissoide in coordinate del corpo (testa, mani, articolazioni) */
static void blobL(float cf, float cu, float cs, float rf, float ru, float rs, uint32_t col, int rings, int segs)
{
    int r, k, tex = cur_tex;
    if (hi_detail) { rings = rings * 3 / 2; segs = segs * 3 / 2; }
    for (r = 0; r < rings; r++) {
        float t0 = PI * r / rings, t1 = PI * (r + 1) / rings;
        for (k = 0; k < segs; k++) {
            float p0 = 2 * PI * k / segs, p1 = 2 * PI * (k + 1) / segs;
            float th[4] = { t0, t0, t1, t1 }, ph[4] = { p0, p1, p1, p0 };
            V3 pos[4];
            uint32_t cc[4];
            int q;
            for (q = 0; q < 4; q++) {
                float st = sinf(th[q]), ct = cosf(th[q]), cp = cosf(ph[q]), sp = sinf(ph[q]);
                float lf = st * cp, lu = ct, ls = st * sp;
                float nf = lf / rf, nu = lu / ru, ns = ls / rs, nl = sqrtf(nf * nf + nu * nu + ns * ns) + 0.0001f;
                float wx, wz;
                nf /= nl; nu /= nl; ns /= nl;
                wx = pb_c * nf - pb_s * ns; wz = pb_s * nf + pb_c * ns;
                pos[q] = L2W(cf + lf * rf, cu + lu * ru, cs + ls * rs);
                cc[q] = mat_metal ? metal_col(col, wx, nu, wz, pos[q]) : lit(col, soft_shade(wx, nu, wz));
            }
            tri_v(pos[0], cc[0], (float)k / segs, (float)r / rings, pos[1], cc[1], (float)(k + 1) / segs, (float)r / rings,
                  pos[2], cc[2], (float)(k + 1) / segs, (float)(r + 1) / rings, tex);
            tri_v(pos[0], cc[0], (float)k / segs, (float)r / rings, pos[2], cc[2], (float)(k + 1) / segs, (float)(r + 1) / rings,
                  pos[3], cc[3], (float)k / segs, (float)(r + 1) / rings, tex);
        }
    }
}

/* telo del mantello: griglia di vertici che ondeggia */
static void cloth_cape(float shU, float lean, float sway, uint32_t col)
{
    enum { CW = 7, CH = 4 };
    V3 pt[CH + 1][CW + 1];
    float nrm[CH + 1][CW + 1][3];
    int i, j, tex = cur_tex;
    for (i = 0; i <= CH; i++)
        for (j = 0; j <= CW; j++) {
            float t = (float)i / CH, sj = ((float)j / CW) * 2 - 1;
            float half = 2.6f + t * 0.9f;
            float back = 1.3f + t * (0.7f + mvAmt * 2.4f) + sinf(frame * 0.07f + j * 0.9f + i) * 0.25f * t + sway * 0.5f * t;
            float lf = lean * 0.8f - back + 1.0f * sj * sj * (1 - 0.4f * t);
            float lu = shU + 0.9f - t * (shU - 6.5f) + (sj * sj) * 0.6f * (1 - t);
            pt[i][j] = L2W(lf, lu, sj * half);
        }
    for (i = 0; i <= CH; i++)
        for (j = 0; j <= CW; j++) {
            int i0 = i > 0 ? i - 1 : i, i1 = i < CH ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < CW ? j + 1 : j;
            float ux = pt[i1][j].x - pt[i0][j].x, uy = pt[i1][j].y - pt[i0][j].y, uz = pt[i1][j].z - pt[i0][j].z;
            float vx = pt[i][j1].x - pt[i][j0].x, vy = pt[i][j1].y - pt[i][j0].y, vz = pt[i][j1].z - pt[i][j0].z;
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz) + 0.0001f;
            nrm[i][j][0] = nx / nl; nrm[i][j][1] = ny / nl; nrm[i][j][2] = nz / nl;
        }
    for (i = 0; i < CH; i++)
        for (j = 0; j < CW; j++) {
            uint32_t c[4];
            int q, ii[4] = { i, i, i + 1, i + 1 }, jj[4] = { j, j + 1, j + 1, j };
            for (q = 0; q < 4; q++) {
                float *n = nrm[ii[q]][jj[q]];
                float sh = soft_shade(fabsf(n[0]), fabsf(n[1]), fabsf(n[2]));
                if (ii[q] == CH) sh *= 0.85f;
                c[q] = lit(col, sh);
            }
            tri_v(pt[i][j], c[0], (float)j / 3, (float)i / 3, pt[i][j + 1], c[1], (float)(j + 1) / 3, (float)i / 3,
                  pt[i + 1][j + 1], c[2], (float)(j + 1) / 3, (float)(i + 1) / 3, tex);
            tri_v(pt[i][j], c[0], (float)j / 3, (float)i / 3, pt[i + 1][j + 1], c[2], (float)(j + 1) / 3, (float)(i + 1) / 3,
                  pt[i + 1][j], c[3], (float)j / 3, (float)(i + 1) / 3, tex);
        }
}

static void limbL(float f0, float u0, float s0, float f1, float u1, float s1, float w, float d, uint32_t col)
{
    limb(L2W(f0, u0, s0), L2W(f1, u1, s1), w, d, col);
}

static int is_wall(int x, int y) { return x < 0 || y < 0 || x >= MW || y >= MH || map[y][x] == T_WALL; }

/* altezza e materiale per isolato (blocchi 3x3), cosi' le facciate sono coerenti */
static float block_h(int tx, int ty) { return 36.0f + (float)(hash2(tx / 3 + 11, ty / 3 + 7) % 4) * 7; }
static int block_mat(int tx, int ty) { return (int)(hash2(tx / 3 + 50, ty / 3 + 3) % 3); }

static void draw_building(int tx, int ty, unsigned h, int cut)
{
    float x0 = (float)tx * TS, z0 = (float)ty * TS, cx = x0 + 8, cz = z0 + 8;
    float hb = block_h(tx, ty), dcam;
    int mat = block_mat(tx, ty), d, detail;
    static const int mtex[3] = { TX_STONE, TX_BRICK, TX_PLASTER };
    static const float mts[3] = { 24, 16, 28 };
    uint32_t wallc = mat == 0 ? RGB(104, 100, 102) : (mat == 1 ? RGB(150, 100, 86) : RGB(156, 146, 130));
    uint32_t trim = RGB(126, 120, 114);
    (void)cut;
    {
        float ex = cx - camEye.x, ez = cz - camEye.z;
        dcam = sqrtf(ex * ex + ez * ez);
    }
    detail = dcam < 150;
    light_at(cx, cz);

    /* corpo dell'edificio */
    TEXS(mtex[mat], mts[mat]);
    box(cx, 0, cz, 8, hb, 8, 0, wallc, wallc, 1, 0);
    TEXS(TX_STONE, 16);
    box(cx, 0, cz, 8.4f, mat == 0 ? 3.0f : 6.0f, 8.4f, 0, RGB(84, 80, 84), RGB(90, 86, 90), 1, 0);   /* zoccolo */
    if (detail) {
        box(cx, 14, cz, 8.35f, 1.1f, 8.35f, 0, trim, trim, 1, 0);                                 /* marcapiano */
        box(cx, hb - 2.2f, cz, 8.7f, 2.2f, 8.7f, 0, trim, trim, 1, 0);                            /* cornicione */
    }

    /* tetto spiovente in ardesia con frontoni */
    {
        int along_x = (is_wall(tx - 1, ty) && is_wall(tx + 1, ty)) || !(is_wall(tx, ty - 1) && is_wall(tx, ty + 1));
        float rh = 10.0f, ov = 1.2f, hbr = hb + cur_gy;
        uint32_t rc = RGB(92, 96, 112);
        Lt l = cur_lt;
        uint32_t c1, c2;
        TEXS(TX_SLATE, 14);
        if (along_x) {
            V3 a = v3(x0, hbr, z0 - ov), b = v3(x0 + TS, hbr, z0 - ov), r0 = v3(x0, hbr + rh, cz), r1 = v3(x0 + TS, hbr + rh, cz);
            V3 c = v3(x0, hbr, z0 + TS + ov), dd = v3(x0 + TS, hbr, z0 + TS + ov);
            c1 = litv(rc, l, 0.62f); c2 = litv(rc, l, 0.95f);
            emit_quad(a, b, r1, r0, c1, c1, c1, c1, x0 / 14, 0, (x0 + TS) / 14, 0, (x0 + TS) / 14, 1.0f, x0 / 14, 1.0f, TX_SLATE, RB_OPAQUE);
            emit_quad(c, dd, r1, r0, c2, c2, c2, c2, x0 / 14, 0, (x0 + TS) / 14, 0, (x0 + TS) / 14, 1.0f, x0 / 14, 1.0f, TX_SLATE, RB_OPAQUE);
            TEXS(mtex[mat], mts[mat]);
            if (!is_wall(tx - 1, ty) || block_h(tx - 1, ty) != hb) {
                uint32_t g = litv(wallc, l, 0.7f);
                emit_quad(v3(x0, hbr, z0), v3(x0, hbr, z0 + TS), v3(x0, hbr + rh, cz), v3(x0, hbr + rh, cz), g, g, g, g,
                          0, 1, 1, 1, 0.5f, 0.4f, 0.5f, 0.4f, mtex[mat], RB_OPAQUE);
            }
            if (!is_wall(tx + 1, ty) || block_h(tx + 1, ty) != hb) {
                uint32_t g = litv(wallc, l, 0.8f);
                emit_quad(v3(x0 + TS, hbr, z0), v3(x0 + TS, hbr, z0 + TS), v3(x0 + TS, hbr + rh, cz), v3(x0 + TS, hbr + rh, cz), g, g, g, g,
                          0, 1, 1, 1, 0.5f, 0.4f, 0.5f, 0.4f, mtex[mat], RB_OPAQUE);
            }
        } else {
            V3 a = v3(x0 - ov, hbr, z0), b = v3(x0 - ov, hbr, z0 + TS), r0 = v3(cx, hbr + rh, z0), r1 = v3(cx, hbr + rh, z0 + TS);
            V3 c = v3(x0 + TS + ov, hbr, z0), dd = v3(x0 + TS + ov, hbr, z0 + TS);
            c1 = litv(rc, l, 0.7f); c2 = litv(rc, l, 0.85f);
            emit_quad(a, b, r1, r0, c1, c1, c1, c1, z0 / 14, 0, (z0 + TS) / 14, 0, (z0 + TS) / 14, 1.0f, z0 / 14, 1.0f, TX_SLATE, RB_OPAQUE);
            emit_quad(c, dd, r1, r0, c2, c2, c2, c2, z0 / 14, 0, (z0 + TS) / 14, 0, (z0 + TS) / 14, 1.0f, z0 / 14, 1.0f, TX_SLATE, RB_OPAQUE);
            if (!is_wall(tx, ty - 1) || block_h(tx, ty - 1) != hb) {
                uint32_t g = litv(wallc, l, 0.65f);
                emit_quad(v3(x0, hbr, z0), v3(x0 + TS, hbr, z0), v3(cx, hbr + rh, z0), v3(cx, hbr + rh, z0), g, g, g, g,
                          0, 1, 1, 1, 0.5f, 0.4f, 0.5f, 0.4f, mtex[mat], RB_OPAQUE);
            }
            if (!is_wall(tx, ty + 1) || block_h(tx, ty + 1) != hb) {
                uint32_t g = litv(wallc, l, 0.9f);
                emit_quad(v3(x0, hbr, z0 + TS), v3(x0 + TS, hbr, z0 + TS), v3(cx, hbr + rh, z0 + TS), v3(cx, hbr + rh, z0 + TS), g, g, g, g,
                          0, 1, 1, 1, 0.5f, 0.4f, 0.5f, 0.4f, mtex[mat], RB_OPAQUE);
            }
        }
        /* comignoli e guglie */
        if (h % 6 == 0) {
            TEXS(TX_BRICK, 10);
            box(cx + 3, hb + 2, cz - 2, 1.9f, 13, 1.9f, 0, RGB(130, 90, 78), RGB(40, 36, 36), 1, 0);
            TEXS(TX_STONE, 10);
            box(cx + 3, hb + 15, cz - 2, 2.3f, 1, 2.3f, 0, trim, trim, 1, 0);
        } else if (h % 23 == 0 && detail) {
            TEXS(TX_SLATE, 12);
            box(cx, hb + 6, cz, 3.0f, 10, 3.0f, 0.785f, RGB(84, 88, 104), RGB(84, 88, 104), 1, 0);
            box(cx, hb + 16, cz, 1.5f, 10, 1.5f, 0.785f, RGB(80, 84, 100), RGB(80, 84, 100), 1, 0);
            TEXS(TX_METAL, 16);
            box(cx, hb + 26, cz, 0.35f, 7, 0.35f, 0, RGB(70, 70, 80), RGB(70, 70, 80), 1, 0);
        }
    }
    if (!detail) return;

    /* facciate rivolte alla strada */
    for (d = 0; d < 4; d++) {
        static const int ddx[4] = { 0, 0, 1, -1 }, ddy[4] = { 1, -1, 0, 0 };
        int nx = tx + ddx[d], ny = ty + ddy[d], litw, floor_i;
        unsigned hw = hash2(tx * 4 + d, ty);
        float yaw = d < 2 ? 0.0f : PI / 2, fx = (float)ddx[d], fz = (float)ddy[d];
        float px = fz != 0 ? 1.0f : 0.0f, pz = fx != 0 ? 1.0f : 0.0f;   /* direzione lungo la facciata */
        if (is_wall(nx, ny)) continue;

        /* lesena a un'estremita' e pluviale */
        TEXS(TX_STONE, 16);
        if (hw % 2) box(cx + fx * 8.3f + px * 7.3f, 0, cz + fz * 8.3f + pz * 7.3f, 0.7f, hb - 2, 0.7f, 0, trim, trim, 1, 0);
        if (hw % 11 == 0) {
            TEXS(TX_METAL, 8);
            box(cx + fx * 8.4f - px * 7.5f, 0, cz + fz * 8.4f - pz * 7.5f, 0.3f, hb - 1, 0.3f, 0, RGB(70, 72, 78), RGB(70, 72, 78), 1, 0);
        }

        /* piano terra: portone ad arco oppure finestra bassa */
        if (hw % 6 == 0) {
            float dx2 = cx + fx * 8.2f, dz2 = cz + fz * 8.2f;
            TEXS(TX_STONE, 10);
            box(dx2, 0, dz2, yaw == 0 ? 3.6f : 0.5f, 12, yaw == 0 ? 0.5f : 3.6f, 0, trim, trim, 1, 0);
            box(cx + fx * 9.6f, 0, cz + fz * 9.6f, yaw == 0 ? 4.2f : 1.6f, 0.6f, yaw == 0 ? 1.6f : 4.2f, 0, RGB(96, 92, 96), RGB(110, 106, 110), 1, 0);
            TEXS(TX_WOOD, 8);
            box(cx + fx * 8.65f, 0.6f, cz + fz * 8.65f, yaw == 0 ? 2.7f : 0.25f, 9.2f, yaw == 0 ? 0.25f : 2.7f, 0, RGB(110, 76, 52), RGB(110, 76, 52), 1, 0);
            box(cx + fx * 8.65f, 9.8f, cz + fz * 8.65f, yaw == 0 ? 1.8f : 0.25f, 1.2f, yaw == 0 ? 0.25f : 1.8f, 0, RGB(110, 76, 52), RGB(110, 76, 52), 1, 0);
            TEXS(TX_METAL, 6);
            box(cx + fx * 8.95f + px * 1.6f, 4.8f, cz + fz * 8.95f + pz * 1.6f, 0.35f, 0.7f, 0.35f, 0, RGB(200, 170, 100), RGB(200, 170, 100), 1, 0);
            floor_i = 1;
        } else floor_i = 0;

        /* finestre ai piani con cornice, davanzale e persiane */
        for (; floor_i < 3; floor_i++) {
            float wy = floor_i == 0 ? 4.0f : (floor_i == 1 ? 16.5f : 29.0f);
            float wh = floor_i == 0 ? 7.0f : 10.5f;
            unsigned hf = hash2(hw, floor_i);
            uint32_t wc;
            float wx = cx + fx * 8.35f, wz = cz + fz * 8.35f;
            if (wy + wh > hb - 3) break;
            if (hf % 4 == 0) continue;
            litw = hf % 5 < 2;
            wc = litw ? RGB(255, 196, 120) : RGB(34, 40, 60);
            if (litw && (hash2(frame / 50, tx + ty + floor_i) % 23 == 0)) wc = RGB(200, 150, 100);
            TEXS(TX_STONE, 8);
            box(cx + fx * 8.2f, wy - 1.0f, cz + fz * 8.2f, yaw == 0 ? 3.2f : 0.45f, wh + 2.0f, yaw == 0 ? 0.45f : 3.2f, 0, trim, trim, 1, 0);
            box(cx + fx * 8.7f, wy - 1.2f, cz + fz * 8.7f, yaw == 0 ? 3.4f : 0.9f, 0.8f, yaw == 0 ? 0.9f : 3.4f, 0, trim, trim, 1, 0);
            TEXS(TX_WINDOW, 0);
            box(wx + fx * 0.3f, wy, wz + fz * 0.3f, 2.4f, wh, 0.12f, yaw, wc, wc, 1, 1);
            if (litw) glow(wx + fx * 2.5f, wy + wh * 0.5f, wz + fz * 2.5f, 9, rgba(RGB(255, 170, 80), 55));
            else if (hf % 3 == 0) {
                /* persiane socchiuse */
                TEXS(TX_WOOD, 6);
                box(wx + fx * 0.6f + px * 3.6f, wy, wz + fz * 0.6f + pz * 3.6f, yaw == 0 ? 1.2f : 0.2f, wh, yaw == 0 ? 0.2f : 1.2f, 0, RGB(70, 84, 76), RGB(70, 84, 76), 1, 0);
                box(wx + fx * 0.6f - px * 3.6f, wy, wz + fz * 0.6f - pz * 3.6f, yaw == 0 ? 1.2f : 0.2f, wh, yaw == 0 ? 0.2f : 1.2f, 0, RGB(70, 84, 76), RGB(70, 84, 76), 1, 0);
            }
            /* balcone in ferro battuto al secondo piano */
            if (floor_i == 1 && hf % 5 == 1) {
                int k;
                float bx2 = cx + fx * 9.8f, bz2 = cz + fz * 9.8f;
                TEXS(TX_STONE, 8);
                box(bx2, wy - 1.6f, bz2, yaw == 0 ? 4.5f : 1.6f, 0.8f, yaw == 0 ? 1.6f : 4.5f, 0, trim, trim, 1, 0);
                TEXS(TX_METAL, 6);
                for (k = -3; k <= 3; k++)
                    box(cx + fx * 11.1f + px * k * 1.4f, wy - 0.8f, cz + fz * 11.1f + pz * k * 1.4f, 0.12f, 4.5f, 0.12f, 0, RGB(50, 50, 56), RGB(50, 50, 56), 1, 0);
                box(cx + fx * 11.1f, wy + 3.7f, cz + fz * 11.1f, yaw == 0 ? 4.4f : 0.2f, 0.35f, yaw == 0 ? 0.2f : 4.4f, 0, RGB(50, 50, 56), RGB(50, 50, 56), 1, 0);
            }
        }
    }
}

static void draw_prop(int t, int tx, int ty, unsigned h)
{
    float cx = tx * TS + 8.0f, cz = ty * TS + 8.0f, yaw = ((int)(h % 7) - 3) * 0.06f;
    float ex = cx - camEye.x, ez = cz - camEye.z;
    if (ex * ex + ez * ez < 30 * 30) return;   /* non coprire la visuale */
    light_at(cx, cz);
    switch (t) {
    case T_LAMP:
        TEXS(TX_METAL, 12);
        box(cx, 0, cz, 1.9f, 3, 1.9f, 0.785f, RGB(70, 70, 80), RGB(80, 80, 90), 1, 0);
        box(cx, 3, cz, 0.7f, 28, 0.7f, 0, RGB(64, 64, 74), RGB(64, 64, 74), 1, 0);
        box(cx, 12, cz, 1.1f, 1, 1.1f, 0.785f, RGB(70, 70, 80), RGB(70, 70, 80), 1, 0);
        box(cx, 29, cz, 3.2f, 0.8f, 0.6f, 0, RGB(64, 64, 74), RGB(64, 64, 74), 1, 0);
        TEXS(TX_WHITE, 32);
        box(cx, 31, cz, 2.0f, 5, 2.0f, 0.785f, RGB(255, 205, 130), RGB(255, 225, 160), 1, 1);
        TEXS(TX_METAL, 12);
        box(cx, 36, cz, 2.7f, 1.4f, 2.7f, 0.785f, RGB(60, 60, 70), RGB(70, 70, 80), 1, 0);
        box(cx, 37.4f, cz, 0.9f, 2, 0.9f, 0.785f, RGB(60, 60, 70), RGB(70, 70, 80), 1, 0);
        {   /* cono di luce nella nebbia */
            int k;
            V3 apex = v3(cx, 32 + cur_gy, cz);
            for (k = 0; k < 10; k++) {
                float a0 = 2 * PI * k / 10, a1 = 2 * PI * (k + 1) / 10;
                RV A, B, C;
                A.x = apex.x; A.y = apex.y; A.z = apex.z; A.u = A.v = 0.5f; A.c = rgba(RGB(255, 170, 90), 46);
                B.x = cx + cosf(a0) * 15; B.y = 0.8f + cur_gy; B.z = cz + sinf(a0) * 15; B.u = B.v = 0.5f; B.c = rgba(RGB(255, 170, 90), 0);
                C.x = cx + cosf(a1) * 15; C.y = 0.8f + cur_gy; C.z = cz + sinf(a1) * 15; C.u = C.v = 0.5f; C.c = rgba(RGB(255, 170, 90), 0);
                r_tri(&A, &B, &C, TX_WHITE, RB_ADD);
            }
        }
        glow(cx, 33.5f, cz, 15, rgba(RGB(255, 170, 80), 130));
        glow(cx, 33.5f, cz, 5, rgba(RGB(255, 235, 190), 220));
        ground_spot(cx, cz, 36, 0.5f, rgba(RGB(255, 160, 70), 70), RB_ADD);
        break;
    case T_GRAVE:
        TEXS(TX_COBBLE, 16);
        box(cx, 0, cz + 2, 3.5f, 1.2f, 6, 0, RGB(80, 70, 60), RGB(90, 78, 66), 1, 0);
        TEXS(TX_STONE, 10);
        if (h % 2) {
            box(cx, 0, cz - 2, 1.2f, 13, 1.0f, yaw, RGB(118, 116, 122), RGB(140, 138, 144), 1, 0);
            box(cx, 8, cz - 2, 1.2f, 1.6f, 4.0f, yaw, RGB(118, 116, 122), RGB(140, 138, 144), 1, 0);
        } else {
            box(cx, 0, cz - 2, 1.5f, 9, 4.2f, yaw, RGB(112, 110, 118), RGB(130, 128, 136), 1, 0);
            box(cx, 9, cz - 2, 1.5f, 2, 2.8f, yaw, RGB(112, 110, 118), RGB(130, 128, 136), 1, 0);
        }
        break;
    case T_CRATE:
        TEXS(TX_WOOD, 12);
        if (h % 2) {
            box(cx, 0, cz, 5.5f, 9, 5.5f, yaw, RGB(140, 104, 70), RGB(150, 112, 76), 1, 0);
            TEXS(TX_METAL, 8);
            box(cx, 4, cz, 5.7f, 1, 5.7f, yaw, RGB(70, 66, 66), RGB(70, 66, 66), 1, 0);
        } else {
            box(cx, 0, cz, 4.5f, 11, 4.5f, 0, RGB(124, 90, 60), RGB(104, 76, 52), 1, 0);
            box(cx, 0, cz, 4.5f, 11, 4.5f, 0.785f, RGB(124, 90, 60), RGB(104, 76, 52), 1, 0);
            TEXS(TX_METAL, 8);
            box(cx, 2, cz, 4.8f, 1, 4.8f, 0.785f, RGB(66, 66, 70), RGB(66, 66, 70), 1, 0);
            box(cx, 8, cz, 4.8f, 1, 4.8f, 0.785f, RGB(66, 66, 70), RGB(66, 66, 70), 1, 0);
        }
        break;
    case T_TREE: {
        int i;
        TEXS(TX_BARK, 10);
        box(cx, 0, cz, 2.3f, 30, 2.3f, 0.3f, RGB(84, 72, 64), RGB(84, 72, 64), 1, 0);
        box(cx, 0, cz, 3.4f, 3, 3.4f, 0.9f, RGB(80, 68, 60), RGB(80, 68, 60), 1, 0);
        for (i = 0; i < 6; i++) {
            float a = i * 2.3f + (h % 5), l = 6 + (i % 3) * 2.5f, y = 14 + i * 3.2f;
            box(cx + cosf(a) * l, y, cz + sinf(a) * l, l, 1.2f, 0.8f, a, RGB(84, 72, 64), RGB(96, 84, 74), 1, 0);
            box(cx + cosf(a) * l * 2, y + 1, cz + sinf(a) * l * 2, l * 0.4f, 0.8f, 0.6f, a + 0.6f, RGB(84, 72, 64), RGB(96, 84, 74), 1, 0);
        }
        break;
    }
    case T_WELL:
        /* pozzo di pietra con argano */
        pb_x = cx; pb_z = cz; pb_c = 1; pb_s = 0; pb_rolling = 0; pb_y = cur_gy;
        TEXS(TX_STONE, 10);
        limbL(0, 0, 0, 0, 5, 0, 5.5f, 5.5f, RGB(118, 114, 112));
        TEXS(TX_WHITE, 32);
        fan(cx, cz, 5.1f, 0, 4.4f, 0, 2 * PI, lit(RGB(20, 26, 38), 1), 10);
        TEXS(TX_WOOD, 8);
        box(cx - 4.6f, 5, cz, 0.6f, 9, 0.6f, 0, RGB(110, 84, 60), RGB(110, 84, 60), 1, 0);
        box(cx + 4.6f, 5, cz, 0.6f, 9, 0.6f, 0, RGB(110, 84, 60), RGB(110, 84, 60), 1, 0);
        box(cx, 13.5f, cz, 5.4f, 0.8f, 0.6f, 0, RGB(110, 84, 60), RGB(110, 84, 60), 1, 0);
        TEXS(TX_SLATE, 10);
        box(cx, 14.3f, cz, 6.2f, 1.2f, 4.0f, 0, RGB(90, 94, 108), RGB(90, 94, 108), 1, 0);
        TEXS(TX_METAL, 6);
        box(cx, 8, cz, 0.15f, 5.5f, 0.15f, 0, RGB(60, 60, 64), RGB(60, 60, 64), 1, 0);
        TEXS(TX_WOOD, 6);
        box(cx, 6.5f, cz, 0.9f, 1.6f, 0.9f, 0, RGB(100, 76, 54), RGB(100, 76, 54), 1, 0);
        break;
    case T_FIRE:
        TEXS(TX_STONE, 10);
        box(cx, 0, cz, 5, 6, 5, 0.785f, RGB(100, 96, 96), RGB(100, 96, 96), 1, 0);
        TEXS(TX_METAL, 10);
        box(cx, 6, cz, 7, 3, 7, 0, RGB(100, 90, 84), RGB(50, 30, 22), 1, 0);
        flame(cx, 9, cz, 3.6f, tx * 7 + ty);
        ground_spot(cx, cz, 46, 0.5f, rgba(RGB(255, 120, 40), 80), RB_ADD);
        break;
    }
}


static int is_ruin(int x, int y) { return x >= 0 && y >= 0 && x < MW && y < MH && map[y][x] == T_RUIN; }

/* ------------------------------------------------------------------ */
/* esterni: terreno con rilievi, alberi, rocce, rovine, acqua          */
/* ------------------------------------------------------------------ */
static void cone(float x, float y, float z, float r, float h, uint32_t col, int seg)
{
    int k, tex = cur_tex;
    V3 ap = v3(x, y + h + cur_gy, z);
    uint32_t ca = lit(col, 1.05f), cu = lit(col, 0.3f);
    for (k = 0; k < seg; k++) {
        float a0 = 2 * PI * k / seg, a1 = 2 * PI * (k + 1) / seg;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        V3 p0 = v3(x + c0 * r, y + cur_gy, z + s0 * r), p1 = v3(x + c1 * r, y + cur_gy, z + s1 * r);
        uint32_t k0 = lit(col, soft_shade(c0 * 0.8f, 0.5f, s0 * 0.8f)), k1 = lit(col, soft_shade(c1 * 0.8f, 0.5f, s1 * 0.8f));
        tri_v(ap, ca, 0.5f, 0, p0, k0, (float)k / seg * 2, 1, p1, k1, (float)(k + 1) / seg * 2, 1, tex);
        tri_v(v3(x, y + cur_gy, z), cu, 0.5f, 0.5f, p1, cu, 0, 0, p0, cu, 1, 0, tex);
    }
}

static void draw_pine(float cx, float cz, unsigned h)
{
    float s = 0.85f + (h % 5) * 0.08f, yaw = (h % 7) * 0.3f;
    uint32_t fol = (h % 3 == 0) ? RGB(58, 74, 62) : ((h % 3 == 1) ? RGB(66, 78, 64) : RGB(54, 66, 60));
    TEXS(TX_BARK, 8);
    box(cx, 0, cz, 1.1f * s, 14 * s, 1.1f * s, yaw, RGB(96, 80, 70), RGB(96, 80, 70), 1, 0);
    TEXS(TX_GRASS, 6);
    cone(cx, 6 * s, cz, 8.5f * s, 11 * s, fol, 8);
    cone(cx, 12 * s, cz, 6.8f * s, 10 * s, shade(fol, 1.05f), 8);
    cone(cx, 18 * s, cz, 4.8f * s, 9 * s, shade(fol, 1.1f), 7);
    cone(cx, 24 * s, cz, 2.6f * s, 7 * s, shade(fol, 1.15f), 6);
}

static void draw_boulder(float cx, float cz, unsigned h)
{
    float s = 0.8f + (h % 4) * 0.2f;
    pb_x = cx; pb_z = cz; pb_c = cosf(h * 0.1f); pb_s = sinf(h * 0.1f); pb_rolling = 0; pb_y = cur_gy;
    TEXS(TX_ROCK, 8);
    blobL(0, 2.5f * s, 0, 6.5f * s, 5.0f * s, 5.5f * s, RGB(116, 114, 118), 5, 8);
    blobL(3 * s, 1.5f * s, 2 * s, 3.5f * s, 3.0f * s, 3.0f * s, RGB(106, 104, 108), 4, 7);
    if (h % 2) blobL(-3 * s, 1 * s, -3 * s, 2.5f * s, 2.0f * s, 2.2f * s, RGB(100, 100, 104), 3, 6);
}

static void draw_ruin(int tx, int ty, unsigned h)
{
    float cx = tx * TS + 8.0f, cz = ty * TS + 8.0f, hh = 7 + (float)(h % 24);
    int col = !is_ruin(tx - 1, ty) && !is_ruin(tx + 1, ty) && !is_ruin(tx, ty - 1) && !is_ruin(tx, ty + 1);
    TEXS(TX_STONE, 20);
    if (col) {
        /* colonna spezzata */
        box(cx, 0, cz, 4.5f, 2, 4.5f, 0, RGB(118, 114, 112), RGB(126, 122, 120), 1, 0);
        pb_x = cx; pb_z = cz; pb_c = 1; pb_s = 0; pb_rolling = 0; pb_y = cur_gy;
        limb_taper = 0.92f;
        limbL(0, 2, 0, 0, 10 + hh, 0, 2.6f, 2.6f, RGB(124, 120, 118));
        limb_taper = 1.0f;
        box(cx + 3, 0, cz + 4, 2.5f, 2.5f, 1.6f, 0.6f, RGB(118, 114, 112), RGB(126, 122, 120), 1, 0);
        return;
    }
    box(cx, 0, cz, 8, hh, 8, 0, RGB(112, 108, 106), RGB(96, 100, 92), 1, 0);
    /* blocchi sbrecciati in cima e macerie alla base */
    box(cx + ((h >> 3) % 9) - 4.0f, hh, cz + ((h >> 6) % 9) - 4.0f, 3.5f, 3 + (h % 4), 3.5f, h * 0.1f, RGB(112, 108, 106), RGB(96, 104, 90), 1, 0);
    if (h % 3 == 0) box(cx + 9, 0, cz - 3, 2.2f, 2, 1.8f, 0.5f, RGB(108, 104, 102), RGB(108, 104, 102), 1, 0);
    if (hh > 20 && h % 2) {
        /* finestra ad arco vuota */
        TEXS(TX_WHITE, 32);
        box(cx, hh * 0.45f, cz, 8.1f, 6, 2.0f, 0, RGB(16, 18, 26), RGB(16, 18, 26), 1, 1);
    }
}

static void draw_tufts(float x0, float z0, unsigned h)
{
    int k, b;
    for (k = 0; k < 3; k++) {
        unsigned hk = hash2(h, k + 7);
        float px = x0 + 2 + (hk % 12), pz = z0 + 2 + ((hk >> 8) % 12), gy = ground_h(px, pz);
        Lt l = light_point(px, pz, 1);
        for (b = 0; b < 3; b++) {
            float a = (hk % 628) / 100.0f + b * 2.1f, ht = 2.0f + ((hk >> (b * 3)) % 4) * 0.6f;
            float sw = sinf(frame * 0.05f + px * 0.3f) * 0.6f;
            V3 b0 = v3(px + cosf(a) * 0.6f, gy, pz + sinf(a) * 0.6f), b1 = v3(px - cosf(a) * 0.6f, gy, pz - sinf(a) * 0.6f);
            V3 tp = v3(px + cosf(a + 1.57f) * 0.8f + sw, gy + ht, pz + sinf(a + 1.57f) * 0.8f);
            uint32_t cb = litv(RGB(70, 86, 58), l, 0.55f), ct = litv(RGB(120, 140, 92), l, 1.0f);
            tri_v(b0, cb, 0, 1, b1, cb, 1, 1, tp, ct, 0.5f, 0, TX_WHITE);
        }
    }
}

static void draw_outdoor(int tx, int ty, int t, unsigned h)
{
    float x0 = (float)tx * TS, z0 = (float)ty * TS, cx = x0 + 8, cz = z0 + 8;
    float h0 = hgt[ty][tx], h1 = hgt[ty][tx + 1], h2 = hgt[ty + 1][tx + 1], h3 = hgt[ty + 1][tx];
    float s0 = hshade[ty][tx], s1 = hshade[ty][tx + 1], s2 = hshade[ty + 1][tx + 1], s3 = hshade[ty + 1][tx];
    Lt l0 = cgl(tx, ty), l1 = cgl(tx + 1, ty), l2 = cgl(tx + 1, ty + 1), l3 = cgl(tx, ty + 1);
    int tex = TX_GRASS;
    float ts = 32, ex = cx - camEye.x, ez = cz - camEye.z, dcam = sqrtf(ex * ex + ez * ez);
    uint32_t base = RGB(104 + (int)(h % 8), 112 + (int)(h % 8), 92 + (int)(h % 6));
    if (t == T_DIRT || t == T_RUIN) { tex = TX_DIRT; base = RGB(130, 118, 104); }
    if (t == T_ROCK) { tex = TX_ROCK; ts = 20; base = RGB(120, 118, 122); }
    if (t == T_WATER || t == T_PIER) { tex = TX_DIRT; base = RGB(60, 58, 56); }
    if (ty < 58 && t == T_GRASS) base = RGB(100 + (int)(h % 8), 104 + (int)(h % 8), 90);   /* erba piu' secca in collina */
    emit_quad(v3(x0, h0, z0), v3(x0 + TS, h1, z0), v3(x0 + TS, h2, z0 + TS), v3(x0, h3, z0 + TS),
              litv(base, l0, s0), litv(base, l1, s1), litv(base, l2, s2), litv(base, l3, s3),
              x0 / ts, z0 / ts, (x0 + TS) / ts, z0 / ts, (x0 + TS) / ts, (z0 + TS) / ts, x0 / ts, (z0 + TS) / ts, tex, RB_OPAQUE);
    cur_gy = ground_h(cx, cz);
    light_at(cx, cz);
    switch (t) {
    case T_GRASS:
        if (dcam < 95) draw_tufts(x0, z0, h);
        break;
    case T_ROCK: {
        float mx = h0, mn = h0;
        if (h1 > mx) mx = h1; if (h2 > mx) mx = h2; if (h3 > mx) mx = h3;
        if (h1 < mn) mn = h1; if (h2 < mn) mn = h2; if (h3 < mn) mn = h3;
        cur_gy = 0;
        TEXS(TX_ROCK, 18);
        box(cx, mn - 3, cz, 8.4f, (mx - mn) + 6 + (h % 5), 8.4f, (h % 5) * 0.08f, RGB(112, 110, 116), RGB(96, 104, 92), 1, 0);
        if (h % 3 == 0) box(cx + 3, mx + 2, cz - 2, 4, 4 + (h % 4), 3.5f, h * 0.2f, RGB(106, 104, 110), RGB(96, 104, 92), 1, 0);
        break;
    }
    case T_PINE:
        if (dcam > 26) draw_pine(cx + (h % 5) - 2.0f, cz + ((h >> 4) % 5) - 2.0f, h);
        break;
    case T_BOULDER: draw_boulder(cx, cz, h); break;
    case T_RUIN: draw_ruin(tx, ty, h); break;
    case T_TREE: case T_GRAVE: case T_LAMP: draw_prop(t, tx, ty, h); break;
    case T_WATER:
    case T_PIER: {
        /* superficie del lago con riflessi */
        Lt lw = light_point(cx, cz, 1);
        uint32_t wc = rgba(litv(RGB(40, 56, 80), lw, 1), 210);
        float sx = (float)((int)(hash2(tx, ty + frame / 24) % 10)) - 5;
        cur_gy = 0;
        quad(v3(x0, WL, z0), v3(x0 + TS, WL, z0), v3(x0 + TS, WL, z0 + TS), v3(x0, WL, z0 + TS), wc);
        if (hash2(tx * 7 + frame / 30, ty) % 6 == 0)
            ground_spot(cx + sx, cz, 3.5f, WL + 0.1f, rgba(RGB(140, 160, 210), 60), RB_ADD);
        if (t == T_PIER) {
            TEXS(TX_WOOD, 10);
            box(cx, WL + 0.2f, cz, 8, 0.7f, 8, 0, RGB(120, 92, 66), RGB(126, 98, 70), 1, 0);
            if ((tx + ty) % 2 == 0) {
                box(x0 + 1, WL - 6, z0 + 1, 0.8f, 7.5f, 0.8f, 0, RGB(90, 70, 52), RGB(90, 70, 52), 1, 0);
                box(x0 + 15, WL - 6, z0 + 15, 0.8f, 7.5f, 0.8f, 0, RGB(90, 70, 52), RGB(90, 70, 52), 1, 0);
            }
        }
        break;
    }
    case T_DIRT:
        if (h % 9 == 0) cube(x0 + 4 + (h % 8), 0, z0 + 5 + ((h >> 4) % 6), 0.7f, h * 0.1f, lit(RGB(110, 104, 100), 1), 1, 1);
        break;
    }
    cur_gy = 0;
    TEXS(TX_WHITE, 32);
}

static void draw_world(float cx, float cz)
{
    int tcx = (int)(cx / TS), tcz = (int)(cz / TS), tx, ty;
    float bx = -cosf(camYaw), bz = -sinf(camYaw);
    build_corner_grid(tcx, tcz);
    for (ty = tcz - GR; ty <= tcz + GR; ty++)
        for (tx = tcx - GR; tx <= tcx + GR; tx++) {
            int t, dx = tx - tcx, dy = ty - tcz;
            float x0 = (float)tx * TS, z0 = (float)ty * TS;
            unsigned h;
            if (dx * dx + dy * dy > GR * GR) continue;
            if (tx < 0 || ty < 0 || tx >= MW || ty >= MH) continue;
            t = map[ty][tx];
            h = hash2(tx, ty);
            if (!in_town(tx, ty)) { draw_outdoor(tx, ty, t, h); continue; }
            cur_gy = town_base(tx, ty);
            if (t != T_WALL) {
                float by = cur_gy;
                /* selciato: texture + luce sfumata ai quattro angoli */
                uint32_t base = RGB(108 + (int)(h % 10), 106 + (int)(h % 10), 116 + (int)(h % 10));
                Lt l0 = cgl(tx, ty), l1 = cgl(tx + 1, ty), l2 = cgl(tx + 1, ty + 1), l3 = cgl(tx, ty + 1);
                int side = is_wall(tx - 1, ty) || is_wall(tx + 1, ty) || is_wall(tx, ty - 1) || is_wall(tx, ty + 1);
                int arena = in_arena(x0 + 8, z0 + 8);
                if (side && !arena) {
                    /* marciapiede lastricato, rialzato col cordolo */
                    uint32_t fb2 = RGB(120, 118, 122);
                    float sy = 0.6f + by;
                    cur_tex = TX_FLAG;
                    emit_quad(v3(x0, sy, z0), v3(x0 + TS, sy, z0), v3(x0 + TS, sy, z0 + TS), v3(x0, sy, z0 + TS),
                              litv(fb2, l0, 1), litv(fb2, l1, 1), litv(fb2, l2, 1), litv(fb2, l3, 1),
                              x0 / 32, z0 / 32, (x0 + TS) / 32, z0 / 32, (x0 + TS) / 32, (z0 + TS) / 32, x0 / 32, (z0 + TS) / 32,
                              TX_FLAG, RB_OPAQUE);
                    {
                        static const int ddx[4] = { 0, 0, 1, -1 }, ddy[4] = { 1, -1, 0, 0 };
                        int k;
                        for (k = 0; k < 4; k++) {
                            int nx = tx + ddx[k], ny = ty + ddy[k], ns;
                            if (is_wall(nx, ny)) continue;
                            ns = is_wall(nx - 1, ny) || is_wall(nx + 1, ny) || is_wall(nx, ny - 1) || is_wall(nx, ny + 1);
                            if (ns) continue;
                            {   /* cordolo verso la strada */
                                float ex = x0 + 8 + ddx[k] * 8, ez = z0 + 8 + ddy[k] * 8;
                                float hx = ddx[k] ? 0.5f : 8, hz = ddy[k] ? 0.5f : 8;
                                uint32_t cc = litv(RGB(130, 128, 132), l0, 0.75f);
                                emit_quad(v3(ex - hx, by, ez - hz), v3(ex + hx, by, ez + hz), v3(ex + hx, sy, ez + hz), v3(ex - hx, sy, ez - hz),
                                          cc, cc, cc, cc, 0, 0, 1, 0, 1, 0.05f, 0, 0.05f, TX_FLAG, RB_OPAQUE);
                            }
                        }
                    }
                } else {
                    cur_tex = TX_COBBLE;
                    emit_quad(v3(x0, by, z0), v3(x0 + TS, by, z0), v3(x0 + TS, by, z0 + TS), v3(x0, by, z0 + TS),
                              litv(base, l0, 1), litv(base, l1, 1), litv(base, l2, 1), litv(base, l3, 1),
                              x0 / 32, z0 / 32, (x0 + TS) / 32, z0 / 32, (x0 + TS) / 32, (z0 + TS) / 32, x0 / 32, (z0 + TS) / 32,
                              TX_COBBLE, RB_OPAQUE);
                }
                if (h % 7 == 0 && t == T_FLOOR) {
                    /* detriti: foglie secche, carte, pietre */
                    int k;
                    for (k = 0; k < 3; k++) {
                        unsigned hk = hash2(h, k);
                        float px2 = x0 + 2 + (hk % 12), pz2 = z0 + 2 + ((hk >> 8) % 12), a = (hk % 628) / 100.0f, sz = 0.8f + (hk % 3) * 0.4f;
                        float yy = ((side && !arena) ? 0.7f : 0.1f) + by;
                        uint32_t dc = (hk % 3 == 0) ? RGB(150, 140, 120) : ((hk % 3 == 1) ? RGB(110, 76, 44) : RGB(90, 70, 40));
                        V3 q0 = v3(px2 + cosf(a) * sz, yy, pz2 + sinf(a) * sz), q1 = v3(px2 - sinf(a) * sz, yy, pz2 + cosf(a) * sz);
                        V3 q2 = v3(px2 - cosf(a) * sz, yy, pz2 - sinf(a) * sz), q3 = v3(px2 + sinf(a) * sz, yy, pz2 - cosf(a) * sz);
                        uint32_t lc = litv(dc, l0, 1);
                        emit_quad(q0, q1, q2, q3, lc, lc, lc, lc, 0, 0, 1, 0, 1, 1, 0, 1, TX_WHITE, RB_OPAQUE);
                    }
                    if (h % 3 == 0) cube(x0 + 4 + (h % 8), 0, z0 + 5 + ((h >> 4) % 6), 0.7f, h * 0.1f, litv(RGB(100, 96, 100), l1, 1), 1, 1);
                }
                if (h % 19 == 0) {
                    /* pozzanghera con riflesso della luna */
                    ground_spot(x0 + 8, z0 + 8, 9, 0.2f, rgba(RGB(10, 14, 26), 150), RB_ALPHA);
                    ground_spot(x0 + 6, z0 + 7, 4, 0.25f, rgba(RGB(120, 140, 190), 70), RB_ADD);
                }
                if (t == T_GATE && gate_closed) {
                    int a = 70 + (int)(hash2(frame / 4, tx + ty) % 50);
                    TEXS(TX_CLOTH, 16);
                    box(x0 + 8, 0, z0 + 8, 8, 40, 8, 0, rgba(RGB(210, 215, 235), a), rgba(RGB(230, 235, 250), a), 1, 1);
                    glow(x0 + 8, 18, z0 + 8, 22, rgba(RGB(180, 190, 230), 60));
                }
                if (t >= T_LAMP) draw_prop(t, tx, ty, h);
            } else {
                int edge = 0, cut = 0;
                float wx = x0 + 8 - P.x, wz = z0 + 8 - P.y, along, lat;
                if (ty > 0 && map[ty - 1][tx] != T_WALL) edge = 1;
                if (ty < MH - 1 && map[ty + 1][tx] != T_WALL) edge = 1;
                if (tx > 0 && map[ty][tx - 1] != T_WALL) edge = 1;
                if (tx < MW - 1 && map[ty][tx + 1] != T_WALL) edge = 1;
                if (!edge) continue;
                along = wx * bx + wz * bz;
                lat = fabsf(wx * bz - wz * bx);
                (void)along; (void)lat;   /* con la telecamera bassa i muri non vengono tagliati */
                draw_building(tx, ty, h, cut);
            }
            cur_gy = 0;
        }
    TEXS(TX_WHITE, 32);
}

static void draw_brazier(int i)
{
    float x = (braz[i].tx - WX0) * TS + 8.0f, z = (braz[i].ty - WY0) * TS + 8.0f;
    {
        float ex = x - camEye.x, ez = z - camEye.z;
        if (ex * ex + ez * ez > 260 * 260) return;
    }
    light_at(x, z);
    cur_gy = ground_h(x, z);
    TEXS(TX_STONE, 12);
    box(x, 0, z, 7, 2, 7, 0, RGB(100, 98, 104), RGB(110, 108, 114), 1, 0);
    box(x, 2, z, 5, 2, 5, 0.785f, RGB(90, 86, 90), RGB(100, 96, 100), 1, 0);
    TEXS(TX_METAL, 10);
    box(x, 4, z, 1.4f, 9, 1.4f, 0, RGB(90, 80, 74), RGB(90, 80, 74), 1, 0);
    box(x, 13, z, 5, 3, 5, 0.785f, RGB(120, 100, 84), RGB(50, 32, 24), 1, 0);
    TEXS(TX_WHITE, 32);
    if (braz[i].lit) {
        flame(x, 16, z, 3, i * 31);
        ground_spot(x, z, 44, 0.6f, rgba(RGB(255, 130, 40), 80), RB_ADD);
    } else cube(x, 16, z, 1.5f, 0, RGB(50, 48, 50), 1, 0);
    cur_gy = 0;
}

/* --- il pellegrino: figura umana articolata (design originale) --- */
/* trasformazione locale del corpo: f = avanti, u = alto, s = destra */

static void draw_player(void)
{
    float x = P.x, z = P.y;
    float ang = atan2f(P.fy, P.fx);
    uint32_t tunic = RGB(118, 114, 108), cape = RGB(78, 80, 84), capeD = RGB(58, 60, 64);
    uint32_t steel = RGB(118, 122, 134), steelD = RGB(86, 88, 98), bronze = RGB(176, 138, 82), mail = RGB(96, 98, 106);
    uint32_t leather = RGB(96, 70, 50), metalc = RGB(210, 214, 228), ember = RGB(200, 90, 40);
    float p = P.walk, breath = sinf(frame * 0.05f) * 0.15f, sway = sinf(frame * 0.045f + p * 0.5f);
    float legA[2], kneeB[2], armA[2], lean = 0;
    float hipU, shU = 16.6f;
    float swF = 0, swU = 0, swS = 0;     /* direzione della lama (locale) */
    float hf, hu, hs;                    /* mano destra */
    int i, attacking = P.state == PS_ATK;

    if (P.state == PS_DEAD && mode == M_DEAD) return;
    hi_detail = 1;
    if (P.iframes > 0 && P.state != PS_ROLL && (frame & 2)) return;

    /* quanto si sta muovendo (per l'ampiezza del passo) */
    {
        float d = P.walk - lastWalk, t = d / 0.3f;
        if (t > 1) t = 1;
        if (t < 0) t = 0;
        mvAmt += (t - mvAmt) * 0.25f;
        lastWalk = P.walk;
    }

    light_at(x, z);
    {
        float gh0 = ground_h(x, z);
        cur_gy = gh0; shadow(x, z, 6); cur_gy = 0;
        pb_y = gh0;
    }
    pb_x = x; pb_z = z; pb_rolling = 0;
    if (P.state == PS_ROLL) {
        float ra = atan2f(P.ry, P.rx);
        pb_c = cosf(ra); pb_s = sinf(ra);
        pb_rolling = 1;
        pb_roll = (P.timer / 18.0f) * 2 * PI;
        if (pb_roll > 2 * PI) pb_roll = 2 * PI;
        pb_y += -sinf(pb_roll * 0.5f) * 2.5f;
    } else { pb_c = cosf(ang); pb_s = sinf(ang); }

    /* ---- posa ---- */
    for (i = 0; i < 2; i++) {
        float ph = p + i * PI;
        legA[i] = sinf(ph) * 0.62f * mvAmt;
        kneeB[i] = (cosf(ph) > 0 ? cosf(ph) : 0) * 0.9f * mvAmt + 0.05f;
        armA[i] = -sinf(ph) * 0.5f * mvAmt;
    }
    hipU = 10.4f + fabsf(sinf(p)) * 0.4f * mvAmt + breath * 0.5f;
    if (attacking) {
        legA[0] = 0.45f; kneeB[0] = 0.35f; legA[1] = -0.35f; kneeB[1] = 0.25f;
        hipU = 9.8f; lean = 0.9f;
    }
    if (P.state == PS_HURT) lean = -1.0f;
    if (pb_rolling) { legA[0] = legA[1] = 1.2f; kneeB[0] = kneeB[1] = 2.0f; lean = 1.5f; }

    /* ---- gambe: anca, ginocchio, caviglia, piede (i: 0 = destra, 1 = sinistra) ---- */
    for (i = 0; i < 2; i++) {
        float side = i == 0 ? 1.15f : -1.15f;
        float a1 = legA[i], a2 = legA[i] - kneeB[i];
        float kf = sinf(a1) * 4.9f, ku = hipU - cosf(a1) * 4.9f;
        float af = kf + sinf(a2) * 4.9f, au = ku - cosf(a2) * 4.9f;
        if (!pb_rolling && au < 0.6f) { kf += 0; au = 0.6f; }
        /* cosciale, ginocchiello a punta, schiniere, scarpa a lame */
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        limb_taper = 0.82f;
        limbL(0, hipU, side, kf, ku + 0.3f, side, 1.12f, 1.2f, steel);
        limb_taper = 1.0f;
        blobL(kf + 0.25f, ku, side, 1.05f, 1.0f, 0.95f, steelD, 4, 7);
        limbL(kf + 0.4f, ku + 0.3f, side, kf + 1.35f, ku, side, 0.35f, 0.35f, bronze);
        limb_taper = 0.85f;
        limbL(kf, ku - 0.4f, side, af, au + 0.4f, side, 0.95f, 1.0f, steel);
        limb_taper = 1.0f;
        blobL(af + 0.75f, au - 0.1f, side, 1.6f, 0.62f, 0.82f, steelD, 4, 8);
        limbL(af - 0.2f, au + 0.2f, side, af + 0.6f, au + 0.5f, side, 0.85f, 0.9f, steelD);
        mat_metal = 0;
    }

    /* ---- maglia, corazza, falda a lame, sopravveste, cintura ---- */
    TEXS(TX_CLOTH, 4);
    limbL(0, hipU - 1.0f, 0, 0, hipU + 1.2f, 0, 1.95f, 1.35f, mail);
    mat_metal = 1;
    TEXS(TX_METAL, 10);
    blobL(lean * 0.7f + 0.3f, shU - 1.6f + breath, 0, 1.75f, 2.4f, 2.75f, steel, 6, 10);           /* petto */
    limbL(0, hipU + 0.6f, 0, lean * 0.6f, shU - 2.2f + breath, 0, 2.2f, 1.45f, steelD);
    limbL(lean * 0.75f + 1.45f, shU - 3.4f + breath, 0, lean * 0.8f + 1.5f, shU + 0.1f + breath, 0, 0.28f, 0.2f, bronze);   /* costola */
    for (i = 0; i < 3; i++) {
        float y0 = hipU + 0.9f - i * 1.15f;
        limb_taper = 1.08f;
        limbL(0.1f, y0, 0, 0.12f, y0 - 1.2f, 0, 2.15f + i * 0.18f, 1.5f + i * 0.1f, i == 0 ? steel : steelD);   /* falda */
        limb_taper = 1.0f;
    }
    mat_metal = 0;
    TEXS(TX_CLOTH, 6);
    {
        float sw2 = mvAmt * sinf(p) * 0.6f;
        limbL(1.25f, hipU + 0.4f, 0, 1.5f + sw2, hipU - 5.6f, 0, 1.25f, 0.18f, tunic);               /* sopravveste davanti */
        limbL(1.5f + sw2, hipU - 5.2f, 0, 1.55f + sw2, hipU - 5.9f, 0, 1.27f, 0.2f, ember);           /* orlo di brace */
        limbL(-1.3f, hipU + 0.4f, 0, -1.6f - sw2 - mvAmt, hipU - 5.6f, 0, 1.25f, 0.18f, shade(tunic, 0.85f));
    }
    TEXS(TX_WOOD, 5);
    limbL(0.05f, hipU + 0.5f, 0, 0.05f, hipU + 1.3f, 0, 2.3f, 1.6f, leather);
    mat_metal = 1;
    TEXS(TX_METAL, 6);
    limbL(1.65f, hipU + 0.55f, 0, 1.65f, hipU + 1.25f, 0, 0.55f, 0.15f, bronze);  /* fibbia */
    /* gorgiera */
    limb_taper = 0.85f;
    limbL(lean * 0.8f + 0.2f, shU - 0.2f + breath, 0, lean * 0.85f + 0.3f, shU + 1.4f + breath, 0, 1.25f, 1.25f, steelD);
    limb_taper = 1.0f;
    mat_metal = 0;

    /* ---- mantellina e cappa sulle spalle ---- */
    TEXS(TX_CLOTH, 8);
    cloth_cape(shU + breath, lean, sway, cape);
    /* spallacci a lame sovrapposte */
    mat_metal = 1;
    TEXS(TX_METAL, 8);
    for (i = 0; i < 2; i++) {
        float ss = i ? -2.75f : 2.75f, sf = lean * 0.8f + 0.1f, su = shU + breath;
        blobL(sf, su + 0.5f, ss, 1.55f, 1.15f, 1.5f, steel, 5, 8);
        blobL(sf, su - 0.35f, ss * 1.08f, 1.6f, 0.75f, 1.45f, steelD, 4, 8);
        blobL(sf, su - 1.1f, ss * 1.12f, 1.5f, 0.6f, 1.3f, steel, 4, 8);
        limbL(sf - 1.4f, su + 1.45f, ss * 0.92f, sf + 1.4f, su + 1.45f, ss * 0.92f, 0.22f, 0.22f, bronze);
    }
    mat_metal = 0;

    /* ---- braccio sinistro (lanterna/fiala) ---- */
    {
        float sf = lean * 0.8f + 0.1f, su = shU + breath, ss = -2.6f;
        float ef, eu, es, hf2, hu2, hs2;
        if (P.state == PS_HEAL) {
            float t = P.timer < 20 ? P.timer / 20.0f : (P.timer > 34 ? (44 - P.timer) / 10.0f : 1);
            ef = sf + 2.5f * t + 0.3f; eu = su - 3.5f + 1.5f * t; es = ss - 0.4f;
            hf2 = ef + 1.5f * t; hu2 = eu + 3.0f * t - (1 - t) * 3.5f; hs2 = ss + 1.8f * t;
        } else {
            ef = sf + sinf(armA[1]) * 4.2f; eu = su - cosf(armA[1]) * 4.2f; es = ss - 0.35f;
            hf2 = ef + sinf(armA[1] + 0.35f) * 3.8f; hu2 = eu - cosf(armA[1] + 0.35f) * 3.8f; hs2 = es + 0.1f;
        }
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        limb_taper = 0.88f;
        limbL(sf, su, ss, ef, eu, es, 0.85f, 0.85f, steel);
        limb_taper = 1.0f;
        blobL(ef, eu, es, 0.85f, 0.85f, 0.85f, steelD, 4, 7);
        limb_taper = 0.85f;
        limbL(ef, eu, es, hf2, hu2, hs2, 0.78f, 0.78f, steel);
        limb_taper = 1.0f;
        blobL(hf2 + 0.15f, hu2 - 0.45f, hs2, 0.68f, 0.82f, 0.55f, steelD, 4, 7);
        mat_metal = 0;
        if (P.state == PS_HEAL) {
            V3 vp = L2W(hf2 + 0.4f, hu2 + 0.2f, hs2);
            TEXS(TX_WHITE, 32);
            box(vp.x, vp.y - 0.5f, vp.z, 0.5f, 2.0f, 0.5f, ang, RGB(255, 160, 60), RGB(255, 200, 120), 1, 1);
            glow(vp.x, vp.y + 0.5f, vp.z, 5, rgba(RGB(255, 160, 60), 200));
        }
    }

    /* ---- lanterna appesa al fianco sinistro ---- */
    {
        float lsw = sinf(p) * 0.6f * mvAmt;
        V3 hook = L2W(0.6f, hipU + 0.6f, -2.1f), lp = L2W(0.6f + lsw, hipU - 2.4f, -2.3f);
        TEXS(TX_METAL, 6);
        limb(hook, lp, 0.12f, 0.12f, RGB(60, 56, 52));
        TEXS(TX_WHITE, 32);
        box(lp.x, lp.y - 2.4f, lp.z, 0.95f, 2.4f, 0.95f, ang, RGB(255, 205, 115), RGB(255, 225, 160), 1, 1);
        TEXS(TX_METAL, 6);
        box(lp.x, lp.y, lp.z, 1.2f, 0.6f, 1.2f, ang, RGB(80, 70, 60), RGB(90, 80, 70), 1, 0);
        box(lp.x, lp.y - 3.0f, lp.z, 1.2f, 0.6f, 1.2f, ang, RGB(80, 70, 60), RGB(90, 80, 70), 1, 0);
        glow(lp.x, lp.y - 1.2f, lp.z, 8, rgba(RGB(255, 180, 90), 120));
    }

    /* ---- braccio destro e sciabola ---- */
    {
        float sf = lean * 0.8f + 0.1f, su = shU + breath, ss = 2.6f;
        float ef, eu, es;
        if (attacking && P.atk == 0) {
            int W = AW[0], A = AA[0];
            float rel;
            if (P.timer <= W) rel = 1.7f * ((float)P.timer / W);
            else if (P.timer <= W + A) rel = 1.7f - 3.4f * ((float)(P.timer - W) / A);
            else rel = -1.7f;
            hf = sf + cosf(rel) * 6.8f; hu = su - 1.6f; hs = ss * 0.4f + sinf(rel) * 6.8f;
            ef = (sf + hf) * 0.5f; eu = (su + hu) * 0.5f - 1.0f; es = (ss + hs) * 0.5f + 0.6f;
            swF = cosf(rel); swU = -0.08f; swS = sinf(rel);
        } else if (attacking && P.atk == 1) {
            /* colpo pesante dall'alto */
            int W = AW[1], A = AA[1];
            float e;
            if (P.timer <= W) e = 2.3f * ((float)P.timer / W);
            else if (P.timer <= W + A) e = 2.3f - 3.0f * ((float)(P.timer - W) / A);
            else e = -0.7f;
            hf = sf + cosf(e) * 6.2f; hu = su + sinf(e) * 6.2f - 0.8f; hs = 0.9f;
            ef = (sf + hf) * 0.5f - 0.4f; eu = (su + hu) * 0.5f; es = 2.4f;
            swF = cosf(e + 0.25f); swU = sinf(e + 0.25f); swS = -0.05f;
        } else {
            float a = armA[0] * 0.6f;
            ef = sf + sinf(a) * 4.2f + 0.4f; eu = su - cosf(a) * 4.2f; es = ss + 0.3f;
            hf = ef + 3.4f; hu = eu - 1.3f; hs = es - 0.2f;
            swF = 0.88f; swU = -0.22f; swS = 0.42f;
        }
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        limb_taper = 0.88f;
        limbL(sf, su, ss, ef, eu, es, 0.85f, 0.85f, steel);
        limb_taper = 1.0f;
        blobL(ef, eu, es, 0.85f, 0.85f, 0.85f, steelD, 4, 7);
        limb_taper = 0.85f;
        limbL(ef, eu, es, hf, hu, hs, 0.78f, 0.78f, steel);
        limb_taper = 1.0f;
        mat_metal = 0;
        {
            float l = sqrtf(swF * swF + swU * swU + swS * swS);
            float bl = attacking ? (P.atk ? 17.0f : 15.5f) : 14.5f;
            float g0f, g0u, g0s;
            swF /= l; swU /= l; swS /= l;
            /* impugnatura, guardia, lama */
            TEXS(TX_WOOD, 4);
            limbL(hf - swF * 2.2f, hu - swU * 2.2f, hs - swS * 2.2f, hf + swF * 0.6f, hu + swU * 0.6f, hs + swS * 0.6f, 0.35f, 0.35f, leather);
            g0f = hf + swF * 0.8f; g0u = hu + swU * 0.8f; g0s = hs + swS * 0.8f;
            TEXS(TX_METAL, 6);
            limbL(g0f - swS * 1.3f, g0u, g0s + swF * 1.3f, g0f + swS * 1.3f, g0u, g0s - swF * 1.3f, 0.3f, 0.3f, RGB(210, 175, 100));
            limbL(g0f, g0u, g0s, g0f + swF * bl, g0u + swU * bl, g0s + swS * bl, 0.12f, 0.55f, metalc);
            if (attacking && P.timer > AW[P.atk] && P.timer <= AW[P.atk] + AA[P.atk]) {
                V3 tip = L2W(g0f + swF * bl, g0u + swU * bl, g0s + swS * bl);
                glow(tip.x, tip.y, tip.z, 6, rgba(RGB(220, 230, 255), 160));
            }
        }
        mat_metal = 1; TEXS(TX_METAL, 6);
        blobL(hf + 0.1f, hu - 0.2f, hs, 0.72f, 0.75f, 0.62f, steelD, 4, 7);
        mat_metal = 0;
    }

    /* ---- elmo chiuso: calotta arrotondata, visiera con fessura, cresta ---- */
    {
        float hfb = lean * 0.9f + 0.35f, hub = shU + 1.2f + breath;
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        blobL(hfb + 0.1f, hub + 2.2f, 0, 1.75f, 2.05f, 1.6f, steel, 7, 12);           /* calotta */
        blobL(hfb + 0.75f, hub + 1.25f, 0, 1.3f, 1.25f, 1.42f, steelD, 5, 10);         /* mentoniera */
        limbL(hfb + 1.55f, hub + 2.9f, 0, hfb + 1.62f, hub + 0.6f, 0, 0.32f, 0.3f, steel);   /* spigolo della visiera */
        limbL(hfb - 1.55f, hub + 4.0f, 0, hfb + 1.4f, hub + 4.05f, 0, 0.22f, 0.32f, bronze); /* cresta */
        blobL(hfb - 0.2f, hub + 0.4f, 0, 1.6f, 0.5f, 1.65f, bronze, 3, 10);              /* bordo inferiore */
        mat_metal = 0;
        TEXS(TX_WHITE, 32);
        /* fessura degli occhi (buia) e fori di respirazione */
        limbL(hfb + 1.45f, hub + 2.55f, -1.1f, hfb + 1.45f, hub + 2.55f, 1.1f, 0.18f, 0.22f, RGB(8, 8, 10));
        for (i = 0; i < 3; i++) {
            limbL(hfb + 1.5f, hub + 1.35f, 0.45f + i * 0.28f, hfb + 1.5f, hub + 1.65f, 0.45f + i * 0.28f, 0.06f, 0.08f, RGB(10, 10, 12));
            limbL(hfb + 1.5f, hub + 1.35f, -0.45f - i * 0.28f, hfb + 1.5f, hub + 1.65f, -0.45f - i * 0.28f, 0.06f, 0.08f, RGB(10, 10, 12));
        }
        /* lembo di stoffa dietro l'elmo */
        TEXS(TX_CLOTH, 6);
        limbL(hfb - 1.5f, hub + 2.6f, 0, hfb - 2.6f - mvAmt, hub - 1.0f, 0, 0.8f, 0.2f, capeD);
    }
    hi_detail = 0;

    /* scia del fendente e cura */
    if (attacking && P.timer > AW[P.atk] && P.timer <= AW[P.atk] + AA[P.atk] && P.atk == 0) {
        float prog = (float)(P.timer - AW[0]) / AA[0];
        cur_gy = ground_h(x, z);
        fan(x, z, 14.5f, 15, 24, ang + 1.4f - 2.8f * prog, ang + 1.4f, rgba(RGB(230, 235, 255), 90), 12);
        cur_gy = 0;
    }
    if (attacking && P.atk == 1 && P.timer == AW[1] + AA[1]) spawn_parts(x + pb_c * 16, z + pb_s * 16, 8, RGB(200, 190, 170), 1.6f);
    if (P.state == PS_HEAL) { cur_gy = ground_h(x, z); ground_spot(x, z, 18, 1, rgba(RGB(255, 150, 50), 110), RB_ADD); cur_gy = 0; }
    TEXS(TX_WHITE, 32);
}

static void hpbar3d(float x, float z, float y, float ratio, float half)
{
    float ra = camYaw + PI / 2, rx = cosf(ra), rz = sinf(ra);
    TEXS(TX_WHITE, 32);
    box(x, y, z, half, 1.4f, 0.4f, ra, RGB(20, 10, 10), RGB(20, 10, 10), 1, 1);
    if (ratio > 0)
        box(x - rx * half * (1 - ratio), y + 0.2f, z - rz * half * (1 - ratio), half * ratio, 1.6f, 0.6f, ra,
            RGB(150, 20, 24), RGB(190, 30, 34), 1, 1);
}

/* --- Lupo cinereo: quadrupede arrotondato (design originale) --- */
static void draw_wolf(Enemy *e, float gh)
{
    float ang = atan2f(e->fy, e->fx);
    int moving = e->state == ES_CHASE, i;
    float ph = frame * (moving ? 0.38f : 0.0f) + e->x * 0.1f, amp = moving ? 1.0f : 0.15f;
    float crouch = e->state == ES_WIND ? 1.6f : 0, stretch = e->state == ES_STRIKE ? 1.5f : 0;
    uint32_t fur = RGB(118, 112, 108), dark = RGB(82, 78, 76), eye = RGB(255, 110, 50);
    if (e->state == ES_WIND && (e->timer & 4)) { fur = RGB(200, 90, 80); eye = RGB(255, 230, 120); }
    if (e->state == ES_STAG) fur = shade(fur, 0.7f);
    if (e->flash > 0) fur = dark = RGB(255, 255, 255);
    pb_x = e->x; pb_z = e->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh - crouch;
    TEXS(TX_BARK, 5);
    /* zampe: anteriori e posteriori, andatura diagonale */
    for (i = 0; i < 4; i++) {
        float lf = i < 2 ? 3.2f + stretch : -3.6f, ls = (i & 1) ? 1.5f : -1.5f;
        float a = sinf(ph + ((i == 0 || i == 3) ? 0 : PI)) * 0.6f * amp;
        float kf = lf + sinf(a) * 2.9f, ku = 6.0f - cosf(a) * 2.9f + crouch * 0.4f;
        float ff = kf + sinf(a * 0.4f - (i < 2 ? 0.0f : 0.5f)) * 3.0f, fu = 0.4f + crouch;
        limb_taper = 0.8f;
        limbL(lf, 6.4f, ls, kf, ku, ls, 0.75f, 0.85f, dark);
        limbL(kf, ku, ls, ff, fu, ls, 0.5f, 0.55f, dark);
        limb_taper = 1.0f;
        blobL(ff + 0.5f, fu, ls, 0.8f, 0.4f, 0.55f, dark, 3, 6);
    }
    /* corpo, petto e criniera */
    blobL(-0.5f + stretch * 0.5f, 7.0f, 0, 5.6f + stretch, 2.5f, 2.3f, fur, 5, 10);
    blobL(3.0f + stretch, 7.4f, 0, 2.9f, 2.9f, 2.5f, fur, 5, 9);
    blobL(2.2f + stretch, 9.2f, 0, 2.8f, 1.6f, 2.1f, dark, 4, 8);
    /* collo, testa, muso, orecchie */
    limbL(4.0f + stretch, 8.4f, 0, 6.0f + stretch, 9.0f - crouch * 0.4f, 0, 1.4f, 1.5f, fur);
    blobL(6.4f + stretch, 9.0f - crouch * 0.5f, 0, 2.2f, 1.7f, 1.7f, fur, 5, 8);
    blobL(8.4f + stretch, 8.3f - crouch * 0.5f, 0, 1.6f, 0.85f, 0.9f, shade(fur, 0.9f), 4, 7);
    blobL(9.7f + stretch, 8.4f - crouch * 0.5f, 0, 0.35f, 0.35f, 0.4f, RGB(30, 26, 26), 3, 5);
    if (e->state == ES_STRIKE) blobL(8.6f + stretch, 7.4f, 0, 1.3f, 0.35f, 0.75f, RGB(120, 40, 40), 3, 6);   /* fauci */
    limbL(5.7f + stretch, 10.2f - crouch * 0.5f, 0.9f, 5.3f + stretch, 12.1f - crouch * 0.5f, 1.1f, 0.4f, 0.25f, dark);
    limbL(5.7f + stretch, 10.2f - crouch * 0.5f, -0.9f, 5.3f + stretch, 12.1f - crouch * 0.5f, -1.1f, 0.4f, 0.25f, dark);
    TEXS(TX_WHITE, 32);
    {
        V3 e1 = L2W(7.7f + stretch, 9.6f - crouch * 0.5f, 0.75f), e2 = L2W(7.7f + stretch, 9.6f - crouch * 0.5f, -0.75f);
        box(e1.x, e1.y - 0.2f, e1.z, 0.25f, 0.4f, 0.25f, ang, eye, eye, 1, 1);
        box(e2.x, e2.y - 0.2f, e2.z, 0.25f, 0.4f, 0.25f, ang, eye, eye, 1, 1);
        glow((e1.x + e2.x) * 0.5f, e1.y, (e1.z + e2.z) * 0.5f, 3.5f, rgba(eye, 140));
    }
    /* coda */
    TEXS(TX_BARK, 5);
    limb_taper = 0.4f;
    limbL(-5.6f, 7.6f, 0, -9.0f, 6.0f + sinf(ph * 0.5f) * 0.8f, sinf(ph) * 1.4f, 0.8f, 0.8f, fur);
    limb_taper = 1.0f;
    TEXS(TX_WHITE, 32);
}

/* --- Colosso di pietra: bruto enorme con una mazza di roccia (design originale) --- */
static void draw_colossus(Enemy *e, float gh)
{
    float ang = atan2f(e->fy, e->fx);
    int moving = e->state == ES_CHASE, i;
    float ph = frame * (moving ? 0.12f : 0.0f), br = sinf(frame * 0.05f) * 0.4f;
    uint32_t skin = RGB(128, 122, 116), dark = RGB(92, 88, 86), ember = RGB(255, 140, 60);
    float ce;   /* angolo della mazza */
    if (e->state == ES_WIND) ce = 2.4f * (1.0f - (float)e->timer / EK[3].wind);
    else if (e->state == ES_STRIKE) ce = 2.4f - 3.2f * (1.0f - e->timer / 9.0f);
    else if (e->state == ES_REC) ce = -0.8f;
    else ce = -0.3f;
    if (e->state == ES_WIND && (e->timer & 4)) skin = RGB(200, 110, 90);
    if (e->flash > 0) skin = dark = RGB(255, 255, 255);
    pb_x = e->x; pb_z = e->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh;
    TEXS(TX_ROCK, 10);
    /* gambe tozze */
    for (i = 0; i < 2; i++) {
        float side = i ? -3.4f : 3.4f, a = sinf(ph + i * PI) * 0.35f;
        float kf = sinf(a) * 7, ku = 14 - cosf(a) * 7;
        limb_taper = 0.85f;
        limbL(0, 15, side, kf, ku, side, 2.6f, 2.8f, dark);
        blobL(kf, ku, side, 2.2f, 2.2f, 2.2f, dark, 4, 7);
        limbL(kf, ku, side, kf + 0.5f, 1.5f, side, 2.1f, 2.3f, dark);
        limb_taper = 1.0f;
        blobL(kf + 1.8f, 1.0f, side, 3.2f, 1.4f, 2.0f, dark, 4, 7);
    }
    /* busto ingobbito, gobba e testa piccola */
    blobL(0.5f, 17.5f + br, 0, 4.2f, 4.0f, 5.6f, skin, 5, 10);
    blobL(2.2f, 24 + br, 0, 4.8f, 5.0f, 7.4f, skin, 6, 10);
    blobL(-1.5f, 28 + br, 0, 4.0f, 3.2f, 5.6f, dark, 5, 9);
    blobL(6.2f, 27.5f + br, 0, 2.3f, 2.6f, 2.3f, skin, 5, 8);
    TEXS(TX_WHITE, 32);
    {
        V3 e1 = L2W(8.3f, 28.0f + br, 0.8f), e2 = L2W(8.3f, 28.0f + br, -0.8f);
        box(e1.x, e1.y - 0.3f, e1.z, 0.3f, 0.6f, 0.35f, ang, ember, ember, 1, 1);
        box(e2.x, e2.y - 0.3f, e2.z, 0.3f, 0.6f, 0.35f, ang, ember, ember, 1, 1);
        glow((e1.x + e2.x) * 0.5f, e1.y, (e1.z + e2.z) * 0.5f, 5, rgba(ember, 150));
        e1 = L2W(6.6f, 22.0f + br, 2.5f);
        box(e1.x, e1.y, e1.z, 0.3f, 4, 0.5f, ang, ember, ember, 1, 1);      /* crepa incandescente */
    }
    TEXS(TX_ROCK, 10);
    /* braccio sinistro lungo, nocche a terra */
    blobL(2.5f, 26 + br, -7.4f, 2.8f, 2.8f, 2.8f, skin, 4, 8);
    limb_taper = 0.85f;
    limbL(2.5f, 26 + br, -7.4f, 5.5f, 15, -8.6f, 2.0f, 2.0f, skin);
    limbL(5.5f, 15, -8.6f, 7.5f, 5, -8.4f, 1.8f, 1.8f, skin);
    limb_taper = 1.0f;
    blobL(7.8f, 4.0f, -8.4f, 2.2f, 2.0f, 2.2f, dark, 4, 7);
    /* braccio destro e mazza */
    {
        float hf = 3 + cosf(ce) * 9, hu = 24 + sinf(ce) * 9, hs = 6.5f;
        blobL(2.5f, 26 + br, 7.4f, 2.8f, 2.8f, 2.8f, skin, 4, 8);
        limbL(2.5f, 26 + br, 7.4f, (2.5f + hf) * 0.5f, (26 + hu) * 0.5f - 1.5f, 8.2f, 2.0f, 2.0f, skin);
        limbL((2.5f + hf) * 0.5f, (26 + hu) * 0.5f - 1.5f, 8.2f, hf, hu, hs, 1.8f, 1.8f, skin);
        blobL(hf, hu, hs, 2.0f, 2.0f, 2.0f, dark, 4, 7);
        limb_taper = 1.7f;
        limbL(hf, hu, hs, hf + cosf(ce + 0.2f) * 18, hu + sinf(ce + 0.2f) * 18, hs - 0.5f, 1.3f, 1.3f, RGB(104, 100, 104));
        limb_taper = 1.0f;
        blobL(hf + cosf(ce + 0.2f) * 18, hu + sinf(ce + 0.2f) * 18, hs - 0.5f, 3.0f, 3.0f, 3.0f, RGB(104, 100, 104), 4, 7);
        if (e->state == ES_STRIKE && e->timer == 7) { shake = 10; spawn_parts(e->x + pb_c * 26, e->y + pb_s * 26, 14, RGB(170, 160, 150), 2.2f); }
    }
    TEXS(TX_WHITE, 32);
}

static void draw_enemy(Enemy *e)
{
    const EKind *k = &EK[e->kind];
    float x = e->x, z = e->y;
    float ang = atan2f(e->fy, e->fx), c = cosf(ang), s = sinf(ang), sx = -s, sz = c;
    float dxp = x - P.x, dzp = z - P.y, br = sinf(frame * 0.12f + x) * 0.5f;
    uint32_t body, dark, eye = RGB(255, 80, 40);
    float gh;
    if (dxp * dxp + dzp * dzp > 220 * 220) return;
    light_at(x, z);
    gh = ground_h(x, z);
    cur_gy = gh;
    if (e->kind >= 2) {
        if (e->state == ES_DEAD) {
            float t = e->deadT / 40.0f;
            TEXS(TX_ROCK, 8);
            box(x, 0, z, k->r * (1.6f - t * 0.6f), 3 * t + 1, k->r * 0.8f, ang, RGB(90, 86, 84), RGB(90, 86, 84), 1, 0);
            TEXS(TX_WHITE, 32); cur_gy = 0;
            return;
        }
        shadow(x, z, k->r + 2);
        cur_gy = 0;
        if (e->kind == 2) draw_wolf(e, gh); else draw_colossus(e, gh);
        cur_gy = gh;
        if (e->hp < k->hp) hpbar3d(x, z, e->kind == 2 ? 15.0f : 36.0f, (float)e->hp / k->hp, e->kind == 3 ? 12 : 7);
        cur_gy = 0;
        return;
    }
    body = e->kind ? RGB(130, 138, 154) : RGB(120, 102, 88);
    if (e->state == ES_DEAD) {
        float t = e->deadT / 40.0f;
        TEXS(TX_CLOTH, 8);
        box(x, 0, z, k->r * (1.6f - t * 0.6f), 3 * t + 1, k->r * 0.8f, ang, shade(body, 0.6f), shade(body, 0.6f), 1, 0);
        TEXS(TX_WHITE, 32);
        cur_gy = 0;
        return;
    }
    if (e->state == ES_WIND && (e->timer & 4)) { body = RGB(220, 70, 60); eye = RGB(255, 230, 120); }
    if (e->state == ES_STAG) body = shade(body, 0.7f);
    if (e->flash > 0) body = RGB(255, 255, 255);
    dark = shade(body, 0.62f);
    shadow(x, z, k->r + 1.5f);
    if (e->kind == 0) {
        float a2 = ang + (e->state == ES_STRIKE ? 0.7f : (e->state == ES_WIND ? -1.4f : 0.35f));
        TEXS(TX_BARK, 8);   /* pelle ruvida */
        box(x - c * 1.5f + sx * 2.4f, 0, z - s * 1.5f + sz * 2.4f, 1.6f, 6, 1.4f, ang, dark, dark, 1, 0);
        box(x - c * 1.5f - sx * 2.4f, 0, z - s * 1.5f - sz * 2.4f, 1.6f, 6, 1.4f, ang, dark, dark, 1, 0);
        box(x - c * 0.5f, 5, z - s * 0.5f, 3.4f, 6 + br, 4.4f, ang, body, body, 1, 0);
        box(x - c * 1.6f, 10 + br, z - s * 1.6f, 3.0f, 3, 4.8f, ang, dark, dark, 1, 0);
        box(x - c * 2.2f, 13 + br, z - s * 2.2f, 0.6f, 2.5f, 0.6f, ang, RGB(190, 180, 170), RGB(210, 200, 190), 1, 0);
        box(x + c * 4.2f, 8.5f + br, z + s * 4.2f, 2.4f, 3.8f, 2.4f, ang, dark, dark, 1, 0);
        box(x + c * 5.6f, 7.6f + br, z + s * 5.6f, 1.6f, 1.6f, 1.6f, ang, shade(dark, 0.8f), shade(dark, 0.8f), 1, 0);
        cube(x + c * 6.6f + sx * 1, 10.6f + br, z + s * 6.6f + sz * 1, 0.4f, 0, eye, 1, 1);
        cube(x + c * 6.6f - sx * 1, 10.6f + br, z + s * 6.6f - sz * 1, 0.4f, 0, eye, 1, 1);
        glow(x + c * 6.8f, 11.2f + br, z + s * 6.8f, 3.5f, rgba(eye, 140));
        box(x + c * 2 - sx * 5, 2, z + s * 2 - sz * 5, 1.2f, 8, 1.2f, ang, dark, dark, 1, 0);
        box(x + sx * 5 + cosf(a2) * 6, 6, z + sz * 5 + sinf(a2) * 6, 6, 1.8f, 1.3f, a2, dark, body, 1, 0);
        TEXS(TX_METAL, 6);
        box(x + sx * 5 + cosf(a2) * 12.5f, 6, z + sz * 5 + sinf(a2) * 12.5f, 1.4f, 1.2f, 1.8f, a2 + 0.3f, RGB(200, 190, 170), RGB(220, 210, 190), 1, 0);
        TEXS(TX_CLOTH, 6);
        box(x - c * 3.6f, 3, z - s * 3.6f, 0.6f, 4, 3.5f, ang, RGB(96, 80, 70), RGB(96, 80, 70), 1, 0);
    } else {
        float sl = e->state == ES_STRIKE ? 14.0f : (e->state == ES_WIND ? 1.0f : 8.0f);
        uint32_t cloth = e->flash > 0 ? body : RGB(130, 50, 54);
        TEXS(TX_WOOD, 6);
        box(x + sx * 2.4f, 0, z + sz * 2.4f, 1.6f, 7, 1.4f, ang, RGB(70, 62, 60), RGB(70, 62, 60), 1, 0);
        box(x - sx * 2.4f, 0, z - sz * 2.4f, 1.6f, 7, 1.4f, ang, RGB(70, 62, 60), RGB(70, 62, 60), 1, 0);
        TEXS(TX_CLOTH, 8);
        box(x - c * 0.6f, 4, z - s * 0.6f, 3.2f, 7, 4.8f, ang, shade(cloth, 0.8f), cloth, 1, 0);
        TEXS(TX_METAL, 10);
        box(x, 10, z, 2.8f, 7, 4.4f, ang, body, body, 1, 0);
        box(x, 15.5f, z, 3.0f, 1.8f, 5.6f, ang, shade(body, 1.15f), shade(body, 1.15f), 1, 0);
        box(x, 17, z, 2.5f, 5, 2.5f, ang, RGB(160, 164, 174), RGB(190, 194, 204), 1, 0);
        box(x, 22, z, 0.6f, 4, 0.6f, ang, RGB(160, 164, 174), RGB(190, 194, 204), 1, 0);
        TEXS(TX_WHITE, 32);
        box(x + c * 2.6f, 19, z + s * 2.6f, 0.3f, 0.8f, 1.8f, ang, eye, eye, 1, 1);
        glow(x + c * 2.9f, 19.4f, z + s * 2.9f, 3, rgba(eye, 120));
        TEXS(TX_WOOD, 10);
        box(x + c * sl + sx * 4, 11, z + s * sl + sz * 4, 15, 0.9f, 0.7f, ang, RGB(140, 110, 80), RGB(160, 130, 96), 1, 0);
        TEXS(TX_METAL, 6);
        box(x + c * (sl + 16) + sx * 4, 10.8f, z + s * (sl + 16) + sz * 4, 2.8f, 1.3f, 1.2f, ang, RGB(220, 222, 234), RGB(245, 246, 255), 1, 0);
        TEXS(TX_CLOTH, 6);
        box(x + c * (sl + 12.5f) + sx * 4, 11.2f, z + s * (sl + 12.5f) + sz * 4, 0.4f, 0.6f, 1.6f, ang, RGB(180, 40, 40), RGB(200, 50, 50), 1, 0);
    }
    TEXS(TX_WHITE, 32);
    if (e->hp < k->hp) hpbar3d(x, z, e->kind ? 29.0f : 20.0f, (float)e->hp / k->hp, 8);
    cur_gy = 0;
}

/* --- il Custode Cinereo --- */
static void draw_boss(void)
{
    float x = Bs.x, z = Bs.y, y = Bs.z;
    float ang = atan2f(Bs.fy, Bs.fx), c = cosf(ang), s = sinf(ang), sx = -s, sz = c;
    float br = sinf(frame * 0.08f) * 0.8f;
    uint32_t body = RGB(130, 120, 112), dark, ember = Bs.phase ? RGB(255, 90, 30) : RGB(255, 165, 60);
    int i;
    if (Bs.state == BS_OFF && Bs.dead) return;
    light_at(x, z);
    if (Bs.state == BS_DEAD) {
        if (Bs.timer > 0) {
            float t = Bs.timer / 150.0f;
            TEXS(TX_WHITE, 32);
            box(x, 0, z, 14 * t, 30 * t, 14 * t, frame * 0.1f, ember, ember, 1, 1);
            glow(x, 15 * t, z, 40 * t, rgba(ember, 160));
        }
        return;
    }
    if (Bs.state == BS_WIND && Bs.atk == BA_SLAM) {
        int W = boss_wind_time(BA_SLAM);
        float rr = 52.0f * (W - Bs.timer) / W;
        fan(Bs.tx, Bs.ty, 0.7f, 49, 52, 0, 2 * PI, rgba(RGB(220, 40, 30), 200), 24);
        ground_spot(Bs.tx, Bs.ty, rr * 1.2f, 0.6f, rgba(RGB(220, 40, 20), 140), RB_ADD);
    }
    if (Bs.state == BS_WIND && Bs.atk == BA_CHARGE && (Bs.timer & 4)) {
        V3 a = v3(x + sx * 9, 0.7f, z + sz * 9), b = v3(x - sx * 9, 0.7f, z - sz * 9);
        V3 cc = v3(b.x + c * 130, 0.7f, b.z + s * 130), d = v3(a.x + c * 130, 0.7f, a.z + s * 130);
        quad(a, b, cc, d, rgba(RGB(220, 50, 30), 90));
    }
    if (Bs.state == BS_ACT && Bs.atk == BA_SLAM) {
        fan(Bs.tx, Bs.ty, 1, 40 - Bs.timer * 3, 52 - Bs.timer * 2, 0, 2 * PI, rgba(RGB(255, 170, 70), 160), 24);
        ground_spot(Bs.tx, Bs.ty, 70, 1, rgba(RGB(255, 120, 40), Bs.timer * 25), RB_ADD);
    }
    shadow(x, z, 21 - y / 4);
    if (Bs.state == BS_WIND && Bs.atk == BA_SWIPE && (Bs.timer & 4)) body = RGB(200, 90, 70);
    if (Bs.flash > 0) body = RGB(255, 240, 220);
    dark = shade(body, 0.6f);
    TEXS(TX_STONE, 14);   /* corpo di cenere pietrificata */
    box(x + sx * 7, y, z + sz * 7, 4, 14, 3.5f, ang, dark, dark, 1, 0);
    box(x - sx * 7, y, z - sz * 7, 4, 14, 3.5f, ang, dark, dark, 1, 0);
    TEXS(TX_CLOTH, 10);
    for (i = -2; i <= 2; i++) {
        float sway = sinf(frame * 0.06f + i) * 1.2f;
        box(x - c * (9 + sway) + sx * i * 5, y + 3 + (i & 1) * 2, z - s * (9 + sway) + sz * i * 5,
            1.2f, 26 - (i & 1) * 4, 2.6f, ang, RGB(80, 74, 74), RGB(96, 90, 90), 1, 0);
    }
    TEXS(TX_STONE, 14);
    box(x, y + 12, z, 8, 18 + br, 13, ang, body, body, 1, 0);
    TEXS(TX_WHITE, 32);
    box(x + c * 8.2f, y + 17, z + s * 8.2f, 0.5f, 8, 1.2f, ang, ember, ember, 1, 1);
    box(x + c * 8.2f + sx * 5, y + 22, z + s * 8.2f + sz * 5, 0.5f, 1.2f, 4, ang, ember, ember, 1, 1);
    box(x + c * 8.2f - sx * 6, y + 15, z + s * 8.2f - sz * 6, 0.5f, 5, 0.9f, ang, ember, ember, 1, 1);
    glow(x + c * 9, y + 20, z + s * 9, 14, rgba(ember, 110));
    TEXS(TX_METAL, 12);
    box(x - c * 2.5f, y + 30 + br, z - s * 2.5f, 9, 4, 15.5f, ang, RGB(96, 90, 88), RGB(110, 104, 100), 1, 0);
    for (i = 0; i < 5; i++)
        box(x + c * 8.6f + sx * (-10 + i * 4.5f), y + 11 - (i == 2 ? 2 : (i & 1)), z + s * 8.6f + sz * (-10 + i * 4.5f), 0.9f, 1.8f, 0.9f, ang + i, RGB(140, 138, 144), RGB(160, 158, 164), 1, 0);
    TEXS(TX_STONE, 10);
    box(x + c * 4, y + 30 + br, z + s * 4, 5.5f, 8, 5.5f, ang, dark, dark, 1, 0);
    TEXS(TX_WHITE, 32);
    box(x + c * 9.7f + sx * 2.5f, y + 34 + br, z + s * 9.7f + sz * 2.5f, 0.4f, 1.5f, 1, ang, ember, ember, 1, 1);
    box(x + c * 9.7f - sx * 2.5f, y + 34 + br, z + s * 9.7f - sz * 2.5f, 0.4f, 1.5f, 1, ang, ember, ember, 1, 1);
    glow(x + c * 10, y + 35 + br, z + s * 10, 7, rgba(ember, 170));
    TEXS(TX_BARK, 8);   /* corna */
    box(x + c * 3 + sx * 7, y + 36 + br, z + s * 3 + sz * 7, 1.3f, 9, 1.3f, ang, RGB(220, 210, 190), RGB(240, 230, 210), 1, 0);
    box(x + c * 3 - sx * 7, y + 36 + br, z + s * 3 - sz * 7, 1.3f, 9, 1.3f, ang, RGB(220, 210, 190), RGB(240, 230, 210), 1, 0);
    box(x + c * 1 + sx * 9, y + 44 + br, z + s * 1 + sz * 9, 1, 6, 1, ang, RGB(220, 210, 190), RGB(240, 230, 210), 1, 0);
    box(x + c * 1 - sx * 9, y + 44 + br, z + s * 1 - sz * 9, 1, 6, 1, ang, RGB(220, 210, 190), RGB(240, 230, 210), 1, 0);
    {
        float a2 = ang + 0.9f, shx = x + sx * 15, shz = z + sz * 15;
        if (Bs.state == BS_WIND && Bs.atk == BA_SWIPE) a2 = ang - 1.8f;
        if (Bs.state == BS_ACT && Bs.atk == BA_SWIPE) a2 = ang - 1.8f + 3.6f * (12 - Bs.timer) / 12.0f;
        TEXS(TX_STONE, 14);
        box(x + sx * 15, y + 14, z + sz * 15, 3.5f, 14, 3.5f, ang, body, body, 1, 0);
        box(shx + cosf(a2) * 12, y + 17, shz + sinf(a2) * 12, 12, 3, 2, a2, dark, dark, 1, 0);
        box(x - sx * 15 + c * 2, y + 6, z - sz * 15 + s * 2, 3, 20, 3, ang, body, body, 1, 0);
        TEXS(TX_WHITE, 32);
        box(shx + cosf(a2) * 33, y + 18, shz + sinf(a2) * 33, 12, 1.4f, 2.4f, a2, ember, RGB(255, 230, 160), 1, 1);
        glow(shx + cosf(a2) * 33, y + 19, shz + sinf(a2) * 33, 16, rgba(ember, 100));
        glow(shx + cosf(a2) * 44, y + 19, shz + sinf(a2) * 44, 9, rgba(ember, 120));
        if (Bs.state == BS_ACT && Bs.atk == BA_SWIPE)
            fan(x, z, y + 18, 34, 52, ang - 1.8f, ang - 1.8f + 3.6f * (12 - Bs.timer) / 12.0f, rgba(RGB(255, 140, 60), 130), 14);
    }
    if (Bs.phase) {
        ground_spot(x, z, 50 + (frame % 8), 0.8f, rgba(RGB(255, 90, 30), 90), RB_ADD);
        if (frame % 3 == 0) spawn_parts(x + frnd() * 20 - 10, z + frnd() * 20 - 10, 1, RGB(255, 120, 40), 0.5f);
    }
    TEXS(TX_WHITE, 32);
}

static void draw_atmosphere(float cx, float cz)
{
    int i;
    for (i = 0; i < 10; i++) {
        float px = cx + (float)((int)(hash2(i, 11) % 360) - 180) + sinf(frame * 0.004f + i) * 40;
        float pz = cz + (float)((int)(hash2(i, 17) % 300) - 150) + cosf(frame * 0.003f + i * 2) * 30;
        cur_gy = ground_h(px, pz);
        ground_spot(px, pz, 50 + (i % 4) * 10, 3.0f + (i % 3), rgba(RGB(150, 160, 190), 45), RB_ALPHA);
    }
    for (i = 0; i < 36; i++) {
        float px = cx + (float)((int)(hash2(i, 5) % 300) - 150) + sinf((frame + i * 37) * 0.02f) * 8;
        float pz = cz + (float)((int)(hash2(i, 9) % 260) - 130);
        float py = 70 - fmodf(frame * 0.22f + (float)(hash2(i, 2) % 70), 70);
        cur_gy = ground_h(px, pz);
        glow(px, py, pz, 1.1f, rgba(RGB(200, 196, 190), 120));
    }
}

static void draw_blood(void)
{
    int i;
    for (i = 0; i < MAXBLOOD; i++) {
        Blood *b = &blood[i];
        cur_gy = 0;
        int a;
        if (b->life <= 0) continue;
        a = b->life < 200 ? b->life * 220 / 200 : 220;
        cur_gy = ground_h(b->x, b->y);
        ground_spot(b->x, b->y, b->r * 1.6f, 0.3f, rgba(RGB(70, 4, 8), a), RB_ALPHA);
        ground_spot(b->x + b->r * 0.7f, b->y - b->r * 0.4f, b->r * 0.7f, 0.32f, rgba(RGB(60, 2, 6), a), RB_ALPHA);
    }
}

/* ------------------------------------------------------------------ */
/* HUD 2D                                                              */
/* ------------------------------------------------------------------ */
#define GOLD RGB(200, 172, 110)
#define GOLDD RGB(120, 100, 64)

static void rectf(int x, int y, int w, int h, uint32_t c) { r_rect(x, y, w, h, c); }
static void shade_screen(int a) { r_rect(0, 0, SW, SH, rgba(RGB(0, 0, 0), a)); }

/* font 5x7 */
static const char GL_CH[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:./-!+',?";
static const unsigned char GL[][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,17,10,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},
    {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
    {0,12,12,0,12,12,0},{0,0,0,0,0,12,12},{1,1,2,4,8,16,16},{0,0,0,31,0,0,0},{4,4,4,4,4,0,4},{0,4,4,31,4,4,0},
    {4,4,8,0,0,0,0},{0,0,0,0,12,4,8},{14,17,1,2,4,0,4}
};

static int fscale(int s) { return s <= 2 ? 1 : s - 1; }

static void text_raw(int x, int y, int s, uint32_t c, const char *str)
{
    for (; *str; str++, x += 6 * s) {
        const char *p = strchr(GL_CH, *str);
        int row, col;
        if (*str == ' ' || !p) continue;
        for (row = 0; row < 7; row++) {
            int bits = GL[p - GL_CH][row];
            for (col = 0; col < 5; col++)
                if (bits & (16 >> col)) {
                    int run = 1;
                    while (col + run < 5 && (bits & (16 >> (col + run)))) run++;
                    rectf(x + col * s, y + row * s, run * s, s, c);
                    col += run - 1;
                }
        }
    }
}

static void text(int x, int y, int s, uint32_t c, const char *str)
{
    int ns = fscale(s);
    text_raw(x + 1, y + 1, ns, rgba(RGB(0, 0, 0), 180), str);
    text_raw(x, y, ns, c, str);
}

static int text_w(const char *s, int sc) { int ns = fscale(sc); return (int)strlen(s) * 6 * ns - ns; }

static void text_c(int y, int s, uint32_t c, const char *str) { text((SW - text_w(str, s)) / 2, y, s, c, str); }

static void itoa_s(int v, char *out)
{
    char tmp[12]; int n = 0, i = 0;
    if (v <= 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n > 0) out[i++] = tmp[--n];
    out[i] = 0;
}

static void diamond(int cx, int cy, int r, uint32_t line, uint32_t fill)
{
    int y;
    for (y = -r; y <= r; y++) {
        int w = r - (y < 0 ? -y : y);
        if (fill && w > 1) rectf(cx - w + 1, cy + y, 2 * w - 1, 1, fill);
        rectf(cx - w, cy + y, 1, 1, line);
        rectf(cx + w, cy + y, 1, 1, line);
    }
}

static void fancy_bar(int x, int y, int w, int h, float v, float ghost, float max, uint32_t col, uint32_t colHi)
{
    int fw = (int)(w * v / max), gw = (int)(w * ghost / max);
    rectf(x, y, w, h, rgba(RGB(10, 8, 10), 170));
    if (gw > fw) rectf(x, y, gw, h, rgba(RGB(230, 220, 190), 170));
    rectf(x, y, fw, h, col);
    rectf(x, y, fw, 1, colHi);
    rectf(x - 1, y - 1, w + 2, 1, rgba(GOLD, 200));
    rectf(x - 1, y + h, w + 2, 1, rgba(GOLDD, 200));
    rectf(x + w + 1, y - 2, 1, h + 4, GOLD);
    rectf(x + w + 2, y + h / 2, 4, 1, GOLD);
}

static void vignette(void)
{
    int k;
    for (k = 0; k < 10; k++) {
        int a = (10 - k) * 9;
        rectf(0, k * 3, SW, 3, rgba(0, a));
        rectf(0, SH - (k + 1) * 3, SW, 3, rgba(0, a));
        rectf(k * 5, 0, 5, SH, rgba(0, a));
        rectf(SW - (k + 1) * 5, 0, 5, SH, rgba(0, a));
    }
}

static void draw_hud(void)
{
    char b[48], n[12];
    int i, w;
    /* emblema */
    diamond(18, 18, 12, GOLD, rgba(RGB(20, 16, 18), 200));
    diamond(18, 18, 8, GOLDD, 0);
    rectf(18, 11, 1, 15, GOLD); rectf(11, 18, 15, 1, GOLD);
    diamond(18, 18, 2, RGB(230, 60, 50), RGB(200, 40, 40));
    /* vita e stamina */
    fancy_bar(34, 12, maxhp() + 30, 4, P.hp, P.hpGhost, (float)maxhp(), RGB(176, 30, 36), RGB(230, 80, 80));
    fancy_bar(34, 20, maxst() + 20, 2, P.st, 0, (float)maxst(), RGB(206, 170, 86), RGB(250, 220, 140));
    /* fiale */
    rectf(12, SH - 46, 30, 34, rgba(RGB(14, 12, 14), 170));
    rectf(12, SH - 46, 30, 1, GOLD); rectf(12, SH - 13, 30, 1, GOLDD);
    rectf(12, SH - 46, 1, 34, GOLDD); rectf(41, SH - 46, 1, 34, GOLDD);
    rectf(24, SH - 40, 6, 3, RGB(140, 110, 80));
    rectf(25, SH - 37, 4, 3, RGB(200, 200, 210));
    rectf(21, SH - 34, 12, 14, P.flasks > 0 ? RGB(230, 120, 40) : RGB(60, 50, 50));
    rectf(22, SH - 33, 3, 8, P.flasks > 0 ? RGB(255, 200, 120) : RGB(80, 70, 70));
    itoa_s(P.flasks, n);
    text(36, SH - 22, 2, RGB(240, 230, 210), n);
    text(46, SH - 22, 2, RGB(170, 160, 140), "FIALE DI BRACE");
    /* frammenti */
    strcpy(b, ""); itoa_s(P.frags, n); strcat(b, n);
    w = text_w(b, 2);
    text(SW - 16 - w, 10, 2, RGB(235, 230, 220), b);
    diamond(SW - 28 - w, 13, 5, GOLD, rgba(RGB(30, 26, 20), 220));
    rectf(SW - 36 - w, 21, w + 26, 1, rgba(GOLD, 180));
    if (mode == M_PLAY) {
        int nb = near_brazier();
        if (nb >= 0 && !gate_closed) {
            const char *pr = braz[nb].lit ? "SELECT  RIPOSA AL BRACIERE" : "SELECT  ACCENDI IL BRACIERE";
            int tw = text_w(pr, 2);
            rectf((SW - tw) / 2 - 12, SH - 62, tw + 24, 17, rgba(RGB(8, 6, 8), 170));
            rectf((SW - tw) / 2 - 12, SH - 62, tw + 24, 1, rgba(GOLD, 160));
            text_c(SH - 57, 2, RGB(240, 220, 180), pr);
        }
    }
    if (rbActive >= 0 && RB[rbActive].state != RS_IDLE && RB[rbActive].state != RS_DEAD) {
        text_c(SH - 40, 2, RGB(235, 225, 205), RBD[rbActive].name);
        fancy_bar(90, SH - 28, 300, 4, (float)RB[rbActive].hp, 0, (float)RBD[rbActive].hp, RGB(160, 28, 26), RGB(220, 80, 60));
        diamond(84, SH - 26, 4, GOLD, rgba(RGB(30, 20, 20), 220));
    }
    /* Braci Primeve raccolte */
    for (i = 0; i < 4; i++) diamond(52 + i * 11, 38, 4, GOLD, i < embers ? RGB(255, 140, 50) : rgba(RGB(30, 24, 20), 220));
    if (mode == M_PLAY) {
        float gx = (CX0 + 66 + COREX - WX0) * TS + 8.0f - P.x, gy = (CY0 + 36 + COREY - WY0) * TS + 8.0f - P.y;
        const char *pr = 0;
        if (embers < 4 && gx * gx + gy * gy < 50 * 50) pr = "IL CANCELLO E' SIGILLATO: SERVONO 4 BRACI PRIMEVE";
        else if (near_npc() && near_brazier() < 0) pr = "SELECT  PARLA CON LA GUARDIANA";
        if (pr) {
            int tw = text_w(pr, 2);
            rectf((SW - tw) / 2 - 12, SH - 62, tw + 24, 17, rgba(RGB(8, 6, 8), 170));
            rectf((SW - tw) / 2 - 12, SH - 62, tw + 24, 1, rgba(GOLD, 160));
            text_c(SH - 57, 2, RGB(240, 220, 180), pr);
        }
    }
    if (Bs.state != BS_OFF && Bs.state != BS_DEAD) {
        text_c(SH - 40, 2, RGB(235, 225, 205), "IL CUSTODE CINEREO");
        fancy_bar(90, SH - 28, 300, 4, (float)Bs.hp, 0, (float)Bs.maxhp, RGB(160, 28, 26), RGB(220, 80, 60));
        diamond(84, SH - 26, 4, GOLD, rgba(RGB(30, 20, 20), 220));
    }
    /* bersaglio agganciato */
    if (msgT > 0 && mode == M_PLAY) {
        int a = msgT > 100 ? (120 - msgT) * 10 : (msgT < 30 ? msgT * 6 : 180);
        if (a > 180) a = 180;
        if (a < 0) a = 0;
        rectf(0, 74, SW, 34, rgba(RGB(6, 4, 6), a));
        rectf(80, 74, SW - 160, 1, rgba(GOLD, a));
        rectf(80, 107, SW - 160, 1, rgba(GOLD, a));
        text_c(82, 4, RGB(235, 220, 190), msg);
    }
}

static void rect_moon_haze(void)
{
    int i;
    for (i = 0; i < 6; i++) rectf(300 - i * 6, 58 + i * 7, 140 + i * 12, 3, rgba(RGB(30, 32, 46), 160));
}

static void draw_story_screen(const char *a, const char *b, int t, int last)
{
    int y, al = t * 8 > 255 ? 255 : t * 8;
    V3 e = v3(0, 0, 0), tt = v3(0, 0, -1);
    r_begin(RGB(6, 5, 8), e, tt, 100, 200, RGB(0, 0, 0));
    r_begin2d();
    for (y = 0; y < SH; y += 4) rectf(0, y, SW, 4, RGB(6 + y / 30, 5 + y / 34, 9 + y / 22));
    for (y = 0; y < 50; y++) rectf((int)(hash2(y, 9) % SW), (int)((hash2(y, 5) % SH + (unsigned)frame / 3) % SH), 1, 1, RGB(110, 104, 100));
    rectf(60, 100, SW - 120, 1, rgba(GOLD, al * 3 / 4));
    rectf(60, 170, SW - 120, 1, rgba(GOLD, al * 3 / 4));
    text_c(116, last ? 5 : 2, rgba(RGB(235, 220, 190), al), a);
    if (b && *b) text_c(140, 2, rgba(RGB(235, 220, 190), al), b);
    if (t > 30 && (frame / 30) & 1) text_c(236, 1, RGB(140, 130, 120), "X  CONTINUA");
    vignette();
    r_end();
}

static void draw_title(void)
{
    int y, i;
    V3 e = v3(0, 0, 0), t = v3(0, 0, -1);
    r_begin(RGB(10, 8, 14), e, t, 100, 200, RGB(0, 0, 0));
    r_begin2d();
    for (y = 0; y < SH; y += 4) rectf(0, y, SW, 4, RGB(14 + y / 18, 14 + y / 20, 24 + y / 12));
    for (i = 0; i < 70; i++) {
        int px = (int)(hash2(i, 7) % SW);
        int py = (int)((hash2(i, 3) % SH + SH - (unsigned)(frame * (1 + i % 3)) / 2 % SH) % SH);
        rectf(px, py, 1, 1, RGB(150, 140, 140));
    }
    for (y = -40; y <= 40; y++) {
        int w = (int)sqrtf((float)(40 * 40 - y * y));
        rectf(360 - w - 8, 70 + y, w * 2 + 16, 1, rgba(RGB(180, 190, 220), 20));
        rectf(360 - w, 70 + y, w * 2, 1, RGB(205 + y / 4, 210 + y / 4, 222));
    }
    rect_moon_haze();
    rectf(150, 150, 180, 122, RGB(6, 5, 9));
    rectf(225, 80, 30, 80, RGB(6, 5, 9));
    for (i = 0; i < 20; i++) rectf(240 - i, 60 + i, i * 2 + 1, 1, RGB(6, 5, 9));
    rectf(170, 120, 20, 40, RGB(6, 5, 9)); rectf(290, 120, 20, 40, RGB(6, 5, 9));
    for (i = 0; i < 12; i++) { rectf(180 - i / 2, 108 + i, i + 1, 1, RGB(6, 5, 9)); rectf(300 - i / 2, 108 + i, i + 1, 1, RGB(6, 5, 9)); }
    rectf(60, 190, 90, 82, RGB(8, 7, 11)); rectf(330, 200, 110, 72, RGB(8, 7, 11));
    rectf(80, 170, 12, 20, RGB(8, 7, 11)); rectf(400, 175, 14, 25, RGB(8, 7, 11));
    for (i = 0; i < 8; i++) rectf(70 + i * 9, 205 + (i % 3) * 14, 3, 5, RGB(220, 150, 70));
    for (i = 0; i < 6; i++) rectf(345 + i * 14, 215 + (i % 2) * 16, 3, 5, RGB(220, 150, 70));
    rectf(236, 170, 8, 14, RGB(255, 160, 60));
    rectf(0, 246, SW, 26, rgba(RGB(120, 125, 150), 40));
    text_c(30, 5, RGB(230, 210, 175), "CENERE DI VELMORA");
    rectf(130, 62, 220, 1, rgba(GOLD, 200));
    diamond(240, 62, 3, GOLD, RGB(30, 20, 20));
    if (hasSave) {
        text_c(200, 3, titleSel == 0 ? RGB(255, 215, 150) : RGB(150, 140, 130), "CONTINUA");
        text_c(222, 3, titleSel == 1 ? RGB(255, 215, 150) : RGB(150, 140, 130), "NUOVA PARTITA");
        diamond(150, titleSel == 0 ? 207 : 229, 3, GOLD, GOLD);
    } else if ((frame / 30) & 1) text_c(212, 3, RGB(220, 200, 165), "PREMI START");
    text_c(252, 2, RGB(150, 140, 140), "UN SOULSLIKE ORIGINALE PER PSP");
    vignette();
    r_end();
}

/* --- aspetto dei boss (forme arrotondate) --- */
static void rb_telegraphs(int i)
{
    RBoss *b = &RB[i];
    float gh = ground_h(b->x, b->y);
    cur_gy = gh;
    if (b->state == RS_WIND && b->atk == RA_SLAM) {
        int W = rb_wind_time(i, RA_SLAM);
        float rr = (i == RB_CAMPANARO ? 58.0f : 48.0f), t = (float)(W - b->timer) / W;
        cur_gy = ground_h(b->tx, b->ty);
        fan(b->tx, b->ty, 0.7f, rr - 3, rr, 0, 2 * PI, rgba(RGB(220, 40, 30), 200), 24);
        ground_spot(b->tx, b->ty, rr * 1.2f * t, 0.6f, rgba(RGB(220, 40, 20), 140), RB_ADD);
    }
    cur_gy = gh;
    if (b->state == RS_WIND && b->atk == RA_CHARGE && (b->timer & 4)) {
        float c = b->fx, s = b->fy, sx = -s, sz = c, w = RBD[i].body * 0.7f;
        V3 a = v3(b->x + sx * w, gh + 0.7f, b->y + sz * w), bb = v3(b->x - sx * w, gh + 0.7f, b->y - sz * w);
        V3 cc = v3(bb.x + c * 140, gh + 0.7f, bb.z + s * 140), dd = v3(a.x + c * 140, gh + 0.7f, a.z + s * 140);
        quad(a, bb, cc, dd, rgba(RGB(220, 50, 30), 90));
    }
    if (b->state == RS_WIND && (b->atk == RA_WAVE || b->atk == RA_BOLTS) && (b->timer & 4))
        ground_spot(b->x, b->y, RBD[i].body * 3, 0.7f, rgba(RGB(220, 60, 30), 120), RB_ADD);
    cur_gy = 0;
}

static void draw_matriarca(RBoss *b, float gh)
{
    /* lupa gigantesca dal manto di cenere, con braci tra il pelo */
    float ang = atan2f(b->fy, b->fx), ph = frame * (b->state == RS_CHASE ? 0.3f : 0.05f), amp = b->state == RS_CHASE ? 1 : 0.2f;
    float crouch = b->state == RS_WIND ? 3 : 0, S = 2.4f;
    uint32_t fur = b->flash ? RGB(255, 255, 255) : RGB(108, 102, 98), dark = shade(fur, 0.65f), ember = b->phase ? RGB(255, 100, 40) : RGB(255, 160, 70);
    int i;
    pb_x = b->x; pb_z = b->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh - crouch + b->z;
    TEXS(TX_BARK, 6);
    for (i = 0; i < 4; i++) {
        float lf = (i < 2 ? 3.2f : -3.6f) * S, ls = ((i & 1) ? 1.6f : -1.6f) * S;
        float a = sinf(ph + ((i == 0 || i == 3) ? 0 : PI)) * 0.55f * amp;
        float kf = lf + sinf(a) * 3 * S, ku = 6.2f * S - cosf(a) * 3 * S;
        limb_taper = 0.75f;
        limbL(lf, 6.6f * S, ls, kf, ku, ls, 0.8f * S, 0.9f * S, dark);
        limbL(kf, ku, ls, kf + sinf(a * 0.4f) * 3 * S, 0.5f + crouch, ls, 0.55f * S, 0.6f * S, dark);
        limb_taper = 1.0f;
    }
    blobL(-0.5f * S, 7.2f * S, 0, 5.8f * S, 2.6f * S, 2.4f * S, fur, 6, 12);
    blobL(3.0f * S, 7.8f * S, 0, 3.0f * S, 3.1f * S, 2.6f * S, fur, 6, 10);
    blobL(2.0f * S, 9.8f * S, 0, 3.4f * S, 1.9f * S, 2.5f * S, dark, 5, 10);           /* criniera */
    blobL(6.6f * S, 9.0f * S - crouch, 0, 2.3f * S, 1.8f * S, 1.8f * S, fur, 6, 10);
    blobL(8.8f * S, 8.2f * S - crouch, 0, 1.8f * S, 0.95f * S, 1.0f * S, shade(fur, 0.9f), 5, 8);
    if (b->state == RS_ACT || b->state == RS_ROAR) blobL(8.9f * S, 7.3f * S - crouch, 0, 1.4f * S, 0.4f * S, 0.8f * S, RGB(120, 30, 30), 3, 6);
    limbL(5.9f * S, 10.3f * S - crouch, 1.0f * S, 5.4f * S, 12.4f * S - crouch, 1.2f * S, 0.4f * S, 0.25f * S, dark);
    limbL(5.9f * S, 10.3f * S - crouch, -1.0f * S, 5.4f * S, 12.4f * S - crouch, -1.2f * S, 0.4f * S, 0.25f * S, dark);
    limb_taper = 0.4f;
    limbL(-5.8f * S, 7.8f * S, 0, -10 * S, 6.0f * S + sinf(ph) * 2, sinf(ph * 0.7f) * 4, 0.9f * S, 0.9f * S, fur);
    limb_taper = 1.0f;
    TEXS(TX_WHITE, 32);
    {
        V3 e1 = L2W(8.0f * S, 9.6f * S - crouch, 0.8f * S), e2 = L2W(8.0f * S, 9.6f * S - crouch, -0.8f * S), c1 = L2W(1 * S, 10.5f * S, 0.8f * S);
        cur_gy = 0;
        box(e1.x, e1.y - 0.4f, e1.z, 0.5f, 0.8f, 0.5f, ang, ember, ember, 1, 1);
        box(e2.x, e2.y - 0.4f, e2.z, 0.5f, 0.8f, 0.5f, ang, ember, ember, 1, 1);
        glow((e1.x + e2.x) * 0.5f, e1.y, (e1.z + e2.z) * 0.5f, 7, rgba(ember, 160));
        glow(c1.x, c1.y, c1.z, 10, rgba(ember, 90));
    }
}

static void draw_campanaro(RBoss *b, float gh)
{
    /* figura altissima e scarna, con un'enorme campana di bronzo in cima a un'asta */
    float ang = atan2f(b->fy, b->fx), br = sinf(frame * 0.05f) * 0.4f, e;
    uint32_t robe = b->flash ? RGB(255, 255, 255) : RGB(62, 58, 66), skin = RGB(170, 160, 150), bronze = RGB(170, 128, 70);
    int i;
    pb_x = b->x; pb_z = b->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh;
    if (b->state == RS_WIND && b->atk == RA_SLAM) e = 2.4f * (1 - (float)b->timer / rb_wind_time(1, RA_SLAM));
    else if (b->state == RS_ACT && b->atk == RA_SLAM) e = -0.5f;
    else if (b->state == RS_WIND && b->atk == RA_SWIPE) e = 0.4f;
    else e = 1.2f;
    TEXS(TX_CLOTH, 8);
    limb_taper = 0.55f;
    limbL(0, 0, 0, 0.5f, 22, 0, 6.5f, 5.5f, robe);                       /* tunica lunga */
    limb_taper = 1.0f;
    for (i = -2; i <= 2; i++) limbL(-1, 2, i * 2.2f, -1.6f - sinf(frame * 0.05f + i) * 0.6f, -0.3f, i * 2.6f, 0.9f, 0.4f, shade(robe, 0.8f));
    blobL(0.8f, 26 + br, 0, 2.8f, 4.2f, 3.6f, robe, 5, 9);
    TEXS(TX_WHITE, 32);
    limbL(1.6f, 29 + br, 0, 2.4f, 31.5f + br, 0, 0.9f, 0.9f, skin);
    blobL(2.6f, 33 + br, 0, 1.7f, 2.2f, 1.6f, skin, 5, 8);                  /* testa calva e cieca */
    limbL(4.1f, 33.4f + br, -1.2f, 4.1f, 33.4f + br, 1.2f, 0.2f, 0.25f, RGB(40, 30, 30));   /* benda */
    TEXS(TX_CLOTH, 8);
    limbL(1, 28 + br, 3.6f, 3.5f, 22, 4.5f, 0.8f, 0.8f, robe);
    limbL(1, 28 + br, -3.6f, 3.5f, 22, -4.5f, 0.8f, 0.8f, robe);
    {   /* asta e campana */
        float hf = 4 + cosf(e) * 2, hu = 22 + sinf(e) * 2;
        float tf = hf + cosf(e) * 22, tu = hu + sinf(e) * 22;
        TEXS(TX_WOOD, 10);
        limbL(hf - cosf(e) * 4, hu - sinf(e) * 4, 0, tf, tu, 0, 0.6f, 0.6f, RGB(110, 84, 60));
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        limb_taper = 1.9f;
        limbL(tf, tu, 0, tf + cosf(e - 1.57f) * 7, tu + sinf(e - 1.57f) * 7, 0, 3.0f, 3.0f, bronze);
        limb_taper = 1.0f;
        blobL(tf, tu, 0, 3.0f, 3.0f, 3.0f, bronze, 5, 9);
        mat_metal = 0;
    }
    TEXS(TX_WHITE, 32);
}

static void draw_annegata(RBoss *b, float gh)
{
    /* figura pallida che emerge dall'acqua, capelli lunghi e catene spezzate ai polsi */
    float ang = atan2f(b->fy, b->fx), sw = sinf(frame * 0.04f), lift = b->state == RS_WIND ? 2 : 0;
    uint32_t skin = b->flash ? RGB(255, 255, 255) : RGB(150, 170, 175), hair = RGB(30, 36, 40), cloth = RGB(70, 86, 92);
    int i;
    pb_x = b->x; pb_z = b->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh + lift;
    TEXS(TX_CLOTH, 8);
    limb_taper = 0.6f;
    limbL(0, 0, 0, 0.3f, 16, 0, 5.5f, 4.5f, cloth);                     /* veste fradicia */
    limb_taper = 1.0f;
    blobL(0.5f, 18, 0, 2.4f, 3.4f, 3.0f, skin, 5, 9);
    blobL(1.4f, 23.5f, 0, 1.7f, 2.1f, 1.6f, skin, 5, 8);
    /* capelli che ricadono */
    for (i = -2; i <= 2; i++) limbL(0.6f, 25, i * 0.7f, -1.2f + sw * 0.5f, 12 + (i & 1) * 2, i * 1.3f, 0.5f, 0.35f, hair);
    blobL(1.1f, 24.5f, 0, 1.9f, 1.6f, 1.8f, hair, 4, 8);
    /* braccia lunghe e catene */
    for (i = 0; i < 2; i++) {
        float ss = i ? -2.8f : 2.8f, rf = b->state == RS_ACT ? 7 : 3 + sw;
        limbL(0.5f, 20.5f, ss, rf, 13 + (b->state == RS_WIND ? 8 : 0), ss * 1.6f, 0.65f, 0.65f, skin);
        mat_metal = 1;
        TEXS(TX_METAL, 6);
        limbL(rf, 13 + (b->state == RS_WIND ? 8 : 0), ss * 1.6f, rf - 2, 3, ss * 2, 0.3f, 0.3f, RGB(80, 84, 90));
        mat_metal = 0;
        TEXS(TX_CLOTH, 8);
    }
    TEXS(TX_WHITE, 32);
    {
        V3 e1 = L2W(2.9f, 24, 0.6f), e2 = L2W(2.9f, 24, -0.6f);
        box(e1.x, e1.y, e1.z, 0.25f, 0.4f, 0.25f, ang, RGB(150, 220, 255), RGB(150, 220, 255), 1, 1);
        box(e2.x, e2.y, e2.z, 0.25f, 0.4f, 0.25f, ang, RGB(150, 220, 255), RGB(150, 220, 255), 1, 1);
        glow((e1.x + e2.x) * 0.5f, e1.y, (e1.z + e2.z) * 0.5f, 5, rgba(RGB(120, 200, 255), 150));
    }
    cur_gy = gh;
    ground_spot(b->x, b->y, 30, 0.4f, rgba(RGB(80, 140, 200), 50), RB_ADD);
    cur_gy = 0;
}

static void draw_re(RBoss *b, float gh)
{
    /* sovrano di pietra coronato, con un grande spadone a due mani */
    float ang = atan2f(b->fy, b->fx), br = sinf(frame * 0.05f) * 0.4f, a2;
    uint32_t stone = b->flash ? RGB(255, 255, 255) : RGB(132, 126, 120), dark = shade(stone, 0.7f), gold = RGB(190, 150, 80);
    int i;
    pb_x = b->x; pb_z = b->y; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh + b->z;
    if (b->state == RS_WIND && b->atk == RA_SWIPE) a2 = 1.8f;
    else if (b->state == RS_ACT && b->atk == RA_SWIPE) a2 = 1.8f - 3.6f * (12 - b->timer) / 12.0f;
    else a2 = 0.5f;
    TEXS(TX_ROCK, 10);
    for (i = 0; i < 2; i++) {
        float side = i ? -4.0f : 4.0f, a = sinf(frame * (b->state == RS_CHASE ? 0.1f : 0) + i * PI) * 0.35f;
        float kf = sinf(a) * 8, ku = 16 - cosf(a) * 8;
        limb_taper = 0.85f;
        limbL(0, 17, side, kf, ku, side, 2.8f, 3.0f, dark);
        blobL(kf, ku, side, 2.4f, 2.4f, 2.4f, dark, 4, 7);
        limbL(kf, ku, side, kf + 0.5f, 1.5f, side, 2.3f, 2.5f, dark);
        limb_taper = 1.0f;
        blobL(kf + 1.8f, 1.0f, side, 3.4f, 1.5f, 2.2f, dark, 4, 7);
    }
    blobL(0.5f, 21 + br, 0, 4.2f, 5.0f, 6.8f, stone, 6, 10);
    blobL(1.2f, 28 + br, 0, 4.4f, 4.2f, 8.2f, stone, 6, 10);
    TEXS(TX_CLOTH, 10);
    for (i = -2; i <= 2; i++) limbL(-3.5f, 31, i * 3, -5 - sinf(frame * 0.04f + i) * 0.8f, 3, i * 3.6f, 1.4f, 0.4f, RGB(90, 40, 44));   /* mantello regale */
    TEXS(TX_ROCK, 10);
    blobL(3.0f, 35 + br, 0, 2.6f, 3.0f, 2.6f, stone, 5, 9);
    mat_metal = 1;
    TEXS(TX_METAL, 6);
    limb_taper = 1.15f;
    limbL(2.8f, 37 + br, 0, 2.8f, 39.5f + br, 0, 2.4f, 2.4f, gold);         /* corona */
    limb_taper = 1.0f;
    for (i = 0; i < 6; i++) {
        float a = i * PI / 3;
        limbL(2.8f + cosf(a) * 2.6f, 39.4f + br, sinf(a) * 2.6f, 2.8f + cosf(a) * 2.6f, 41.4f + br, sinf(a) * 2.6f, 0.3f, 0.3f, gold);
    }
    mat_metal = 0;
    TEXS(TX_ROCK, 10);
    {   /* braccia e spadone */
        float hf = 6 + cosf(a2) * 4, hu = 24, hs = sinf(a2) * 6;
        limbL(1.5f, 29, 7.5f, hf, hu, hs + 2, 2.0f, 2.0f, stone);
        limbL(1.5f, 29, -7.5f, hf, hu, hs - 2, 2.0f, 2.0f, stone);
        blobL(hf, hu, hs, 2.2f, 2.0f, 2.6f, dark, 4, 7);
        mat_metal = 1;
        TEXS(TX_METAL, 8);
        limbL(hf, hu, hs, hf + cosf(a2) * 34, hu + 1, hs + sinf(a2) * 34, 0.5f, 2.2f, RGB(150, 150, 160));
        limbL(hf - sinf(a2) * 4, hu, hs + cosf(a2) * 4, hf + sinf(a2) * 4, hu, hs - cosf(a2) * 4, 0.6f, 0.6f, gold);
        mat_metal = 0;
    }
    TEXS(TX_WHITE, 32);
    {
        V3 e1 = L2W(5.4f, 35.5f + br, 0.9f), e2 = L2W(5.4f, 35.5f + br, -0.9f);
        box(e1.x, e1.y, e1.z, 0.3f, 0.5f, 0.35f, ang, RGB(255, 150, 60), RGB(255, 150, 60), 1, 1);
        box(e2.x, e2.y, e2.z, 0.3f, 0.5f, 0.35f, ang, RGB(255, 150, 60), RGB(255, 150, 60), 1, 1);
        glow((e1.x + e2.x) * 0.5f, e1.y, (e1.z + e2.z) * 0.5f, 6, rgba(RGB(255, 150, 60), 150));
    }
}

static void rb_draw(void)
{
    int i, k;
    for (i = 0; i < NRB; i++) {
        RBoss *b = &RB[i];
        float gh, ex = b->x - camEye.x, ez = b->y - camEye.z;
        if (b->state == RS_DEAD && b->timer <= 0) continue;
        if (ex * ex + ez * ez > 300 * 300) continue;
        light_at(b->x, b->y);
        gh = ground_h(b->x, b->y);
        if (i == RB_ANNEGATA) gh = WL + 0.9f;
        if (b->state == RS_DEAD) {
            float t = b->timer / 120.0f;
            cur_gy = gh;
            glow(b->x, 15 * t, b->y, 40 * t + 4, rgba(RGB(255, 150, 60), 200));
            cur_gy = 0;
            continue;
        }
        rb_telegraphs(i);
        cur_gy = gh; shadow(b->x, b->y, RBD[i].body + 2 - b->z / 4); cur_gy = 0;
        hi_detail = 0;
        if (i == RB_MATRIARCA) draw_matriarca(b, gh);
        else if (i == RB_CAMPANARO) draw_campanaro(b, gh);
        else if (i == RB_ANNEGATA) draw_annegata(b, gh);
        else draw_re(b, gh);
    }
    for (k = 0; k < MAXBOLT; k++)
        if (bolts[k].life > 0) {
            cur_gy = ground_h(bolts[k].x, bolts[k].y);
            if (cur_gy < WL + 1) cur_gy = WL + 1;
            glow(bolts[k].x, 8, bolts[k].y, 5, rgba(RGB(120, 200, 255), 220));
            glow(bolts[k].x, 8, bolts[k].y, 2, rgba(RGB(230, 250, 255), 255));
        }
    for (k = 0; k < MAXWAVE; k++)
        if (waves[k].maxr > 0) {
            cur_gy = ground_h(waves[k].x, waves[k].y);
            fan(waves[k].x, waves[k].y, 1.0f, waves[k].r - 3, waves[k].r + 1, 0, 2 * PI, rgba(RGB(255, 170, 90), 150), 32);
        }
    cur_gy = 0;
}


static void draw_npc(void)
{
    /* vecchia incappucciata con bastone e candela */
    float x = npc_x(), z = npc_y(), gh, ang;
    float ex = x - camEye.x, ez = z - camEye.z;
    if (ex * ex + ez * ez > 260 * 260) return;
    gh = ground_h(x, z);
    ang = atan2f(P.y - z, P.x - x);
    light_at(x, z);
    cur_gy = gh; shadow(x, z, 5); cur_gy = 0;
    pb_x = x; pb_z = z; pb_c = cosf(ang); pb_s = sinf(ang); pb_rolling = 0; pb_y = gh;
    TEXS(TX_CLOTH, 8);
    limb_taper = 0.6f;
    limbL(0, 0, 0, 0.6f, 13, 0, 4.2f, 3.6f, RGB(84, 72, 70));
    limb_taper = 1.0f;
    blobL(0.6f, 13.5f, 0, 2.6f, 2.0f, 3.0f, RGB(70, 60, 58), 5, 9);
    blobL(1.6f, 15.5f, 0, 1.8f, 2.0f, 1.8f, RGB(64, 56, 54), 5, 9);
    TEXS(TX_WHITE, 32);
    blobL(2.6f, 15.2f, 0, 0.9f, 1.1f, 1.0f, RGB(190, 160, 140), 4, 7);
    TEXS(TX_WOOD, 6);
    limbL(2.2f, 0, 2.6f, 2.8f, 17, 2.4f, 0.3f, 0.3f, RGB(100, 76, 54));
    {
        V3 c = L2W(2.8f, 12, -2.4f);
        TEXS(TX_WHITE, 32);
        box(c.x, c.y - 1, c.z, 0.35f, 1.2f, 0.35f, ang, RGB(230, 220, 190), RGB(230, 220, 190), 1, 1);
        glow(c.x, c.y + 0.6f, c.z, 5, rgba(RGB(255, 190, 100), 200));
    }
}

void game_render(void)
{
    int i;
    float fx, fz, tx, tz, hd;
    V3 eye, tgt;

    if (mode == M_TITLE) { draw_title(); return; }
    if (mode == M_STORY) { draw_story_screen(intro_text[storyPage][0], intro_text[storyPage][1], storyT, 0); return; }
    if (mode == M_END && storyPage < 5) { draw_story_screen(ending_text[storyPage][0], ending_text[storyPage][1], storyT, storyPage == 4); return; }
    if (mode == M_END) {
        char b1[48], b2[48], n[12];
        int m = playtime / 3600;
        strcpy(b1, "TEMPO DI GIOCO ");
        itoa_s(m / 60, n); strcat(b1, n); strcat(b1, " ORE ");
        itoa_s(m % 60, n); strcat(b1, n); strcat(b1, " MIN");
        strcpy(b2, "MORTI "); itoa_s(deaths, n); strcat(b2, n); strcat(b2, "   LIVELLO "); itoa_s(P.lvl, n); strcat(b2, n);
        draw_story_screen(b1, b2, storyT, 0);
        return;
    }

    fx = cosf(camYaw); fz = sinf(camYaw);
    tx = camX; tz = camZ;
    if (shake > 0) { tx += (float)((int)(rnd() % 5) - 2); tz += (float)((int)(rnd() % 5) - 2); }
    {
        /* telecamera alle spalle con collisione contro gli edifici */
        float want = 62.0f, t, dist, cp = cosf(camPitch), sp = sinf(camPitch);
        float lx, ly, ox = tx, oz = tz;
        if (P.lock != -1 && lock_pos(&lx, &ly)) { ox = tx + (lx - tx) * 0.25f; oz = tz + (ly - tz) * 0.25f; }
        camGY += (ground_h(P.x, P.y) - camGY) * 0.2f;
        tgt = v3(ox, 16 + camGY, oz);
        dist = want;
        for (t = 6; t <= want; t += 2) {
            float px = ox - fx * cp * t, pz = oz - fz * cp * t, py = 16 + camGY + sp * t;
            int ttx = (int)floorf(px / TS), ttz = (int)floorf(pz / TS);
            int wall = ttx < 0 || ttz < 0 || ttx >= MW || ttz >= MH || map[ttz][ttx] == T_WALL || map[ttz][ttx] == T_RUIN;
            if ((wall && py < 52 + ground_h(px, pz)) || py < ground_h(px, pz) + 4 ||
                (ttx >= 0 && ttz >= 0 && ttx < MW && ttz < MH && map[ttz][ttx] == T_ROCK &&
                 py < (hgt[ttz][ttx] > hgt[ttz + 1][ttx + 1] ? hgt[ttz][ttx] : hgt[ttz + 1][ttx + 1]) + 14)) { dist = t - 5; break; }
        }
        if (dist < 22) dist = 22;
        if (dist < camDistS) camDistS = dist; else camDistS += (dist - camDistS) * 0.08f;
        hd = camDistS * cp;
        eye = v3(ox - fx * hd, 16 + camGY + camDistS * sp + (want - camDistS) * 0.9f, oz - fz * hd);
    }
    {   /* assi della telecamera per i billboard */
        float dx = tgt.x - eye.x, dy = tgt.y - eye.y, dz = tgt.z - eye.z, l = sqrtf(dx * dx + dy * dy + dz * dz);
        float rx, rz, rl;
        dx /= l; dy /= l; dz /= l;
        rx = -dz; rz = dx; rl = sqrtf(rx * rx + rz * rz);
        camR = v3(rx / rl, 0, rz / rl);
        camU = v3(-camR.z * dy, camR.z * dx - camR.x * dz, camR.x * dy);
        l = sqrtf(camU.x * camU.x + camU.y * camU.y + camU.z * camU.z);
        camU = v3(camU.x / l, camU.y / l, camU.z / l);
        if (camU.y < 0) camU = v3(-camU.x, -camU.y, -camU.z);
    }
    camEye = eye;
    r_begin(endingSeen ? RGB(96, 80, 84) : FOGC, eye, tgt, 70, 270, endingSeen ? RGB(96, 80, 84) : FOGC);

    {   /* luna velata nel cielo (additiva: la nebbia non la copre) */
        float ma = -0.9f, mx = eye.x + cosf(ma) * 320, mz = eye.z + sinf(ma) * 320, my = eye.y + 170;
        glow(mx, my, mz, 110, rgba(RGB(90, 110, 160), 80));
        glow(mx, my, mz, 34, rgba(RGB(150, 165, 200), 150));
        glow(mx, my, mz, 18, rgba(RGB(235, 238, 250), 255));
    }
    draw_world(tx + fx * 100, tz + fz * 100);
    draw_blood();
    for (i = 0; i < nbraz; i++) draw_brazier(i);
    cur_gy = 0;
    for (i = 0; i < npick; i++)
        if (!picks[i].taken) {
            float px = (picks[i].tx - WX0) * TS + 8.0f, pz = (picks[i].ty - WY0) * TS + 8.0f, bob = sinf(frame * 0.08f + i) * 1.5f;
            float ex = px - camEye.x, ez = pz - camEye.z;
            uint32_t pc = picks[i].type == 1 ? RGB(255, 150, 60) : RGB(210, 220, 255);
            if (ex * ex + ez * ez > 250 * 250) continue;
            cur_gy = ground_h(px, pz);
            cube(px, 5 + bob, pz, 1.0f, frame * 0.06f, pc, 1, 1);
            glow(px, 7 + bob, pz, 8, rgba(pc, 160));
            glow(px, 7 + bob, pz, 3, rgba(RGB(255, 255, 255), 220));
            {   /* colonna di luce */
                V3 a = v3(px - camR.x * 2, cur_gy, pz - camR.z * 2), b = v3(px + camR.x * 2, cur_gy, pz + camR.z * 2);
                V3 c = v3(px + camR.x * 0.5f, cur_gy + 40, pz + camR.z * 0.5f), d = v3(px - camR.x * 0.5f, cur_gy + 40, pz - camR.z * 0.5f);
                emit_quad(a, b, c, d, rgba(pc, 90), rgba(pc, 90), rgba(pc, 0), rgba(pc, 0), 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, TX_WHITE, RB_ADD);
            }
            cur_gy = 0;
        }
    if (drop.active) {
        float bob = sinf(frame * 0.1f) * 2;
        cur_gy = ground_h(drop.x, drop.y);
        cube(drop.x, 4 + bob, drop.y, 1.4f, frame * 0.08f, RGB(170, 235, 255), 1, 1);
        glow(drop.x, 6 + bob, drop.y, 9 + (frame / 6) % 4, rgba(RGB(90, 190, 255), 170));
        ground_spot(drop.x, drop.y, 16, 0.6f, rgba(RGB(80, 170, 240), 90), RB_ADD);
        cur_gy = 0;
    }
    for (i = 0; i < NE; i++) if (E[i].alive) draw_enemy(&E[i]);
    if (Bs.state != BS_OFF || !Bs.dead) draw_boss();
    rb_draw();
    draw_npc();
    draw_player();
    for (i = 0; i < MAXPART; i++)
        if (parts[i].life > 0) {
            cur_gy = ground_h(parts[i].x, parts[i].y);
            glow(parts[i].x, 6 + parts[i].life * 0.4f, parts[i].y, parts[i].life > 12 ? 2.2f : 1.4f, rgba(parts[i].col, 230));
        }
    cur_gy = 0;
    {
        float lx, ly;
        if (P.lock != -1 && lock_pos(&lx, &ly)) {
            float hy = P.lock == 100 ? 56.0f : (P.lock >= 0 && E[P.lock].kind == 3 ? 44.0f : (P.lock >= 0 && E[P.lock].kind == 2 ? 18.0f : 30.0f));
            cur_gy = ground_h(lx, ly);
            cube(lx, hy, ly, 1.0f, frame * 0.15f, RGB(240, 240, 255), 1, 1);
            glow(lx, hy + 1, ly, 6, rgba(RGB(220, 230, 255), 200));
            cur_gy = 0;
        }
    }
    draw_atmosphere(tx, tz);

    r_begin2d();
    vignette();
    draw_hud();

    if (mode == M_DEAD) {
        int t = 170 - deadT;
        if (t > 20) shade_screen(t > 60 ? 170 : (t - 20) * 4);
        if (t > 30) {
            rectf(0, 110, SW, 44, rgba(RGB(8, 2, 4), 220));
            rectf(60, 110, SW - 120, 1, rgba(RGB(150, 30, 30), 200));
            rectf(60, 153, SW - 120, 1, rgba(RGB(150, 30, 30), 200));
            text_c(120, 5, RGB(170, 24, 28), "SEI CADUTO");
        }
    } else if (mode == M_TALK) {
        rectf(30, SH - 86, SW - 60, 56, rgba(RGB(10, 8, 10), 225));
        rectf(30, SH - 86, SW - 60, 1, GOLD); rectf(30, SH - 31, SW - 60, 1, GOLDD);
        text(44, SH - 80, 1, RGB(255, 200, 130), "ILDE, LA GUARDIANA");
        text(44, SH - 64, 2, RGB(235, 225, 205), talk_a);
        text(44, SH - 50, 2, RGB(235, 225, 205), talk_b);
    } else if (mode == M_PAUSE) {
        shade_screen(180);
        text_c(26, 4, RGB(235, 215, 180), "PAUSA");
        rectf(150, 54, 180, 1, rgba(GOLD, 200));
        text(110, 70, 2, RGB(200, 190, 170), "LEVETTA");        text(250, 70, 2, RGB(235, 225, 205), "MUOVI");
        text(110, 86, 2, RGB(200, 190, 170), "CROCE / L+LEV.");  text(250, 86, 2, RGB(235, 225, 205), "VISUALE  L RICENTRA");
        text(110, 102, 2, RGB(200, 190, 170), "QUADRATO");      text(250, 102, 2, RGB(235, 225, 205), "ATTACCO");
        text(110, 118, 2, RGB(200, 190, 170), "TRIANGOLO");     text(250, 118, 2, RGB(235, 225, 205), "ATTACCO PESANTE");
        text(110, 134, 2, RGB(200, 190, 170), "X");             text(250, 134, 2, RGB(235, 225, 205), "SCHIVATA");
        text(110, 150, 2, RGB(200, 190, 170), "CERCHIO");       text(250, 150, 2, RGB(235, 225, 205), "FIALA DI BRACE");
        text(110, 166, 2, RGB(200, 190, 170), "R");             text(250, 166, 2, RGB(235, 225, 205), "AGGANCIO");
        text(110, 182, 2, RGB(200, 190, 170), "SELECT");        text(250, 182, 2, RGB(235, 225, 205), "BRACIERE");
        text_c(226, 2, RGB(160, 150, 135), "START  RIPRENDI");
    } else if (mode == M_MENU) {
        static const char *opts[4] = { "VIGORE", "RESISTENZA", "FORZA", "ESCI" };
        char b[48], n[12];
        shade_screen(150);
        rectf(120, 36, 240, 200, rgba(RGB(14, 11, 14), 235));
        rectf(120, 36, 240, 1, GOLD); rectf(120, 235, 240, 1, GOLDD);
        rectf(120, 36, 1, 200, GOLDD); rectf(359, 36, 1, 200, GOLDD);
        diamond(240, 36, 5, GOLD, RGB(40, 24, 20));
        text_c(50, 3, RGB(250, 205, 140), "BRACIERE");
        strcpy(b, "LIVELLO "); itoa_s(P.lvl, n); strcat(b, n);
        text(140, 82, 2, RGB(220, 210, 195), b);
        strcpy(b, "COSTO "); itoa_s(lvlCost(), n); strcat(b, n);
        text(140, 96, 2, P.frags >= lvlCost() ? RGB(160, 220, 160) : RGB(210, 100, 100), b);
        strcpy(b, "FRAMMENTI "); itoa_s(P.frags, n); strcat(b, n);
        text(140, 110, 2, RGB(200, 200, 215), b);
        for (i = 0; i < 4; i++) {
            int v = i == 0 ? P.vig : (i == 1 ? P.end : P.str);
            uint32_t c = i == menuSel ? RGB(255, 215, 150) : RGB(165, 155, 145);
            if (i == menuSel) { rectf(132, 132 + i * 20, 216, 14, rgba(GOLD, 40)); diamond(140, 138 + i * 20, 3, GOLD, GOLD); }
            text(150, 135 + i * 20, 2, c, opts[i]);
            if (i < 3) { itoa_s(v, n); text(320, 135 + i * 20, 2, c, n); }
        }
        text(140, 220, 1, RGB(140, 130, 120), "X SCEGLI    CERCHIO ESCI");
    } else if (mode == M_WIN) {
        if (winT > 10) shade_screen(winT > 50 ? 160 : (winT - 10) * 4);
        if (winT > 20) {
            rectf(0, 100, SW, 70, rgba(RGB(14, 10, 6), 230));
            rectf(60, 100, SW - 120, 1, rgba(GOLD, 220));
            rectf(60, 169, SW - 120, 1, rgba(GOLD, 220));
            text_c(112, 4, RGB(255, 205, 120), "CUSTODE SCONFITTO");
            text_c(146, 2, RGB(210, 200, 180), "LA CENERE SI POSA SU VELMORA");
        }
        if (winT > 90) text_c(200, 2, RGB(160, 150, 135), "START  CONTINUA");
    }
    r_end();
}

/* schermata mostrata subito all'avvio, mentre il mondo viene generato */
void game_loading(void)
{
    V3 e = v3(0, 0, 0), t = v3(0, 0, -1);
    r_begin(RGB(8, 6, 10), e, t, 100, 200, RGB(0, 0, 0));
    r_begin2d();
    rectf(0, 0, SW, SH, RGB(8, 6, 10));
    text_c(110, 4, RGB(230, 210, 175), "CENERE DI VELMORA");
    text_c(150, 2, RGB(170, 160, 150), "CARICAMENTO...");
    r_end();
}

#ifdef GAME_DEBUG
static void teleport_w(int wx, int wy)
{
    move_window(wx - MW / 2, wy - MH / 2);
    P.x = (wx - WX0) * TS + 8; P.y = (wy - WY0) * TS + 8;
    camX = P.x; camZ = P.y;
}
void game_debug_teleport(int where)
{
    P.iframes = 100000; P.hp = (float)maxhp();
    switch (where) {
    case 1: teleport_w(COREX + CX0 + 69, COREY + CY0 + 36); break;
    case 2: teleport_w(COREX + CX0 + 38, COREY + CY0 + 14); break;
    case 3: teleport_w(COREX + 18, COREY + 84); break;
    case 4: teleport_w(COREX + 88, COREY + 30); break;
    case 5: teleport_w(COREX + 85, COREY + 119); break;
    case 6: teleport_w(COREX + 146, COREY + 80); break;
    case 7: teleport_w(COREX + 51, COREY + 128); break;
    case 8: teleport_w(COREX + 116, COREY + 29); break;
    case 9: teleport_w(COREX + 18, COREY + 35); break;
    case 11: teleport_w(COREX + 18, COREY + 109); break;
    case 12: teleport_w(COREX + 88, COREY + 15); break;
    case 13: teleport_w(COREX + 85, COREY + 125); break;
    case 14: teleport_w(COREX + 148, COREY + 96); break;
    case 15: teleport_w(COREX + CX0 + 9, COREY + CY0 + 7); break;
    default: {
        /* where >= 100: borgo procedurale numero (where-100) */
        int n = where - 100, cx, cy;
        for (cy = 0; cy < NCELL; cy++)
            for (cx = 0; cx < NCELL; cx++)
                if (ptown[cy][cx].ok && n-- == 0) {
                    const Town *t = &ptown[cy][cx];
                    teleport_w((t->x0 + t->x1) / 2, (t->y0 + t->y1) / 2 + 3);
                    return;
                }
        if (where >= 1000) teleport_w(where % 10000, where / 10000);   /* x + y*10000 */
    }
    }
}
int game_debug_tile(int x, int y) { return map[y][x]; }
int game_debug_h(int x, int y) { return (int)hgt[y][x]; }
int game_debug_ntowns(void) { int n = 0, x, y; for (y = 0; y < NCELL; y++) for (x = 0; x < NCELL; x++) n += ptown[y][x].ok; return n; }
int game_debug_world(int wx, int wy) { return (int)height_raw(wx, wy); }
int game_debug_town(int wx, int wy) { return town_at_w(wx, wy) != 0; }
int game_debug_ne(void) { int n = 0, i; for (i = 0; i < NE; i++) n += E[i].alive; return n; }
int game_debug_state(int what)
{
    switch (what) {
    case 0: return mode;
    case 1: return (int)P.hp;
    case 2: return Bs.hp;
    case 3: return Bs.state;
    case 4: return P.frags;
    case 5: return (int)camDistS;
    case 6: return (int)(camYaw * 100);
    case 7: return (int)P.x;
    case 8: return (int)P.y;
    case 9: return WX0;
    case 10: return WY0;
    case 11: return rbActive;
    case 12: return rbActive >= 0 ? RB[rbActive].hp : -1;
    case 13: return embers;
    case 14: return mode;
    }
    return 0;
}
#endif
