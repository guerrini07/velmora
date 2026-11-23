/* Punto d'ingresso PSP: input, ciclo di gioco, uscita col tasto HOME */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <string.h>
#include "game.h"
#include "render.h"

PSP_MODULE_INFO("Velmora", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);              /* usa tutta la RAM libera tranne 1 MB */
PSP_MAIN_THREAD_STACK_SIZE_KB(512);


static volatile int running = 1;

static int exit_cb(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; running = 0; return 0; }

static int cb_thread(SceSize args, void *argp)
{
    int cbid;
    (void)args; (void)argp;
    cbid = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int th = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* thread audio: chiede al gioco blocchi di suono e li manda alla PSP */
#define AUDIO_FRAMES 1024
static int16_t abuf[2][AUDIO_FRAMES * 2] __attribute__((aligned(64)));

static int audio_thread(SceSize args, void *argp)
{
    int ch, cur = 0;
    (void)args; (void)argp;
    ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, PSP_AUDIO_SAMPLE_ALIGN(AUDIO_FRAMES), PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    while (running) {
        game_audio_fill(abuf[cur], AUDIO_FRAMES);
        sceAudioOutputPannedBlocking(ch, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, abuf[cur]);
        cur ^= 1;
    }
    sceAudioChRelease(ch);
    return 0;
}

int main(void)
{
    unsigned prev = 0;

    setup_callbacks();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    r_init();
    game_loading();
    game_loading();   /* due volte: riempie entrambi i buffer dello schermo */

    game_init();
    {
        int th = sceKernelCreateThread("audio_thread", audio_thread, 0x12, 0x10000, THREAD_ATTR_USER | THREAD_ATTR_VFPU, 0);
        if (th >= 0) sceKernelStartThread(th, 0, 0);
    }

    {
        /* Logica a passo fisso (60 volte al secondo) separata dal disegno: se un frame e' lento,
         * il gioco recupera aggiornando piu' volte invece di andare al rallentatore. */
        const unsigned STEP = 16667;           /* microsecondi */
        unsigned last = sceKernelGetSystemTimeLow(), acc = STEP, fps_t = last, slow = 0, fast = 0;
        int frames = 0;
        unsigned pend = 0;                     /* tasti premuti non ancora consegnati al gioco */
        while (running) {
            SceCtrlData pad;
            Input in;
            unsigned h = 0, now, dt;
            int steps = 0;

            sceCtrlPeekBufferPositive(&pad, 1);    /* non blocca: legge subito lo stato attuale */
            if (pad.Buttons & PSP_CTRL_UP) h |= B_UP;
            if (pad.Buttons & PSP_CTRL_DOWN) h |= B_DOWN;
            if (pad.Buttons & PSP_CTRL_LEFT) h |= B_LEFT;
            if (pad.Buttons & PSP_CTRL_RIGHT) h |= B_RIGHT;
            if (pad.Buttons & PSP_CTRL_SQUARE) h |= B_SQUARE;
            if (pad.Buttons & PSP_CTRL_TRIANGLE) h |= B_TRIANGLE;
            if (pad.Buttons & PSP_CTRL_CROSS) h |= B_CROSS;
            if (pad.Buttons & PSP_CTRL_CIRCLE) h |= B_CIRCLE;
            if (pad.Buttons & PSP_CTRL_LTRIGGER) h |= B_L;
            if (pad.Buttons & PSP_CTRL_RTRIGGER) h |= B_R;
            if (pad.Buttons & PSP_CTRL_START) h |= B_START;
            if (pad.Buttons & PSP_CTRL_SELECT) h |= B_SELECT;
            pend |= h & ~prev;                 /* una pressione non va mai persa, anche se breve */
            prev = h;

            in.ax = (pad.Lx - 128) / 127.0f;
            in.ay = (pad.Ly - 128) / 127.0f;
            /* zona morta circolare con risposta riscalata: niente deriva, risposta piena ai bordi */
            {
                float m = in.ax * in.ax + in.ay * in.ay;
                if (m < 0.2f * 0.2f) { in.ax = in.ay = 0; }
                else {
                    float l = __builtin_sqrtf(m), k = (l - 0.2f) / 0.8f / l;
                    if (k * l > 1) k = 1 / l;
                    in.ax *= k; in.ay *= k;
                }
            }

            now = sceKernelGetSystemTimeLow();
            dt = now - last; last = now;
            if (dt > 100000) dt = 100000;
            acc += dt;
            while (acc >= STEP && steps < 4) {
                in.held = h | pend;
                in.pressed = pend;
                game_update(&in);
                pend = 0;
                acc -= STEP;
                steps++;
            }
            if (acc >= STEP) acc = 0;              /* troppo indietro: non inseguire all'infinito */
            game_render();

            /* misura gli fps e regola da sola la qualita' se la PSP non ce la fa */
            frames++;
            if (now - fps_t >= 1000000) {
                game_set_fps(frames);
                if (frames < 24) { slow++; fast = 0; } else if (frames >= 50) { fast++; slow = 0; } else { slow = fast = 0; }
                if (slow >= 2 && perf_lvl < 2) { perf_lvl++; slow = 0; }
                if (fast >= 6 && perf_lvl > 0) { perf_lvl--; fast = 0; }
                frames = 0; fps_t = now;
            }
        }
    }

    sceKernelExitGame();
    return 0;
}
