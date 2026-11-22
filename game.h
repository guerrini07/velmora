#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define SW 480
#define SH 272

enum {
    B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8,
    B_SQUARE = 16, B_TRIANGLE = 32, B_CROSS = 64, B_CIRCLE = 128,
    B_L = 256, B_R = 512, B_START = 1024, B_SELECT = 2048
};

typedef struct {
    float ax, ay;        /* levetta analogica -1..1 */
    unsigned held;       /* tasti tenuti premuti */
    unsigned pressed;    /* tasti appena premuti in questo frame */
} Input;

void game_init(void);
/* schermata di caricamento (prima di game_init) */
void game_loading(void);
void game_update(const Input *in);
/* disegna il frame tramite render.h */
void game_render(void);
/* riempie un blocco audio stereo a 44100 Hz (chiamata dal thread audio) */
void game_audio_fill(int16_t *stereo, int frames);

#ifdef GAME_DEBUG
void game_debug_teleport(int where);
int  game_debug_state(int what);
#endif

#endif
