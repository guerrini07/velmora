/* Interfaccia di rendering: triangoli 3D con texture, colori per vertice e tre modi di fusione,
 * piu' rettangoli 2D per l'HUD. Implementata da render_gu.c (PSP) e render_soft.c (simulatore PC). */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, u, v; uint32_t c; } RV;   /* c = colore ABGR del vertice */

enum { RB_OPAQUE = 0, RB_ALPHA = 1, RB_ADD = 2 };

void r_init(void);
/* texture quadrate, contigue in memoria: count * size * size pixel ABGR */
void r_textures(const uint32_t *pixels, int count, int size);
void r_begin(uint32_t clear, V3 eye, V3 target, float fog_near, float fog_far, uint32_t fogcol);
/* il colore finale e' texture * colore del vertice; blend: RB_OPAQUE, RB_ALPHA, RB_ADD */
void r_tri(const RV *a, const RV *b, const RV *c, int tex, int blend);
void r_begin2d(void);
void r_rect(int x, int y, int w, int h, uint32_t col);
void r_end(void);

#endif
