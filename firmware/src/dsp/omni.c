/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS: the instrument (omni.h). One compilation unit, -O2.
 *
 * Ported from the "Chord Generator" web app, which runs on Tone.js 14.8. Where this follows Tone.js
 * it says so; the numbers (envelope time constants, fat oscillator levels, the distortion curve,
 * the bitcrusher's steps, the instruments' presets) are the web app's and Tone's. The reverb is
 * FoMni's Dattorro plate standing in for Tone.Freeverb. */
#include "omni.h"
#include "fastmath.h"

#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

/* ============================================================== theory === */
const char *const OM_NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *const OM_ROOT_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const OM_SCALE_NAME[NSCALES] = {"Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                            "Harmonic Min", "Melodic Min", "Pent. Major", "Pent. Minor", "Blues",
                                            "Whole Tone"};
static const uint8_t SCALE_LEN[NSCALES] = {7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6};
static const uint8_t SCALE[NSCALES][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11},
    {0, 2, 4, 7, 9},        {0, 3, 5, 7, 10},       {0, 3, 5, 6, 7, 10},    {0, 2, 4, 6, 8, 10}};

int om_scale_len(int s) { return SCALE_LEN[(unsigned)s < NSCALES ? s : 0]; }
int om_scale_step(int s, int i)
{
    s = (unsigned)s < NSCALES ? s : 0;
    return SCALE[s][(unsigned)i < SCALE_LEN[s] ? i : 0];
}

static int clamp_note(int n) { return n < 0 ? 0 : n > 127 ? 127 : n; }

int om_scale_note(int s, int root, int oct, int i)
{
    int len = om_scale_len(s);
    return clamp_note(root + om_scale_step(s, i % len) + (oct + 1 + i / len) * 12);
}

/* the web app's getNotes(): every other scale step from the degree */
int om_chord_notes(int s, int root, int oct, int deg, int n, uint8_t *out)
{
    int i;
    if (n > OM_MAXCHORD)
        n = OM_MAXCHORD;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t)om_scale_note(s, root, oct, deg + 2 * i);
    return n;
}

static void scat(char *b, const char *s)
{
    while (*b)
        b++;
    while ((*b++ = *s++))
        ;
}

/* the web app's cQ(): the chord's name from its intervals */
void om_chord_quality(const uint8_t *mn, int n, char *q)
{
    int it[OM_MAXCHORD], k = 0, i, s = -1;
    q[0] = 0;
    if (n < 2)
        return;
    if (n < 3) {
        int iv = mn[1] - mn[0];
        scat(q, iv == 7 ? "5" : iv == 3 ? "m" : "");
        return;
    }
    for (i = 1; i < n; i++)
        it[k++] = mn[i] - mn[0];
    {
        int has_v[32] = {0}, im, iD, iA, i4, i2;
        for (i = 0; i < k; i++)
            if (it[i] >= 0 && it[i] < 32)
                has_v[it[i]] = 1;
        im = has_v[3] && has_v[7];
        iD = has_v[3] && has_v[6];
        iA = has_v[4] && has_v[8];
        i4 = has_v[5] && has_v[7];
        i2 = has_v[2] && has_v[7];
        scat(q, iD ? "dim" : iA ? "aug" : im ? "m" : i4 ? "sus4" : i2 ? "sus2" : "");
        if (n >= 4) {
            for (i = 0; i < k; i++)
                if (it[i] >= 9 && it[i] <= 11) {
                    s = it[i];
                    break;
                }
            if (s == 11) {
                if (!iD)
                    scat(q, "maj7");
            } else if (s == 10) {
                if (iD) {
                    q[0] = 0;
                    scat(q, "m7b5");
                } else {
                    scat(q, "7");
                }
            } else if (s == 9 && iD) {
                q[0] = 0;
                scat(q, "dim7");
            }
        }
        if (n >= 5) {
            if (has_v[14] || has_v[2])
                scat(q, "(9)");
            else if (has_v[13] || has_v[1])
                scat(q, "(b9)");
        }
        if (n >= 6 && (has_v[17] || has_v[5]))
            scat(q, "(11)");
    }
}

void om_roman(int deg, int minor, char *b)
{
    static const char *const ROM[12] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};
    const char *r = ROM[(unsigned)deg < 12u ? deg : 0];
    while (*r) {
        *b++ = minor ? (char)(*r + ('a' - 'A')) : *r;
        r++;
    }
    *b = 0;
}

/* ========================================================== parameters === */
static const om_param_t PARAMS[P_NPARAMS] = {
    {"Voicing", 0, 4, 1},   {"Attack", 0, 100, 3},  {"Release", 1, 40, 6},  {"Level", 0, 100, 80},
    {"Arp", 0, 4, 0},       {"Rate", 0, 5, 1},      {"Tempo", 40, 220, 120}, {"Gate", 10, 100, 70},
    {"Bass", 0, 2, 0},      {"Bass tone", 0, 3, 0}, {"Bass vol", 0, 100, 80}, {"Arp oct", 1, 3, 1},
    {"Reverb", 0, 100, 25}, {"Delay", 0, 100, 0},   {"Chorus", 0, 100, 0},  {"Tremolo", 0, 100, 0},
    {"Low-pass", 0, 6, 0},  {"High-pass", 0, 5, 0}, {"Auto-filt", 0, 4, 0}, {"Vibrato", 0, 100, 0},
    {"Drive", 0, 4, 0},     {"Crush", 0, 4, 0},     {"Shape", 0, 3, 0},     {"Comp", 0, 4, 0},
    {"Scale", 0, NSCALES - 1, 0}, {"Root", 0, 11, 0}, {"Octave", 2, 5, 3},  {"Sound", 0, NINSTR - 1, 0},
    {"Tune", -50, 50, 0},   {"MIDI out", 0, 1, 1},
};
const om_param_t *om_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

static const char *const VOICING_NAME[5] = {"Dyads", "Triads", "7ths", "9ths", "11ths"};
static const char *const ARP_NAME[5] = {"Off", "Up", "Down", "Up-Dn", "Random"};
static const char *const RATE_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const BASS_NAME[3] = {"Off", "-1 Oct", "-2 Oct"};
static const char *const WAVE_NAME[4] = {"Sine", "Tri", "Saw", "Square"};
static const char *const LP_NAME[7] = {"Off", "Open", "Bright", "Warm", "Muted", "Dark", "Sub"};
static const char *const HP_NAME[6] = {"Off", "Rumble", "Thin", "Narrow", "Tinny", "Radio"};
static const char *const AF_NAME[5] = {"Off", "Slow", "Medium", "Fast", "Wobble"};
static const char *const OD_NAME[5] = {"Off", "Light", "Medium", "Heavy", "Crushed"};
static const char *const BC_NAME[5] = {"Off", "Subtle", "Lo-Fi", "Crunchy", "Wrecked"};
static const char *const WS_NAME[4] = {"Off", "Warm", "Gritty", "Harsh"};
static const char *const CM_NAME[5] = {"Off", "Light", "Medium", "Heavy", "Limit"};
const char *const OM_INSTR_NAME[NINSTR] = {"Piano", "E. Piano", "Organ", "Brass", "Strings", "Pad",
                                           "Pluck", "Bell", "Saw", "Square", "Triangle", "Sine"};

static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void copy_s(char *b, const char *s)
{
    int k = 0;
    while (s[k] && k < 11) {
        b[k] = s[k];
        k++;
    }
    b[k] = 0;
}

void om_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    const char *s = 0;
    switch (i) {
    case P_VOICING: s = VOICING_NAME[v % 5]; break;
    case P_ARPMODE: s = ARP_NAME[v % 5]; break;
    case P_ARPRATE: s = RATE_NAME[v % 6]; break;
    case P_BASSMODE: s = BASS_NAME[v % 3]; break;
    case P_BASSTONE: s = WAVE_NAME[v % 4]; break;
    case P_LOWPASS: s = LP_NAME[v % 7]; break;
    case P_HIGHPASS: s = HP_NAME[v % 6]; break;
    case P_AUTOFILT: s = AF_NAME[v % 5]; break;
    case P_DRIVE: s = OD_NAME[v % 5]; break;
    case P_CRUSH: s = BC_NAME[v % 5]; break;
    case P_SHAPE: s = WS_NAME[v % 4]; break;
    case P_COMP: s = CM_NAME[v % 5]; break;
    case P_SCALE: s = OM_SCALE_NAME[v % NSCALES]; break;
    case P_ROOT: s = OM_ROOT_NAME[v % 12]; break;
    case P_INSTR: s = OM_INSTR_NAME[v % NINSTR]; break;
    case P_MIDI: s = ONOFF[v ? 1 : 0]; break;
    default: break;
    }
    if (s) {
        copy_s(b, s);
        return;
    }
    if (i == P_ATTACK || i == P_RELEASE) {        /* seconds, as the web app shows them: 0.03, 0.60 */
        int cs = i == P_ATTACK ? (v < 1 ? 1 : v) : v * 10;   /* centiseconds (Attack 0 is 0.005: "0.01") */
        b[0] = (char)('0' + cs / 100);
        b[1] = '.';
        b[2] = (char)('0' + cs / 10 % 10);
        b[3] = (char)('0' + cs % 10);
        b[4] = 0;
        return;
    }
    if (i == P_TUNE && v > 0)
        *b++ = '+';
    itoa_s(v, b);
    if (i == P_GATE || i == P_REVERB || i == P_DELAY || i == P_CHORUS || i == P_TREMOLO || i == P_VIBRATO ||
        i == P_LEVEL || i == P_BASSVOL) {
        while (*b)
            b++;
        b[0] = '%';
        b[1] = 0;
    }
}

/* ========================================================= instruments === */
/* the web app's IN table. kind: K_FM is Tone.FMSynth (carrier sine), K_OSC Tone.Synth; Organ, Brass
 * and Strings have engines of their own here (in the web app all three were the same detuned saws).
 * Attack and release come from the ATK / REL controls; an instrument's attack never goes under its
 * own floor (att), so Strings always swell and Brass always speaks. */
enum { W_SINE, W_TRI, W_SAW, W_SQUARE, W_TRI4 };
enum { K_OSC, K_FM, K_ORGAN, K_BRASS, K_STRINGS };
typedef struct {
    uint8_t fm, wave, count, mwave;               /* fm: the kind, K_* */
    float spread, harm, index, vol_db;
    float d, s;                                   /* the carrier envelope's decay, sustain */
    float ma, md, ms, mr;                         /* the modulation envelope */
    float att;                                    /* the shortest attack, seconds */
} inst_t;
static const inst_t INSTR[NINSTR] = {
    /* Piano */    {1, W_SINE, 1, W_SQUARE, 0, 3.01f, 1.5f, -8, 0.6f, 0.15f, 0.002f, 0.4f, 0.1f, 0.8f, 0},
    /* E.Piano */  {1, W_SINE, 1, W_SINE, 0, 2.5f, 4.0f, -6, 1.2f, 0.05f, 0.001f, 0.8f, 0.0f, 1.0f, 0},
    /* Organ */    {K_ORGAN, W_SINE, 1, 0, 0, 0, 0, -11, 0.1f, 1.0f, 0, 0, 0, 0, 0},
    /* Brass */    {K_BRASS, W_SAW, 2, 0, 12, 0, 0, -6, 0.3f, 0.8f, 0, 0, 0, 0, 0.05f},
    /* Strings */  {K_STRINGS, W_SAW, 4, 0, 22, 0, 0, -6, 0.4f, 0.9f, 0, 0, 0, 0, 0.3f},
    /* Pad */      {1, W_SINE, 1, W_TRI, 0, 1.5f, 2.0f, -8, 0.5f, 0.8f, 0.3f, 0.4f, 0.6f, 1.5f, 0},
    /* Pluck */    {1, W_SINE, 1, W_SQUARE, 0, 5.0f, 8.0f, -6, 0.4f, 0.0f, 0.001f, 0.3f, 0.0f, 0.2f, 0},
    /* Bell */     {1, W_SINE, 1, W_SINE, 0, 5.07f, 12.0f, -10, 1.8f, 0.0f, 0.001f, 1.2f, 0.0f, 1.5f, 0},
    /* Saw */      {0, W_SAW, 1, 0, 0, 0, 0, -12, 0.15f, 0.5f, 0, 0, 0, 0, 0},
    /* Square */   {0, W_SQUARE, 1, 0, 0, 0, 0, -14, 0.15f, 0.5f, 0, 0, 0, 0, 0},
    /* Triangle */ {0, W_TRI4, 1, 0, 0, 0, 0, -5, 0.15f, 0.6f, 0, 0, 0, 0, 0},
    /* Sine */     {0, W_SINE, 1, 0, 0, 0, 0, -3, 0.1f, 0.7f, 0, 0, 0, 0, 0},
};

/* ============================================================ commands === */
enum { C_SET, C_NOTE, C_TAGOFF, C_ARPCLR, C_ARPADD, C_ARPGO, C_BASS, C_PANIC, C_POLY };
#define QN 128u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void om_set(int p, int v) { post(C_SET, p, v); }
void om_note(int n, int on, int tag) { post(C_NOTE, n, (on ? 1 : 0) | tag << 1); }
void om_tag_off(int tag) { post(C_TAGOFF, tag, 0); }
void om_bass(int n) { post(C_BASS, n < 0 ? 255 : n, 0); }
void om_panic(void) { post(C_PANIC, 0, 0); }
void om_poly(int n) { post(C_POLY, n, 0); }
void om_arp(const uint8_t *notes, int n)
{
    int i;
    post(C_ARPCLR, 0, 0);
    for (i = 0; i < n && i < OM_MAXCHORD; i++)
        post(C_ARPADD, notes[i], 0);
    post(C_ARPGO, n > 0, 0);
}

/* =============================================================== state === */
volatile uint32_t om_sounding[4];
volatile float om_level;
volatile uint8_t om_arp_on, om_arp_note, om_bass_note, om_nvoices, om_poly_max = OM_NVOICE;

static int16_t par[P_NPARAMS];
static float tunefac = 1.0f;

#define TBL 1024
static float sintab[TBL + 1];
static inline float tsin(float ph)                /* ph in turns, [0, 1) */
{
    float x = ph * (float)TBL;
    int i = (int)x;
    float f = x - (float)i;
    return sintab[i] + (sintab[i + 1] - sintab[i]) * f;
}
static inline float wrap1(float p) { return p >= 1.0f ? p - (float)(int)p : p; }

static inline float blep(float t, float dt)       /* polyBLEP: band-limits the saw and square edges */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float osc(int w, float t, float dt)
{
    switch (w) {
    case W_SAW: return 2.0f * t - 1.0f - blep(t, dt);
    case W_SQUARE: {
        float t2 = t + 0.5f, v = t < 0.5f ? 1.0f : -1.0f;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        return v + blep(t, dt) - blep(t2, dt);
    }
    case W_TRI: {                                  /* Tone's triangle starts at 0 going up, as its sine */
        float u = t + 0.25f;
        if (u >= 1.0f)
            u -= 1.0f;
        return 1.0f - 4.0f * fm_fabsf(u - 0.5f);
    }
    case W_TRI4: {                                 /* the Triangle sound: a 4-bit, 32-step triangle (as the
                                                    * game consoles' triangle channel), its steps give it
                                                    * the edge a smooth triangle lacks on a small speaker */
        float u = t + 0.25f, x;
        int lv;
        if (u >= 1.0f)
            u -= 1.0f;
        x = 1.0f - 4.0f * fm_fabsf(u - 0.5f);
        lv = (int)((x + 1.0f) * 7.999f);
        return ((float)lv - 7.5f) * (1.0f / 7.5f);
    }
    default: return tsin(t);
    }
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        om_midi_out(st, d1, d2);
}

/* ---- envelopes. Tone.Envelope: a linear attack; decay and release are setTargetAtTime curves with
 * the time constant ln(t + 1) / ln(200) (Param.exponentialApproachValueAtTime). */
static float tc_coef(float t)
{
    float tc = fm_logf(t + 1.0f) * (1.0f / 5.2983174f);   /* / ln 200 */
    if (tc < 1e-4f)
        tc = 1e-4f;
    return 1.0f - fm_expf(-1.0f / (tc * OM_SR));
}
enum { E_OFF, E_ATT, E_DEC, E_REL };

/* ---- voices */
typedef struct {
    uint8_t st, mst, note, tag;
    float env, menv;
    float ph[4], mph, dt;
    float det[4];                                 /* the fat oscillator's detune ratios */
    float flo, fbd, fenv, fc;                     /* Brass / Strings: the filter, its envelope, its coefficient */
    float perc, scoop, vph, vamt;                 /* Organ's percussion; Brass's pitch scoop; Strings' vibrato */
    uint32_t gate_left;                           /* arp notes: samples until their release */
    uint32_t age;
} voice_t;
static voice_t vc[OM_NVOICE];
static uint32_t age_ctr;
static const inst_t *ins = &INSTR[0];
static float k_att, k_dec, k_rel, k_matt, k_mdec, k_mrel, sus, msus, ins_gain, fat_gain;

static float note_dt(int n) { return 440.0f * tunefac * fm_exp2f((float)(n - 69) * (1.0f / 12.0f)) / OM_SR; }

static void env_coefs(void)
{
    float at = par[P_ATTACK] < 1 ? 0.005f : (float)par[P_ATTACK] * 0.01f;
    if (at < ins->att)
        at = ins->att;
    k_att = 1.0f / (at * OM_SR);
    k_rel = tc_coef((float)par[P_RELEASE] * 0.1f);
    k_dec = tc_coef(ins->d);
    sus = ins->s;
    if (ins->fm == K_FM) {
        k_matt = 1.0f / (fm_maxf(ins->ma, 0.001f) * OM_SR);
        k_mdec = tc_coef(ins->md);
        k_mrel = tc_coef(ins->mr);
        msus = ins->ms;
    }
}

static void set_instr(int i)
{
    int v;
    ins = &INSTR[(unsigned)i < NINSTR ? i : 0];
    ins_gain = fm_db2lin(ins->vol_db);
    fat_gain = ins->count > 1 ? fm_db2lin(-6.0f - (float)ins->count * 1.1f) : 1.0f;   /* Tone.FatOscillator */
    for (v = 0; v < OM_NVOICE; v++)             /* the old sound goes quickly: the web app swaps synths */
        if (vc[v].st != E_OFF && vc[v].tag != 255) {
            if (vc[v].st != E_REL)
                midi(0x80, vc[v].note, 0);
            vc[v].st = E_REL;
            vc[v].tag = 254;                    /* a fast fade (render_voices) */
        }
    env_coefs();
}

static void voice_start(voice_t *v, int n, int tag)
{
    int k;
    if (v->st != E_OFF && v->st != E_REL)
        midi(0x80, v->note, 0);
    v->note = (uint8_t)n;
    v->tag = (uint8_t)tag;
    v->dt = note_dt(n);
    if (v->st == E_OFF) {
        v->env = v->menv = 0.0f;
        v->mph = 0.0f;
        for (k = 0; k < 4; k++)
            v->ph[k] = (float)k * 0.23f;        /* the fat saws start apart */
        v->flo = v->fbd = v->fenv = 0.0f;
        v->vph = (float)(n % 7) * 0.13f;        /* each string its own vibrato phase */
    }
    v->perc = 1.0f;
    v->scoop = 1.0f;
    v->vamt = 0.0f;
    v->st = E_ATT;
    v->mst = E_ATT;
    v->gate_left = 0;
    v->age = ++age_ctr;
    for (k = 0; k < ins->count; k++) {          /* Tone.FatOscillator: -spread/2 .. +spread/2 cents */
        float c = ins->count > 1 ? -ins->spread * 0.5f + ins->spread * (float)k / (float)(ins->count - 1) : 0.0f;
        v->det[k] = fm_exp2f(c * (1.0f / 1200.0f));
    }
    midi(0x90, (uint32_t)n, 100);
}

static void voice_release(voice_t *v)
{
    if (v->st == E_OFF || v->st == E_REL)
        return;
    v->st = E_REL;
    v->mst = E_REL;
    midi(0x80, v->note, 0);
}

static voice_t *find(int n, int tag)
{
    int i;
    for (i = 0; i < OM_NVOICE; i++)
        if (vc[i].st != E_OFF && vc[i].st != E_REL && vc[i].note == n && vc[i].tag == tag)
            return &vc[i];
    return 0;
}

static voice_t *alloc(void)
{
    int i, best = -1;
    float lo = 2.0f;
    uint32_t oldest = 0xFFFFFFFFu;
    int busy = 0;
    for (i = 0; i < OM_NVOICE; i++)
        busy += vc[i].st != E_OFF;
    if (busy < om_poly_max)
        for (i = 0; i < OM_NVOICE; i++)
            if (vc[i].st == E_OFF)
                return &vc[i];
    for (i = 0; i < OM_NVOICE; i++)             /* steal: the quietest releasing voice */
        if (vc[i].st == E_REL && vc[i].env < lo) {
            lo = vc[i].env;
            best = i;
        }
    if (best >= 0)
        return &vc[best];
    for (i = 0; i < OM_NVOICE; i++)             /* else the oldest */
        if (vc[i].age < oldest) {
            oldest = vc[i].age;
            best = i;
        }
    return &vc[best];
}

static void note_on(int n, int tag)
{
    voice_t *v = find(n, tag);
    voice_start(v ? v : alloc(), n, tag);
}
static void note_off(int n, int tag)
{
    voice_t *v = find(n, tag);
    if (v)
        voice_release(v);
}
static void tag_off(int tag)
{
    int i;
    for (i = 0; i < OM_NVOICE; i++)
        if (vc[i].tag == tag)
            voice_release(&vc[i]);
}

/* ---- bass: a mono Tone.Synth, envelope 0.01 / 0.2 / 0.8 / 0.5, volume -20 + vol * 0.2 dB */
static struct {
    uint8_t st, note;
    float env, ph, dt;
} bs;
static float bk_att, bk_dec, bk_rel, bass_gain;

static void bass_cmd(int n)
{
    if (n == 255) {
        if (bs.st != E_OFF && bs.st != E_REL) {
            bs.st = E_REL;
            midi(0x81, bs.note, 0);
        }
        om_bass_note = 0;
        return;
    }
    if (bs.st != E_OFF && bs.st != E_REL)
        midi(0x81, bs.note, 0);
    if (bs.st == E_OFF) {
        bs.env = 0.0f;
        bs.ph = 0.0f;
    }
    bs.note = (uint8_t)n;
    bs.dt = note_dt(n);
    bs.st = E_ATT;
    om_bass_note = (uint8_t)n;
    midi(0x91, (uint32_t)n, 100);
}

/* ---- the arpeggiator: Tone.Loop at the rate; each step lets the last note go and plays the next for
 * a gate's share of the step (triggerAttackRelease) */
#define TAG_ARP 3
static uint8_t arp_src[OM_MAXCHORD], arp_pend[OM_MAXCHORD], arp_n, arp_pn;
static uint8_t arp_seq[OM_MAXCHORD * 3 * 2];
static int arp_len, arp_pos, arp_last = -1;
static float arp_left;                           /* samples to the next step */
static uint32_t rng = 0x12345678u;

static void arp_build(void)                     /* the web app's buildArpSequence() */
{
    int o, i, octs = par[P_ARPOCT], m = par[P_ARPMODE], n = 0;
    uint8_t ex[OM_MAXCHORD * 3];
    for (o = 0; o < octs; o++)
        for (i = 0; i < arp_n; i++)
            ex[n++] = (uint8_t)clamp_note(arp_src[i] + 12 * o);
    arp_len = 0;
    if (m == 2) {                                /* down */
        for (i = n - 1; i >= 0; i--)
            arp_seq[arp_len++] = ex[i];
    } else {
        for (i = 0; i < n; i++)
            arp_seq[arp_len++] = ex[i];
        if (m == 3)                              /* up-down: back down without repeating the ends */
            for (i = n - 2; i >= 1; i--)
                arp_seq[arp_len++] = ex[i];
    }
}

static float step_samples(void)
{
    static const float BEATS[6] = {1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
    return OM_SR * 60.0f / (float)par[P_TEMPO] * BEATS[par[P_ARPRATE] % 6];
}

static void arp_stop(void)
{
    om_arp_on = 0;
    arp_n = 0;
    arp_len = 0;
    arp_last = -1;
    tag_off(TAG_ARP);
}

static void arp_step(void)
{
    int idx, note;
    voice_t *v;
    if (!arp_len)
        return;
    if (par[P_ARPMODE] == 4) {
        rng = rng * 1664525u + 1013904223u;
        idx = (int)((rng >> 16) % (uint32_t)arp_len);
    } else {
        idx = arp_pos % arp_len;
    }
    note = arp_seq[idx];
    if (arp_last >= 0)
        note_off(arp_last, TAG_ARP);
    note_on(note, TAG_ARP);
    v = find(note, TAG_ARP);
    if (v) {
        uint32_t g = (uint32_t)(step_samples() * (float)par[P_GATE] * 0.01f);
        v->gate_left = g ? g : 1u;
    }
    arp_last = note;
    om_arp_note = (uint8_t)note;
    arp_pos++;
}

/* ================================================================ FX === */
/* ---- biquads (RBJ), for the fixed filters (24 dB: two in a row, Q 1 each, as Tone.Filter -24) */
typedef struct {
    float b0, b1, b2, a1, a2, z1, z2;
} bq_t;
static void bq_set(bq_t *f, int hp, float hz, float qv)
{
    float w = FM_TWO_PI * fm_minf(hz, 20000.0f) / OM_SR, cw = fm_cosf(w), sw = fm_sinf(w), al = sw / (2.0f * qv),
          a0 = 1.0f + al, z1 = f->z1, z2 = f->z2;
    if (hp) {
        f->b0 = (1.0f + cw) * 0.5f / a0;
        f->b1 = -(1.0f + cw) / a0;
    } else {
        f->b0 = (1.0f - cw) * 0.5f / a0;
        f->b1 = (1.0f - cw) / a0;
    }
    f->b2 = f->b0;
    f->a1 = -2.0f * cw / a0;
    f->a2 = (1.0f - al) / a0;
    f->z1 = z1;
    f->z2 = z2;
}
static inline float bq(bq_t *f, float x)          /* transposed direct form II */
{
    float y = f->b0 * x + f->z1;
    f->z1 = fm_flush(f->b1 * x - f->a1 * y + f->z2);
    f->z2 = fm_flush(f->b2 * x - f->a2 * y);
    return y;
}
static bq_t lp1, lp2, hp1, hp2, af;
static uint8_t lp_on, hp_on, af_on;
static float af_rate, af_ph;

/* ---- distortion (Tone.Distortion's curve), bitcrusher, Chebyshev */
static float od_k;
static uint8_t od_on, bc_on, ws_on;
static float bc_step, bc_inv;
static int ws_order;
static float dc_x, dc_y;                         /* after the waveshaper: Chebyshev's even orders add DC */

/* ---- chorus (Tone.Chorus 1.5 Hz, 3.5 ms, depth 0.7, spread 180), tremolo (4 Hz, depth 1, spread
 * 180), vibrato (5 Hz, at the oscillators), delay (8th note, feedback 0.3) */
#define CH_N 512
static float ch_buf[CH_N] OM_POOL;
static int ch_i;
static float ch_ph, ch_dry = 1.0f, ch_wet, tr_ph, tr_dry = 1.0f, tr_wet, vib_depth, vib_ph;
#define DL_N 33075                                /* 0.75 s: an 8th note at 40 BPM */
static int16_t dl_buf[DL_N] OM_POOL;
static int dl_i;
static float dl_len, dl_dry = 1.0f, dl_wet, rv_dry = 1.0f, rv_wet;
static float lvl_gain = 1.0f, out_lvl;

/* ---- compressor: a DynamicsCompressorNode (knee 30 dB, its automatic makeup gain), attack 3 ms,
 * release 250 ms */
static uint8_t cm_on;
static float cm_thr, cm_ratio, cm_env, cm_gain = 1.0f, cm_makeup = 1.0f, cm_ka, cm_kr;
static float cm_curve(float db)                  /* gain change (dB) at a level (dB) */
{
    const float knee = 30.0f;
    float over = db - cm_thr, slope = 1.0f / cm_ratio - 1.0f;
    if (over <= -knee * 0.5f)
        return 0.0f;
    if (over < knee * 0.5f) {
        float x = over + knee * 0.5f;
        return slope * x * x / (2.0f * knee);
    }
    return slope * over;
}

/* ---- reverb: Dattorro's plate (FoMni's), standing in for Tone.Freeverb (room 0.75, damping 3 kHz) */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 442
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define PL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[PL_TOTAL] OM_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, lfo_s, lfo_c = 1.0f;
static const float PL_DECAY = 0.76f, PL_DD2 = 0.5f, PL_DAMP = 0.42f;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.000128f * lfo_c, c = lfo_c - 0.000128f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + PL_DECAY * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += PL_DAMP * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * PL_DECAY, PL_DD2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + PL_DECAY * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += PL_DAMP * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * PL_DECAY, PL_DD2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}
static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < PL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* Tone's wet / dry: an equal-power crossfade */
static void xfade(int pct, float *dry, float *wet)
{
    float a = (float)pct * 0.01f * (FM_PI * 0.5f);
    *dry = fm_cosf(a);
    *wet = fm_sinf(a);
}

/* ========================================================= parameters === */
static void apply(int p, int v)
{
    const om_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_ATTACK: case P_RELEASE: env_coefs(); break;
    case P_LEVEL: lvl_gain = (float)v * (1.0f / 80.0f); break;
    case P_INSTR: set_instr(v); break;
    case P_ARPMODE: case P_ARPOCT:
        if (arp_n) {
            arp_build();
            if (par[P_ARPMODE] == 0)
                arp_stop();
        }
        break;
    case P_BASSVOL: bass_gain = fm_db2lin(-20.0f + (float)v * 0.2f); break;
    case P_TEMPO: dl_len = fm_minf(OM_SR * 30.0f / (float)v, (float)(DL_N - 2)); break;
    case P_REVERB: xfade(v, &rv_dry, &rv_wet); break;
    case P_DELAY: xfade(v, &dl_dry, &dl_wet); break;
    case P_CHORUS: xfade(v, &ch_dry, &ch_wet); break;
    case P_TREMOLO: xfade(v, &tr_dry, &tr_wet); break;
    case P_VIBRATO: vib_depth = (float)v * 0.01f * 0.0785f; break;   /* Tone.Vibrato: 5 ms max delay at 5 Hz */
    case P_LOWPASS: {
        static const float HZ[7] = {20000, 20000, 8000, 4000, 2000, 800, 400};
        lp_on = v >= 2;
        bq_set(&lp1, 0, HZ[v], 1.0f);
        bq_set(&lp2, 0, HZ[v], 1.0f);
        break;
    }
    case P_HIGHPASS: {
        static const float HZ[6] = {1, 60, 150, 400, 800, 2000};
        hp_on = v >= 1;
        bq_set(&hp1, 1, HZ[v], 1.0f);
        bq_set(&hp2, 1, HZ[v], 1.0f);
        break;
    }
    case P_AUTOFILT: {
        static const float HZ[5] = {0, 0.5f, 2, 6, 12};
        af_on = v >= 1;
        af_rate = HZ[v] / OM_SR;
        break;
    }
    case P_DRIVE: {
        static const float AMT[5] = {0, 0.2f, 0.5f, 0.8f, 1.0f};
        od_on = v >= 1;
        od_k = AMT[v] * 100.0f;
        break;
    }
    case P_CRUSH: {
        static const int BITS[5] = {16, 8, 6, 4, 2};
        bc_on = v >= 1;
        bc_step = fm_exp2f(-(float)(BITS[v] - 1));   /* Tone.BitCrusher: 0.5 ^ (bits - 1) */
        bc_inv = 1.0f / bc_step;
        break;
    }
    case P_SHAPE: {
        static const int ORD[4] = {1, 2, 20, 50};
        ws_on = v >= 1;
        ws_order = ORD[v];
        break;
    }
    case P_COMP: {
        static const float THR[5] = {0, -20, -24, -30, -10}, RAT[5] = {1, 2, 4, 8, 20};
        cm_on = v >= 1;
        cm_thr = THR[v];
        cm_ratio = RAT[v];
        cm_makeup = cm_on ? fm_db2lin(-0.6f * cm_curve(0.0f)) : 1.0f;
        break;
    }
    case P_TUNE: {
        int i;
        tunefac = fm_exp2f((float)v * (1.0f / 1200.0f));
        for (i = 0; i < OM_NVOICE; i++)
            vc[i].dt = note_dt(vc[i].note);
        bs.dt = note_dt(bs.note);
        break;
    }
    case P_MIDI:
        if (!v) {
            om_midi_out(0xB0, 123, 0);
            om_midi_out(0xB1, 123, 0);
        }
        break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < OM_NVOICE; i++)
        vc[i].st = E_OFF;
    bs.st = E_OFF;
    arp_n = arp_pn = 0;
    arp_len = 0;
    arp_last = -1;
    om_arp_on = 0;
    om_bass_note = 0;
    om_midi_out(0xB0, 123, 0);
    om_midi_out(0xB1, 123, 0);
}

void om_init(void)
{
    int i;
    for (i = 0; i <= TBL; i++)
        sintab[i] = fm_sinf((float)i * (FM_TWO_PI / (float)TBL));
    plate_init();
    for (i = 0; i < CH_N; i++)
        ch_buf[i] = 0.0f;
    for (i = 0; i < DL_N; i++)
        dl_buf[i] = 0;
    q_r = q_w = 0;
    for (i = 0; i < OM_NVOICE; i++)
        vc[i].st = E_OFF;
    bk_att = 1.0f / (0.01f * OM_SR);
    bk_dec = tc_coef(0.2f);
    bk_rel = tc_coef(0.5f);
    cm_ka = 1.0f - fm_expf(-1.0f / (0.003f * OM_SR));
    cm_kr = 1.0f - fm_expf(-1.0f / (0.25f * OM_SR));
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_NOTE:
            if (b & 1)
                note_on(a, b >> 1);
            else
                note_off(a, b >> 1);
            break;
        case C_TAGOFF: tag_off(a); break;
        case C_ARPCLR: arp_pn = 0; break;
        case C_ARPADD:
            if (arp_pn < OM_MAXCHORD)
                arp_pend[arp_pn++] = (uint8_t)a;
            break;
        case C_ARPGO: {
            int i;
            if (!a || !arp_pn || !par[P_ARPMODE]) {
                arp_stop();
                break;
            }
            tag_off(TAG_ARP);                   /* a new chord: the arp starts over (startArp) */
            for (i = 0; i < arp_pn; i++)
                arp_src[i] = arp_pend[i];
            arp_n = arp_pn;
            arp_build();
            arp_pos = 0;
            arp_last = -1;
            arp_left = 0.0f;
            om_arp_on = 1;
            break;
        }
        case C_BASS: bass_cmd(a); break;
        case C_PANIC: quiet(); break;
        case C_POLY: {                          /* fewer voices: the extra ones fade out fast */
            int i, busy = 0;
            om_poly_max = (uint8_t)(a < 4 ? 4 : a > OM_NVOICE ? OM_NVOICE : a);
            for (i = 0; i < OM_NVOICE; i++)
                if (vc[i].st != E_OFF && ++busy > om_poly_max) {
                    voice_release(&vc[i]);
                    vc[i].tag = 254;
                }
            break;
        }
        }
    }
}

/* ============================================================== render === */
#define BLK 256
static float mono[BLK], bassbuf[BLK];

static void render_voices(uint32_t n)
{
    int i;
    uint32_t j;
    const float g = ins_gain * lvl_gain, fast = 1.0f - fm_expf(-1.0f / (0.02f * OM_SR));
    float vph = vib_ph;
    for (j = 0; j < n; j++)
        mono[j] = 0.0f;
    for (i = 0; i < OM_NVOICE; i++) {
        voice_t *v = &vc[i];
        float env = v->env, menv = v->menv, mph = v->mph;
        const float kr = v->tag == 254 ? fast : k_rel;
        uint8_t st = v->st, mst = v->mst;
        if (st == E_OFF)
            continue;
        vph = vib_ph;
        for (j = 0; j < n; j++) {
            float dt = v->dt, o;
            if (vib_depth > 0.0f) {
                dt *= 1.0f + vib_depth * tsin(vph);
                vph += 5.0f / OM_SR;
                if (vph >= 1.0f)
                    vph -= 1.0f;
            }
            if (v->gate_left && !--v->gate_left && st != E_REL) {   /* an arp note's gate closes */
                st = mst = E_REL;
                midi(0x80, v->note, 0);
            }
            /* the envelope */
            if (st == E_ATT) {
                env += k_att;
                if (env >= 1.0f) {
                    env = 1.0f;
                    st = E_DEC;
                }
            } else if (st == E_DEC) {
                env += (sus - env) * k_dec;
            } else {
                env -= env * kr;
                if (env < 1e-4f) {
                    st = E_OFF;
                    break;
                }
            }
            if (ins->fm == K_ORGAN) {           /* drawbars 8', 4', 2 2/3', 2' and 1', and a
                                                 * percussion on the third harmonic that fades in 0.3 s */
                float p0 = v->ph[0], p2 = p0 + p0, p3 = p2 + p0, p4 = p2 + p2, p8 = p4 + p4;
                p2 -= (float)(int)p2;
                p3 -= (float)(int)p3;
                p4 -= (float)(int)p4;
                p8 -= (float)(int)p8;
                o = (tsin(p0) + 0.8f * tsin(p2) + (0.45f + 0.9f * v->perc) * tsin(p3) + 0.5f * tsin(p4) +
                     0.3f * tsin(p8)) * 0.33f;
                v->perc *= 0.99992f;
                v->ph[0] = wrap1(p0 + dt);
            } else if (ins->fm == K_BRASS || ins->fm == K_STRINGS) {
                int k, brass = ins->fm == K_BRASS;
                float x = 0.0f, d0 = dt, hp;
                if (brass) {                     /* a scoop up into the note, 1.5 % in 40 ms */
                    d0 *= 1.0f - 0.015f * v->scoop;
                    v->scoop *= 0.99943f;
                } else {                         /* a vibrato that comes in after the attack */
                    v->vamt += (0.0035f - v->vamt) * 0.00008f;
                    d0 *= 1.0f + v->vamt * tsin(v->vph);
                    v->vph = wrap1(v->vph + 5.3f / OM_SR);
                }
                for (k = 0; k < ins->count; k++) {
                    float d = d0 * v->det[k], t = v->ph[k];
                    x += 2.0f * t - 1.0f - blep(t, d);
                    t += d;
                    v->ph[k] = t >= 1.0f ? t - 1.0f : t;
                }
                x *= fat_gain;
                if (!(j & 15u)) {                /* the filter: Brass opens with its envelope and closes
                                                  * to a mellow tone; Strings stay soft and dark */
                    float hz;
                    if (brass) {
                        float tgt = st == E_ATT ? 1.0f : st == E_DEC ? 0.45f : 0.0f;
                        v->fenv += (tgt - v->fenv) * (st == E_ATT ? 0.06f : 0.012f);
                        hz = dt * OM_SR * (1.2f + 9.0f * v->fenv);
                    } else {
                        hz = dt * OM_SR * 5.0f;
                    }
                    hz = fm_minf(hz, brass ? 7000.0f : 3200.0f);
                    v->fc = 2.0f * fm_sinf(FM_PI * hz / OM_SR);
                }
                hp = x - v->flo - (brass ? 0.9f : 1.3f) * v->fbd;   /* Chamberlin state variable */
                v->fbd = fm_flush(v->fbd + v->fc * hp);
                v->flo = fm_flush(v->flo + v->fc * v->fbd);
                o = v->flo;
            } else if (ins->fm == K_FM) {        /* Tone.FMSynth: the carrier's frequency moves by
                                                 * f * index * modulator * its envelope */
                float m;
                if (mst == E_ATT) {
                    menv += k_matt;
                    if (menv >= 1.0f) {
                        menv = 1.0f;
                        mst = E_DEC;
                    }
                } else if (mst == E_DEC) {
                    menv += (msus - menv) * k_mdec;
                } else {
                    menv -= menv * k_mrel;
                }
                m = osc(ins->mwave, mph, dt * ins->harm) * menv;
                mph = wrap1(mph + dt * ins->harm);
                o = tsin(v->ph[0]);
                v->ph[0] += dt * (1.0f + ins->index * m);
                if (v->ph[0] >= 1.0f || v->ph[0] < 0.0f)
                    v->ph[0] -= fm_floorf(v->ph[0]);
            } else if (ins->count > 1) {         /* Tone.FatOscillator */
                int k;
                o = 0.0f;
                for (k = 0; k < ins->count; k++) {   /* fat sawtooths (every fat instrument) */
                    float d = dt * v->det[k], t = v->ph[k];
                    o += 2.0f * t - 1.0f - blep(t, d);
                    t += d;
                    v->ph[k] = t >= 1.0f ? t - 1.0f : t;
                }
                o *= fat_gain;
            } else {
                o = osc(ins->wave, v->ph[0], dt);
                v->ph[0] = wrap1(v->ph[0] + dt);
            }
            mono[j] += o * env * g;
        }
        v->env = env;
        v->menv = menv;
        v->mph = mph;
        v->st = st;
        v->mst = mst;
    }
    if (vib_depth > 0.0f)
        vib_ph = vph;
}

static void render_bass(uint32_t n)
{
    uint32_t j;
    int w = par[P_BASSTONE];
    for (j = 0; j < n; j++)
        bassbuf[j] = 0.0f;
    if (bs.st == E_OFF)
        return;
    for (j = 0; j < n; j++) {
        if (bs.st == E_ATT) {
            bs.env += bk_att;
            if (bs.env >= 1.0f) {
                bs.env = 1.0f;
                bs.st = E_DEC;
            }
        } else if (bs.st == E_DEC) {
            bs.env += (0.8f - bs.env) * bk_dec;
        } else {
            bs.env -= bs.env * bk_rel;
            if (bs.env < 1e-4f) {
                bs.st = E_OFF;
                break;
            }
        }
        bassbuf[j] = osc(w, bs.ph, bs.dt) * bs.env * bass_gain;
        bs.ph = wrap1(bs.ph + bs.dt);
    }
}

static inline float chebyshev(float x, int n)    /* T_n(x) by its recurrence */
{
    float t0 = 1.0f, t1 = x, t2;
    int k;
    if (n == 1)
        return x;
    for (k = 2; k <= n; k++) {
        t2 = 2.0f * x * t1 - t0;
        t0 = t1;
        t1 = t2;
    }
    return t1;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    float pk = 0.0f;
    render_voices(n);
    render_bass(n);
    for (j = 0; j < n; j++) {
        float x = mono[j], l, r, wl, wr;
        /* filters */
        if (lp_on)
            x = bq(&lp2, bq(&lp1, x));
        if (hp_on)
            x = bq(&hp2, bq(&hp1, x));
        if (af_on) {
            if (!(j & 31u)) {                    /* Tone.AutoFilter: 200 Hz, 2.6 octaves, -12 dB */
                float lfo = 0.5f + 0.5f * tsin(af_ph);
                bq_set(&af, 0, 200.0f * fm_exp2f(2.6f * lfo), 1.0f);
            }
            af_ph += af_rate;
            if (af_ph >= 1.0f)
                af_ph -= 1.0f;
            x = bq(&af, x);
        }
        /* distortion */
        if (od_on) {
            x = fm_clampf(x, -1.0f, 1.0f);       /* a WaveShaper's curve spans -1..1 */
            x = (3.0f + od_k) * x * (20.0f * FM_PI / 180.0f) / (FM_PI + od_k * fm_fabsf(x));
        }
        if (bc_on)
            x = bc_step * fm_floorf(x * bc_inv + 0.5f);
        if (ws_on) {
            float y = chebyshev(fm_clampf(x, -1.0f, 1.0f), ws_order);
            dc_y = fm_flush(y - dc_x + 0.9986f * dc_y);   /* DC out (~10 Hz) */
            dc_x = y;
            x = dc_y;
        }
        /* chorus: mono in, stereo out */
        if (ch_wet > 0.0f) {
            float dlL, dlR, s = tsin(ch_ph), pl, pr;
            int il, ir;
            ch_buf[ch_i] = x;
            dlL = 154.35f * (1.0f + 0.7f * s);  /* 3.5 ms +- 70 %, L and R opposite */
            dlR = 154.35f * (1.0f - 0.7f * s);
            pl = (float)ch_i - dlL;
            pr = (float)ch_i - dlR;
            if (pl < 0.0f)
                pl += (float)CH_N;
            if (pr < 0.0f)
                pr += (float)CH_N;
            il = (int)pl;
            ir = (int)pr;
            wl = ch_buf[il] + (ch_buf[(il + 1) % CH_N] - ch_buf[il]) * (pl - (float)il);
            wr = ch_buf[ir] + (ch_buf[(ir + 1) % CH_N] - ch_buf[ir]) * (pr - (float)ir);
            ch_i = (ch_i + 1) % CH_N;
            ch_ph += 1.5f / OM_SR;
            if (ch_ph >= 1.0f)
                ch_ph -= 1.0f;
            l = x * ch_dry + wl * ch_wet;
            r = x * ch_dry + wr * ch_wet;
        } else {
            l = r = x;
        }
        /* tremolo */
        if (tr_wet > 0.0f) {
            float s = tsin(tr_ph);
            tr_ph += 4.0f / OM_SR;
            if (tr_ph >= 1.0f)
                tr_ph -= 1.0f;
            l = l * tr_dry + l * (0.5f + 0.5f * s) * tr_wet;
            r = r * tr_dry + r * (0.5f - 0.5f * s) * tr_wet;
        }
        /* delay: an 8th note, feedback 0.3; one line for both sides (RAM) */
        if (dl_wet > 0.0f) {
            float p = (float)dl_i - dl_len, d;
            int i0;
            if (p < 0.0f)
                p += (float)DL_N;
            i0 = (int)p;
            d = ((float)dl_buf[i0] + ((float)dl_buf[(i0 + 1) % DL_N] - (float)dl_buf[i0]) * (p - (float)i0)) *
                (1.0f / 16384.0f);
            {
                float w = fm_clampf((l + r) * 0.5f + 0.3f * d, -1.99f, 1.99f);
                dl_buf[dl_i] = (int16_t)(w * 16384.0f);
            }
            if (++dl_i >= DL_N)
                dl_i = 0;
            l = l * dl_dry + d * dl_wet;
            r = r * dl_dry + d * dl_wet;
        }
        /* the bass joins here, at the reverb */
        l += bassbuf[j];
        r += bassbuf[j];
        if (rv_wet > 0.0f) {
            reverb((l + r) * 0.25f, &wl, &wr);
            l = l * rv_dry + wl * rv_wet;
            r = r * rv_dry + wr * rv_wet;
        }
        /* compressor */
        if (cm_on) {
            float a = fm_maxf(fm_fabsf(l), fm_fabsf(r));
            cm_env += (a - cm_env) * (a > cm_env ? cm_ka : cm_kr);
            if (!(j & 15u)) {
                float db = 20.0f * 0.30103f * fm_log2f(fm_maxf(cm_env, 1e-5f));
                cm_gain = fm_db2lin(cm_curve(db)) * cm_makeup;
            }
            l *= cm_gain;
            r *= cm_gain;
        }
        l *= gain;
        r *= gain;
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(l), fm_fabsf(r)));
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
    out_lvl = fm_maxf(pk, out_lvl * 0.9f);
}

void om_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 1.2f;
    drain();
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        if (om_arp_on) {                         /* the arp's steps land on their sample */
            if (arp_left <= 0.0f) {
                arp_step();
                arp_left += step_samples();
            }
            if ((float)k > arp_left)
                k = (uint32_t)arp_left + 1u;
            if (k > n)
                k = n;
            arp_left -= (float)k;
        }
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
    {   /* for the screen */
        uint32_t s[4] = {0, 0, 0, 0};
        int i, busy = 0;
        for (i = 0; i < OM_NVOICE; i++)
            if (vc[i].st != E_OFF) {
                busy++;
                if (vc[i].st != E_REL)
                    s[vc[i].note >> 5] |= 1u << (vc[i].note & 31u);
            }
        if (bs.st != E_OFF && bs.st != E_REL)
            s[bs.note >> 5] |= 1u << (bs.note & 31u);
        for (i = 0; i < 4; i++)
            om_sounding[i] = s[i];
        om_nvoices = (uint8_t)busy;
        om_level = fm_minf(out_lvl, 1.0f);
    }
}
