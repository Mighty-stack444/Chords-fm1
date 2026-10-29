/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS app: the project (everything that is saved) and the app's entry points. Included by the unity
 * build after gfx.c (drawing helpers in scope) and by the host simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "../dsp/omni.h"

#ifndef OM_VERSION                       /* a release build passes its own (tools/build.py --release) */
#define OM_VERSION "DEV"
#endif
#define PROJ_MAGIC 0x44524843u           /* "CHRD" */
#define PROJ_FORMAT 1u

typedef struct {
    uint32_t magic, format;
    int16_t par[P_NPARAMS];              /* omni.h P_* */
    uint8_t hold;                        /* HOLD: chords and notes keep sounding after the key is let go */
    uint8_t layout;                      /* 0: chords on the white keys, notes on the black; 1: swapped */
    uint8_t arp_last;                    /* the arp mode ARP turns back on (1..5) */
    uint8_t leds;                        /* lights: 0 off, 1 keys and the buttons' glow, 2 keys only */
    uint8_t mel_set;                     /* 1: the melody's settings below are set (0.2 projects: 0) */
    uint8_t mel_density, mel_bars, mel_range;   /* 0..100; 1, 2, 4 bars (0..2); low, mid, high (0..2) */
    uint8_t mel_seed[2];                 /* the phrase's seed, low byte first */
    uint8_t rsv[10];                     /* room to grow: an older, shorter project loads (zeros here) */
} project_t;

extern project_t proj;

void project_defaults(void);
int project_load(void);                  /* 0 = loaded; else defaults are in place */
int project_save(void);                  /* 0 = saved and verified */
void project_apply(void);                /* every value to the engine (boot, load) */

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
int ui_dirty(void);
void ui_say(const char *a, const char *b);
