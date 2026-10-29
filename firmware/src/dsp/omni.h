/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS: the instrument. A port of the "Chord Generator" web app (Tone.js) to the FM-1.
 *
 *   SYNTH   12-voice polyphonic, the web app's twelve synthesized instruments: 2-operator FM
 *           (Tone.FMSynth: Piano, E.Piano, Pad, Pluck, Bell) and oscillator (Tone.Synth: Organ,
 *           Brass and Strings on detuned "fat" saws, Saw, Square, Triangle, Sine).
 *   BASS    a mono Tone.Synth under the chord: the root, one or two octaves down.
 *   ARP     up / down / up-down / random over the chord, 1..3 octaves, synced to the tempo.
 *   CHAIN   as the web app wires it: low-pass > high-pass > auto-filter > overdrive > bitcrush >
 *           waveshape > chorus > vibrato > tremolo > delay > reverb > compressor. The bass joins at
 *           the reverb.
 *
 * Threads: om_render() runs in the audio interrupt. Everything else posts commands (om_*), which the
 * render drains at the start of a block. The state marked (UI) is written by the render and only
 * read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define OM_SR 44100.0f
#define OM_NVOICE 12                   /* the web app's maxPolyphony */
#define OM_MAXCHORD 6                  /* 11ths */

/* ---- music theory (pure functions, shared with the UI) */
enum { NSCALES = 12, NINSTR = 12 };
extern const char *const OM_SCALE_NAME[NSCALES];
extern const char *const OM_INSTR_NAME[NINSTR];
extern const char *const OM_NOTE_NAME[12];             /* "C", "C#"... (the web app's sharps) */
extern const char *const OM_ROOT_NAME[12];             /* "C", "C#", "D", "Eb"... (its root menu) */
int om_scale_len(int scale);
int om_scale_step(int scale, int i);                   /* semitones over the root, i < len */
/* the chord on scale degree deg (0.., may run past the scale: the next octave), n notes stacked in
 * thirds of the scale, from octave oct (3 = C4 is 60): MIDI notes, ascending. Returns n. */
int om_chord_notes(int scale, int root, int oct, int deg, int n, uint8_t *out);
void om_chord_quality(const uint8_t *notes, int n, char *buf);   /* "m", "maj7", "m7b5", "(9)"... (16 B) */
void om_roman(int deg, int minor, char *buf);          /* "IV", "vi"... (8 B) */
int om_scale_note(int scale, int root, int oct, int i);   /* the i-th note of the scale from the root */

/* ---- parameters: value ranges in om_param_info() */
enum {
    P_VOICING, P_ATTACK, P_RELEASE, P_LEVEL,           /* PLAY */
    P_ARPMODE, P_ARPRATE, P_TEMPO, P_GATE,             /* ARP */
    P_BASSMODE, P_BASSTONE, P_BASSVOL, P_ARPOCT,       /* BASS (+ the arp's octaves) */
    P_REVERB, P_DELAY, P_CHORUS, P_TREMOLO,            /* EFFECTS */
    P_LOWPASS, P_HIGHPASS, P_AUTOFILT, P_VIBRATO,      /* FILTER */
    P_DRIVE, P_CRUSH, P_SHAPE, P_COMP,                 /* DRIVE */
    P_SCALE, P_ROOT, P_OCTAVE, P_INSTR,                /* the key and the sound (encoders) */
    P_TUNE, P_MIDI,                                    /* SETUP */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} om_param_t;
const om_param_t *om_param_info(int i);
void om_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

/* ---- commands (main loop -> render) */
enum { TAG_CHORD, TAG_MELODY, TAG_MIDI, NTAGS };   /* who a note belongs to */
void om_init(void);
void om_set(int param, int value);
void om_note(int note, int on, int tag);
void om_tag_off(int tag);                      /* release every note of a tag */
void om_arp(const uint8_t *notes, int n);      /* the arp plays these (ascending), from now; n 0 = stop */
void om_bass(int note);                        /* the bass plays note; < 0: let go */
void om_panic(void);                           /* everything quiet, now */
void om_poly(int n);                           /* the most voices at once (4..12): the CPU guard */

/* the melody generator (Arp mode Melody): a phrase of slots, one per arp step, each a rest (0), a hold
 * of the note before (MEL_HOLD) or a note (MEL_NOTE | scale steps over the chord's root). The UI
 * writes the phrase; the render plays it over the chord at the arp's rate, in the chord's key. */
#define ARP_MELODY 5
#define MEL_NOTE 0x80u
#define MEL_HOLD 0x40u
#define MEL_MAX 128
extern volatile uint8_t om_mel[MEL_MAX], om_mel_len;
void om_melody(int on, int deg, int oct);      /* on: over the chord on scale degree deg, from octave oct */
extern volatile uint8_t om_mel_note;           /* the melody's note sounding, 0 none (UI) */
extern volatile uint8_t om_mel_pos;            /* the slot playing (UI) */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void om_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* ---- state for the screen (UI) */
extern volatile uint32_t om_sounding[4];       /* notes sounding (gate on), bit per MIDI note */
extern volatile float om_level;                /* output level, 0..1, smoothed */
extern volatile uint8_t om_arp_on, om_arp_note;   /* the arp running, the note it played last */
extern volatile uint8_t om_bass_note;          /* 0: none */
extern volatile uint8_t om_nvoices;            /* voices busy (sounding or releasing) */
extern volatile uint8_t om_poly_max;           /* the voice limit now */
