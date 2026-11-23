/* Backend PSP: GPU (sceGu/sceGum) con texture, Gouraud, trasparenze e luce additiva */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <string.h>
#include "render.h"

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272
#define MAXTEX 17

/* lista dei comandi grafici: contiene anche i vertici di ogni frame, quindi va dimensionata larga */
#define LIST_WORDS (1024 * 1024)
#define LIST_BYTES (LIST_WORDS * 4)
static unsigned int __attribute__((aligned(16))) list[LIST_WORDS];
static int list_used;   /* stima dei byte di vertici usati in questo frame */

typedef struct { float u, v; uint32_t color; float x, y, z; } GV;
typedef struct { uint32_t color; short x, y, z; } SV;

#define VFMT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define OPQ_MAX 6000    /* vertici per contenitore opaco (multiplo di 3) */
#define TRN_MAX 2400    /* trasparenti e additivi: disegnati a fine scena */
#define RECT_MAX 1024

static GV opq[MAXTEX][OPQ_MAX]; static int nopq[MAXTEX];
static GV trn[MAXTEX][TRN_MAX]; static int ntrn[MAXTEX];
static GV add[MAXTEX][TRN_MAX]; static int nadd[MAXTEX];
static SV rects[RECT_MAX * 2]; static int nrect;

static const uint32_t *texpix; static int texcount, texsize;
static int curtex = -1;

#define UNCACHED(p) ((void *)((uintptr_t)(p) | 0x40000000))

/* mipmap: versioni ridotte di ogni texture (1/2, 1/4, 1/8) per le superfici lontane:
 * niente sfarfallio e meno accessi alla memoria */
#define NMIP 3
static uint32_t *mips[MAXTEX][NMIP];
static uint32_t mipbuf[MAXTEX * (64 * 64 + 32 * 32 + 16 * 16)] __attribute__((aligned(16)));
static int mip_ok;

static void build_mips(void)
{
    int t, l, x, y, sz, used = 0;
    mip_ok = texsize == 128 && texcount <= MAXTEX;
    if (!mip_ok) return;
    for (t = 0; t < texcount; t++) {
        const uint32_t *src = texpix + t * texsize * texsize;
        sz = texsize;
        for (l = 0; l < NMIP; l++) {
            int ns = sz / 2;
            uint32_t *dst = mipbuf + used;
            mips[t][l] = dst;
            used += ns * ns;
            for (y = 0; y < ns; y++)
                for (x = 0; x < ns; x++) {
                    uint32_t a = src[(y * 2) * sz + x * 2], b = src[(y * 2) * sz + x * 2 + 1];
                    uint32_t c = src[(y * 2 + 1) * sz + x * 2], d = src[(y * 2 + 1) * sz + x * 2 + 1];
                    uint32_t o = 0;
                    int k;
                    for (k = 0; k < 32; k += 8) {
                        uint32_t v = (((a >> k) & 255) + ((b >> k) & 255) + ((c >> k) & 255) + ((d >> k) & 255) + 2) / 4;
                        o |= v << k;
                    }
                    dst[y * ns + x] = o;
                }
            src = dst; sz = ns;
        }
    }
}

static void bind_tex(int t)
{
    if (t == curtex) return;
    curtex = t;
    sceGuTexImage(0, texsize, texsize, texsize, texpix + t * texsize * texsize);
    if (mip_ok) {
        int l, sz = texsize / 2;
        for (l = 0; l < NMIP; l++, sz /= 2) sceGuTexImage(l + 1, sz, sz, sz, mips[t][l]);
    }
    sceGuTexFlush();
}

/* La scena si prepara mentre la PSP sta ancora disegnando quella precedente:
 * i comandi grafici partono solo quando servono (prima volta in un frame), e solo allora
 * si aspetta la fine del frame precedente e si scambiano gli schermi. */
static int started, have_prev;
static uint32_t pclear, pfogcol;
static float pfn, pff;
static ScePspFVector3 peye, ptgt;
static void ensure_started(void)
{
    ScePspFVector3 up = { 0, 1, 0 };
    if (started) return;
    started = 1;
    sceGuSync(0, 0);
    if (have_prev) { sceDisplayWaitVblankStart(); sceGuSwapBuffers(); }
    have_prev = 1;
    curtex = -1;
    list_used = 0;
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(pclear);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_FOG);
    sceGuFog(pfn, pff, pfogcol);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, (float)SCR_W / SCR_H, 4.0f, 1500.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&peye, &ptgt, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

static void draw_verts(GV *src, int n, int tex)
{
    GV *dst;
    if (n <= 0) return;
    ensure_started();
    if (list_used + n * (int)sizeof(GV) > LIST_BYTES - 128 * 1024) return;   /* lista piena: salta, niente crash */
    list_used += n * (int)sizeof(GV);
    bind_tex(tex);
    dst = (GV *)sceGuGetMemory(n * sizeof(GV));
    memcpy(UNCACHED(dst), src, n * sizeof(GV));
    sceGumDrawArray(GU_TRIANGLES, VFMT, n, 0, dst);
}

static void flush_rects(void)
{
    SV *dst;
    if (nrect <= 0) return;
    ensure_started();
    if (list_used + nrect * 2 * (int)sizeof(SV) > LIST_BYTES - 32 * 1024) { nrect = 0; return; }
    list_used += nrect * 2 * (int)sizeof(SV);
    dst = (SV *)sceGuGetMemory(nrect * 2 * sizeof(SV));
    memcpy(UNCACHED(dst), rects, nrect * 2 * sizeof(SV));
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, nrect * 2, 0, dst);
    nrect = 0;
}

void r_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void r_textures(const uint32_t *pixels, int count, int size)
{
    texpix = pixels; texcount = count; texsize = size;
    build_mips();
    sceKernelDcacheWritebackAll();
    if (mip_ok) {
        sceGuSync(0, 0);
        sceGuStart(GU_DIRECT, list);
        sceGuTexMode(GU_PSM_8888, NMIP, 0, 0);
        sceGuTexLevelMode(GU_TEXTURE_AUTO, 0.0f);
        sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR, GU_LINEAR);
        sceGuFinish();
        sceGuSync(0, 0);
    }
}

void r_begin(uint32_t clear, V3 eye, V3 target, float fog_near, float fog_far, uint32_t fogcol)
{
    int i;
    for (i = 0; i < MAXTEX; i++) nopq[i] = ntrn[i] = nadd[i] = 0;
    nrect = 0;
    started = 0;
    pclear = clear; pfogcol = fogcol; pfn = fog_near; pff = fog_far;
    peye.x = eye.x; peye.y = eye.y; peye.z = eye.z;
    ptgt.x = target.x; ptgt.y = target.y; ptgt.z = target.z;
}

static void put(GV *d, const RV *a, const RV *b, const RV *c)
{
    d[0].u = a->u; d[0].v = a->v; d[0].color = a->c; d[0].x = a->x; d[0].y = a->y; d[0].z = a->z;
    d[1].u = b->u; d[1].v = b->v; d[1].color = b->c; d[1].x = b->x; d[1].y = b->y; d[1].z = b->z;
    d[2].u = c->u; d[2].v = c->v; d[2].color = c->c; d[2].x = c->x; d[2].y = c->y; d[2].z = c->z;
}

void r_tri(const RV *a, const RV *b, const RV *c, int tex, int blend)
{
    if (tex < 0 || tex >= texcount || tex >= MAXTEX) tex = 0;
    if (blend == RB_ALPHA) {
        if (ntrn[tex] + 3 > TRN_MAX) return;
        put(&trn[tex][ntrn[tex]], a, b, c); ntrn[tex] += 3;
    } else if (blend == RB_ADD) {
        if (nadd[tex] + 3 > TRN_MAX) return;
        put(&add[tex][nadd[tex]], a, b, c); nadd[tex] += 3;
    } else {
        if (nopq[tex] + 3 > OPQ_MAX) { draw_verts(opq[tex], nopq[tex], tex); nopq[tex] = 0; }
        put(&opq[tex][nopq[tex]], a, b, c); nopq[tex] += 3;
    }
}

void r_begin2d(void)
{
    int i;
    ensure_started();
    for (i = 0; i < MAXTEX; i++) { draw_verts(opq[i], nopq[i], i); nopq[i] = 0; }
    sceGuEnable(GU_BLEND);
    sceGuDepthMask(GU_TRUE);                 /* trasparenti: niente scrittura Z */
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    for (i = 0; i < MAXTEX; i++) { draw_verts(trn[i], ntrn[i], i); ntrn[i] = 0; }
    sceGuDisable(GU_FOG);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);
    for (i = 0; i < MAXTEX; i++) { draw_verts(add[i], nadd[i], i); nadd[i] = 0; }
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

void r_rect(int x, int y, int w, int h, uint32_t col)
{
    SV *v;
    if (w <= 0 || h <= 0) return;
    if (nrect >= RECT_MAX) flush_rects();
    v = &rects[nrect * 2]; nrect++;
    v[0].color = col; v[0].x = (short)x; v[0].y = (short)y; v[0].z = 0;
    v[1].color = col; v[1].x = (short)(x + w); v[1].y = (short)(y + h); v[1].z = 0;
}

void r_end(void)
{
    ensure_started();
    flush_rects();
    sceGuFinish();          /* la PSP disegna in parallelo; si aspetta solo all'inizio del frame dopo */
}
