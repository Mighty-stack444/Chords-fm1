/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too.
 *
 *   white keys     the scale's chords, I II III ... up the keys (more than an octave of them)
 *   black keys     the scale's notes, an octave above (REC swaps the two rows)
 *   ENV / LFO      held with a chord key: one more note in the chord / one fewer
 *   PLAY           HOLD: chords and notes keep sounding until the next one (or HOLD off)
 *   REC            swap the rows: notes on the white keys, chords on the black
 *   ARP            the arpeggiator on / off
 *   OCT- / OCT+    the octave           SELECT scale    ALGORITHM root    PRESETS sound
 *   HOME SEQ FX SEL GLO   pages PLAY, ARP, EFFECTS, FILTER, SETUP; EDIT steps through all seven
 *   KNOB 1-4       the page's four values
 *   SAVE           saves (it also saves by itself, a few seconds after a change, when quiet) */

enum { V_PLAY, V_ARP, V_BASS, V_FX, V_FILTER, V_DRIVE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Play", "Arp", "Bass", "Effects", "Filter", "Drive", "Setup"};
enum { K_LEDS = 100, K_LAYOUT, K_NONE };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_VOICING, P_ATTACK, P_RELEASE, P_LEVEL},
    {P_ARPMODE, P_ARPRATE, P_TEMPO, P_GATE},
    {P_BASSMODE, P_BASSTONE, P_BASSVOL, K_NONE},
    {P_REVERB, P_DELAY, P_CHORUS, P_TREMOLO},
    {P_LOWPASS, P_HIGHPASS, P_AUTOFILT, P_VIBRATO},
    {P_DRIVE, P_CRUSH, P_SHAPE, P_COMP},
    {P_ARPOCT, P_TUNE, P_MIDI, K_LEDS},
};

#define NWHITE 16
#define NBLACK 11
static const uint8_t WHITE_K[NWHITE] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NBLACK] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* the web app's colours: near-black, its periwinkle accent, a colour per scale degree */
#define K_BG RGB(14, 14, 18)
#define K_PANEL RGB(26, 26, 34)
#define K_HDR RGB(19, 19, 24)
#define K_LINE RGB(58, 58, 68)
#define K_DIM RGB(136, 136, 160)
#define K_LABEL RGB(176, 176, 192)
#define K_TEXT RGB(240, 240, 244)
#define K_ACC RGB(143, 164, 255)
#define K_HOLD RGB(255, 107, 107)
#define K_GOLD RGB(252, 196, 25)
static const uint16_t PC_A[12] = {RGB(255, 107, 107), RGB(255, 146, 43), RGB(252, 196, 25), RGB(81, 207, 102),
                                  RGB(34, 184, 207),  RGB(92, 124, 250), RGB(204, 93, 232), RGB(240, 101, 149),
                                  RGB(32, 201, 151),  RGB(250, 176, 5),  RGB(253, 126, 20), RGB(132, 94, 247)};
static const uint16_t PC_B[12] = {RGB(42, 21, 24),  RGB(42, 28, 20), RGB(42, 38, 20), RGB(20, 42, 26),
                                  RGB(20, 36, 40),  RGB(24, 28, 46), RGB(36, 24, 46), RGB(42, 20, 32),
                                  RGB(20, 42, 36),  RGB(42, 36, 20), RGB(42, 30, 20), RGB(30, 24, 46)};

#define NKEYS_ 27
static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    /* what each key started when it went down (so its release lets go of exactly that) */
    uint8_t kn[NKEYS_][OM_MAXCHORD], kcount[NKEYS_], kchord[NKEYS_];
    int8_t held_chord_key, held_note_key;        /* HOLD: the key whose chord / note is held, -1 none */
    int8_t last_deg;                             /* the chord on the screen: its degree, -1 none */
    uint8_t last_notes[OM_MAXCHORD], last_n;
    int8_t last_note;                            /* the last melody note (MIDI), -1 none */
    uint8_t chords_down;                         /* chord keys down now */
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
} ui;

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }
static void mark_dirty(void) { ui.dirty = 1; }

static void scpy(char *b, const char *s)
{
    while ((*b++ = *s++))
        ;
}
static void scat_(char *b, const char *s)
{
    while (*b)
        b++;
    scpy(b, s);
}

/* --------------------------------------------------------- the music --- */
static int nvoice(void) { return proj.par[P_VOICING] + 2; }   /* Dyads .. 11ths: 2 .. 6 notes */
static int deg_minor(const char *q)                 /* the roman numeral in lower case (not "maj7") */
{
    return (q[0] == 'm' && q[1] != 'a') || (q[0] == 'd' && q[1] == 'i');
}
static int chord_of(int deg, int n, uint8_t *out)
{
    return om_chord_notes(proj.par[P_SCALE], proj.par[P_ROOT], proj.par[P_OCTAVE], deg, n, out);
}

/* everything quiet: the web app's update() (a new scale, root, voicing or octave) */
static void all_off(void)
{
    int k;
    om_arp(0, 0);
    om_tag_off(TAG_CHORD);
    om_tag_off(TAG_MELODY);
    om_bass(-1);
    ui.held_chord_key = ui.held_note_key = -1;
    for (k = 0; k < NKEYS_; k++)
        ui.kcount[k] = 0;
}

static void play_bass(const uint8_t *notes)
{
    int m = proj.par[P_BASSMODE];
    if (m)
        om_bass(notes[0] - 12 * m);
}

static void chord_down(int key, int deg)
{
    uint8_t notes[OM_MAXCHORD];
    int n = nvoice(), i, arp = proj.par[P_ARPMODE] != 0;
    uint32_t b = ui.btn;
    if (b >> B_ENV & 1u) {                       /* ENV: one more note, LFO: one fewer */
        n++;
        ui.btn_used |= 1u << B_ENV;
    }
    if (b >> B_LFO & 1u) {
        n--;
        ui.btn_used |= 1u << B_LFO;
    }
    n = n < 2 ? 2 : n > OM_MAXCHORD ? OM_MAXCHORD : n;
    n = chord_of(deg, n, notes);
    if (proj.hold && ui.held_chord_key >= 0) {   /* HOLD: the held chord gives way */
        int h = ui.held_chord_key;
        for (i = 0; i < ui.kcount[h]; i++)
            om_note(ui.kn[h][i], 0, TAG_CHORD);
        ui.kcount[h] = 0;
    }
    if (proj.hold)
        om_arp(0, 0);
    if (arp) {
        om_arp(notes, n);
    } else {
        for (i = 0; i < n; i++)
            om_note(notes[i], 1, TAG_CHORD);
    }
    for (i = 0; i < n; i++)
        ui.kn[key][i] = notes[i];
    ui.kcount[key] = (uint8_t)(arp ? 0 : n);
    ui.kchord[key] = 1;
    play_bass(notes);
    if (proj.hold)
        ui.held_chord_key = (int8_t)key;
    ui.chords_down++;
    ui.last_deg = (int8_t)deg;
    for (i = 0; i < n; i++)
        ui.last_notes[i] = notes[i];
    ui.last_n = (uint8_t)n;
}

static void chord_up(int key)
{
    int i;
    if (ui.chords_down)
        ui.chords_down--;
    if (proj.hold)
        return;
    for (i = 0; i < ui.kcount[key]; i++)
        om_note(ui.kn[key][i], 0, TAG_CHORD);
    ui.kcount[key] = 0;
    if (!ui.chords_down) {                       /* the last chord key up: the arp and bass stop */
        om_arp(0, 0);
        om_bass(-1);
    }
}

static void note_down(int key, int idx, int base_oct)
{
    int n = om_scale_note(proj.par[P_SCALE], proj.par[P_ROOT], base_oct, idx);
    if (proj.hold && ui.held_note_key >= 0) {
        int h = ui.held_note_key;
        if (ui.kcount[h])
            om_note(ui.kn[h][0], 0, TAG_MELODY);
        ui.kcount[h] = 0;
    }
    om_note(n, 1, TAG_MELODY);
    ui.kn[key][0] = (uint8_t)n;
    ui.kcount[key] = 1;
    ui.kchord[key] = 0;
    if (proj.hold)
        ui.held_note_key = (int8_t)key;
    ui.last_note = (int8_t)n;
}

static void note_up(int key)
{
    if (proj.hold)
        return;
    if (ui.kcount[key])
        om_note(ui.kn[key][0], 0, TAG_MELODY);
    ui.kcount[key] = 0;
}

/* which role a key has: a chord (deg >= 0) or a note (idx), by the layout */
static void key_role(int key, int *deg, int *idx, int *oct)
{
    int i;
    *deg = *idx = -1;
    *oct = proj.par[P_OCTAVE];
    for (i = 0; i < NWHITE; i++)
        if (WHITE_K[i] == key) {
            if (proj.layout)
                *idx = i;
            else
                *deg = i;
            return;
        }
    for (i = 0; i < NBLACK; i++)
        if (BLACK_K[i] == key) {
            if (proj.layout) {
                *deg = i;
            } else {
                *idx = i;
                *oct = proj.par[P_OCTAVE] + 1;   /* the melody row sits an octave over the chords */
            }
            return;
        }
}

static void key_event(int key, int down)
{
    int deg, idx, oct;
    if (!down) {
        if (ui.kchord[key])
            chord_up(key);
        else
            note_up(key);
        return;
    }
    key_role(key, &deg, &idx, &oct);
    if (deg >= 0)
        chord_down(key, deg);
    else if (idx >= 0)
        note_down(key, idx, oct);
}

/* ------------------------------------------------------------ values --- */
static int knob_get(int k)
{
    if (k < P_NPARAMS)
        return proj.par[k];
    if (k == K_LEDS)
        return proj.leds;
    if (k == K_LAYOUT)
        return proj.layout;
    return 0;
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k < P_NPARAMS) {
        *lo = om_param_info(k)->lo;
        *hi = om_param_info(k)->hi;
    } else {
        *lo = 0;
        *hi = k == K_LEDS ? 2 : k == K_LAYOUT ? 1 : 0;
    }
}
static const char *knob_name(int k)
{
    if (k < P_NPARAMS)
        return om_param_info(k)->name;
    return k == K_LEDS ? "Lights" : k == K_LAYOUT ? "Layout" : "";
}
static void knob_text(int k, char *b)
{
    int v = knob_get(k);
    if (k < P_NPARAMS)
        om_param_text(k, v, b);
    else if (k == K_LEDS)
        scpy(b, v == 2 ? "Keys" : v ? "On" : "Off");
    else if (k == K_LAYOUT)
        scpy(b, v ? "Notes" : "Chords");
    else
        b[0] = 0;
}

static void knob_set(int k, int v)
{
    int lo, hi;
    knob_range(k, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    if (v == knob_get(k))
        return;
    if (k < P_NPARAMS) {
        proj.par[k] = (int16_t)v;
        om_set(k, v);
        if (k == P_VOICING || k == P_SCALE || k == P_ROOT || k == P_OCTAVE)
            all_off();                           /* as the web app: a new key, everything stops */
        if (k == P_INSTR)
            om_arp(0, 0);                        /* the web app stops the arp for a new sound */
        if (k == P_ARPMODE && v)
            proj.arp_last = (uint8_t)v;
        if (k == P_SCALE)
            ui.last_deg = -1;
    } else if (k == K_LEDS) {
        proj.leds = (uint8_t)v;
    } else if (k == K_LAYOUT) {
        proj.layout = (uint8_t)v;
        all_off();
    }
    mark_dirty();
}

/* encoders that wrap round (scale, root, sound) */
static void knob_wrap(int k, int32_t e)
{
    int lo, hi, v;
    knob_range(k, &lo, &hi);
    v = knob_get(k) + (e > 0 ? 1 : -1);
    if (v > hi)
        v = lo;
    if (v < lo)
        v = hi;
    knob_set(k, v);
}

static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 6u : dt < 30u ? 4u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

static void turn(int role, int k, int32_t e)
{
    int lo, hi;
    if (!e || k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    knob_set(k, knob_get(k) + accel(role, e, hi - lo));
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static void button(int b)
{
    char t[16];
    switch (b) {
    case B_HOME: set_view(V_PLAY); break;
    case B_SEQ: set_view(V_ARP); break;
    case B_FX: set_view(V_FX); break;
    case B_SEL: set_view(V_FILTER); break;
    case B_GLO: set_view(V_SETUP); break;
    case B_EDIT: set_view((ui.view + 1) % NVIEWS); break;
    case B_PLAY:                                 /* HOLD */
        proj.hold = (uint8_t)!proj.hold;
        if (!proj.hold) {                        /* off: everything held lets go (the web app's HOLD) */
            int k;
            om_arp(0, 0);
            om_bass(-1);
            for (k = 0; k < NKEYS_; k++)
                if (!(ui.keys >> k & 1u) && ui.kcount[k]) {
                    int i;
                    for (i = 0; i < ui.kcount[k]; i++)
                        om_note(ui.kn[k][i], 0, ui.kchord[k] ? TAG_CHORD : TAG_MELODY);
                    ui.kcount[k] = 0;
                }
            ui.held_chord_key = ui.held_note_key = -1;
        }
        say("HOLD", proj.hold ? "ON" : "OFF");
        mark_dirty();
        break;
    case B_REC:
        knob_set(K_LAYOUT, !proj.layout);
        say(proj.layout ? "NOTES ON WHITE" : "CHORDS ON WHITE", 0);
        break;
    case B_ARP:
        if (proj.par[P_ARPMODE]) {
            knob_set(P_ARPMODE, 0);
            om_arp(0, 0);
            say("ARP", "OFF");
        } else {
            knob_set(P_ARPMODE, proj.arp_last ? proj.arp_last : 1);
            om_param_text(P_ARPMODE, proj.par[P_ARPMODE], t);
            say("ARP", t);
        }
        break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        om_param_text(P_OCTAVE, proj.par[P_OCTAVE], t);
        say("OCTAVE", t);
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else {
            say("NOT SAVED", "FLASH ERROR");
        }
        break;
    default: break;
    }
}

/* MIDI in (USB and the TRS jack): notes on any channel play the sound, as a sound module */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xF0u, n = (pkt >> 16) & 0x7Fu, v = pkt >> 24;
        if (st == 0x90u && v)
            om_note((int)n, 1, TAG_MIDI);
        else if (st == 0x80u || st == 0x90u)
            om_note((int)n, 0, TAG_MIDI);
        else if (st == 0xB0u && (n == 120u || n == 123u))
            om_tag_off(TAG_MIDI);
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_PLAY)                     /* HOLD on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY && i != B_ENV && i != B_LFO) {
            button((int)i);
        }
    }
    ch = keys ^ ui.keys;
    ui.keys = keys;
    for (i = 0; i < NKEYS_; i++)
        if (ch >> i & 1u)
            key_event((int)i, (int)(keys >> i & 1u));
    if (btn || keys || ch)
        ui.act_t = plat_ms();
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0)
        knob_wrap(P_SCALE, e);
    if ((e = plat_enc(EN_ALGO)) != 0)
        knob_wrap(P_ROOT, e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        knob_wrap(P_INSTR, e);
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            turn(EN_K1 + (int)i, VIEW_KNOB[ui.view][i], e);
            ui.touched = (int8_t)i;
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
}

/* ------------------------------------------------------------- screen --- */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(hash(2166136261u, ui.view | ui.dirty << 8 | (uint32_t)msg << 9),
                           (uint32_t)proj.par[P_SCALE] | (uint32_t)proj.par[P_ROOT] << 8),
                      (uint32_t)proj.par[P_INSTR]);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_HDR);
    cv_rect(0, HDR_H - 1, 240, 1, K_LINE);
    if (msg) {
        int32_t x = cv_text(10, 5, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 5, &FONT_B, ui.msg[1], K_ACC);
    } else {
        const char *in = OM_INSTR_NAME[proj.par[P_INSTR]];
        scpy(t, OM_ROOT_NAME[proj.par[P_ROOT]]);
        scat_(t, " ");
        scat_(t, OM_SCALE_NAME[proj.par[P_SCALE]]);
        cv_text(10, 5, &FONT_B, t, K_ACC);
        cv_text(232 - text_w(&FONT_S, in), 5, &FONT_S, in, K_LABEL);
        if (ui.dirty)
            cv_round(232 - text_w(&FONT_S, in) - 10, 11, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* a tag: a small rounded label */
static int32_t tag(int32_t x, int32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(&FONT_XS, s) + 10;
    cv_round(x, y, w, 16, 5, bg);
    cv_text(x + 5, y + 1, &FONT_XS, s, fg);
    return x + w + 4;
}

static uint16_t dimmed(uint16_t c)               /* a colour at about a fifth, on the dark ground */
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (uint16_t)((r / 5) << 11 | (g / 5) << 5 | (b / 5));
}

static void draw_main(void)
{
    char name[24], q[24], t[32];
    int len = om_scale_len(proj.par[P_SCALE]), d, i;
    uint32_t h = hash(2166136261u, (uint32_t)ui.last_deg | (uint32_t)ui.last_n << 8 | proj.hold << 12 |
                                       proj.layout << 13 | (uint32_t)(ui.last_note & 0xFF) << 16);
    for (i = 0; i < 4; i++)
        h = hash(h, om_sounding[i]);
    h = hash(hash(h, (uint32_t)proj.par[P_SCALE] | (uint32_t)proj.par[P_ROOT] << 4 | (uint32_t)proj.par[P_OCTAVE] << 8 |
                         (uint32_t)proj.par[P_VOICING] << 12 | (uint32_t)proj.par[P_ARPMODE] << 16 |
                         (uint32_t)proj.par[P_BASSMODE] << 20 | (uint32_t)proj.par[P_ARPRATE] << 24),
             (uint32_t)om_arp_on | (uint32_t)om_arp_note << 1 | (uint32_t)om_bass_note << 8);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    /* the chord: its name big, its numeral, its notes */
    if (ui.last_deg >= 0 && ui.last_n) {
        const uint8_t *mn = ui.last_notes;
        int sounding = 0, col = ui.last_deg % len;
        int32_t x;
        om_chord_quality(mn, ui.last_n, q);
        for (i = 0; i < ui.last_n; i++)
            sounding |= om_sounding[mn[i] >> 5] >> (mn[i] & 31u) & 1u;
        sounding |= om_arp_on;
        scpy(name, OM_NOTE_NAME[mn[0] % 12]);
        x = cv_text(12, 2, &FONT_L, name, sounding ? K_TEXT : K_LABEL);
        cv_text(x + 2, 15, &FONT_B, q, sounding ? K_TEXT : K_LABEL);
        om_roman(ui.last_deg % 12, deg_minor(q), t);
        cv_text(228 - text_w(&FONT_B, t), 5, &FONT_B, t, PC_A[col % 12]);
        t[0] = 0;
        for (i = 0; i < ui.last_n; i++) {
            if (i)
                scat_(t, " ");
            scat_(t, OM_NOTE_NAME[mn[i] % 12]);
        }
        cv_text(228 - text_w(&FONT_S, t), 24, &FONT_S, t, K_DIM);
    } else {
        cv_text(12, 10, &FONT_B, proj.layout ? "Play a black key" : "Play a white key", K_DIM);
    }
    {   /* the state, in tags */
        int32_t x = 12, y = 46;
        if (proj.hold)
            x = tag(x, y, "HOLD", K_HOLD, RGB(60, 26, 28));
        if (proj.par[P_ARPMODE]) {
            om_param_text(P_ARPMODE, proj.par[P_ARPMODE], t);
            scat_(t, " ");
            om_param_text(P_ARPRATE, proj.par[P_ARPRATE], q);
            scat_(t, q);
            x = tag(x, y, t, om_arp_on ? K_BG : K_ACC, om_arp_on ? K_ACC : RGB(32, 36, 60));
        }
        if (proj.par[P_BASSMODE]) {
            scpy(t, "BASS");
            if (om_bass_note) {
                scat_(t, " ");
                scat_(t, OM_NOTE_NAME[om_bass_note % 12]);
            }
            x = tag(x, y, t, K_GOLD, RGB(50, 44, 18));
        }
        om_param_text(P_VOICING, proj.par[P_VOICING], t);
        x = tag(x, y, t, K_LABEL, K_PANEL);
        if (ui.last_note >= 0) {                 /* the last melody note, at the right */
            scpy(t, OM_NOTE_NAME[ui.last_note % 12]);
            {
                char o[4];
                int ov = ui.last_note / 12 - 1;
                o[0] = (char)('0' + (ov < 0 ? 0 : ov));
                o[1] = 0;
                scat_(t, o);
            }
            cv_text(228 - text_w(&FONT_B, t), y, &FONT_B, t,
                    om_sounding[ui.last_note >> 5] >> (ui.last_note & 31) & 1u ? K_TEXT : K_DIM);
        }
        (void)x;
    }
    /* the scale's chords: a pad per degree, as the web app's grid */
    {
        int32_t pw = 232 / len, x0 = 4 + (232 - pw * len) / 2, y0 = 70;
        for (d = 0; d < len; d++) {
            uint8_t mn[OM_MAXCHORD];
            int n = chord_of(d, nvoice(), mn), on = 0, sel = ui.last_deg >= 0 && ui.last_deg % len == d;
            int32_t x = x0 + d * pw, cx = x + pw / 2;
            char *p;
            for (i = 0; i < n; i++)
                on |= om_sounding[mn[i] >> 5] >> (mn[i] & 31u) & 1u;
            on = on && sel;
            om_chord_quality(mn, n, q);
            for (p = q; *p; p++)                  /* the pad shows the chord's base: "maj7", not "maj7(9)" */
                if (*p == '(') {
                    *p = 0;
                    break;
                }
            cv_round(x + 1, y0, pw - 2, 68, 6, sel ? PC_A[d] : K_LINE);
            cv_round(x + 2, y0 + 1, pw - 4, 66, 5, on ? PC_A[d] : sel ? PC_B[d] : K_PANEL);
            cv_rect(x + 2, y0 + 8, 3, 52, on ? K_BG : PC_A[d]);
            om_roman(d, deg_minor(q), t);
            text_c(cx + 1, y0 + 4, &FONT_XS, t, on ? K_BG : K_LABEL);
            text_c(cx + 1, y0 + 20, &FONT_B, OM_NOTE_NAME[mn[0] % 12], on ? K_BG : K_TEXT);
            text_c(cx + 1, y0 + 40, &FONT_XS, q, on ? K_BG : K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, (uint32_t)knob_get(VIEW_KNOB[ui.view][i]));
    h = hash(h, (uint32_t)now_touch);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_HDR);
    cv_text(10, 3, &FONT_XS, VIEW_NAME[ui.view], K_ACC);   /* the page, small, top left */
    for (i = 0; i < 4; i++) {
        int k = VIEW_KNOB[ui.view][i], lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        char t[16];
        if (k == K_NONE)
            continue;
        if (i)
            cv_rect(x, 18, 1, KNB_H - 32, K_LINE);
        knob_range(k, &lo, &hi);
        v = knob_get(k);
        text_c(cx, 16, &FONT_XS, knob_name(k), hot ? K_TEXT : K_LABEL);
        knob_text(k, t);
        {
            const char *qq = t;
            int lower = 0;
            for (; *qq; qq++)
                lower |= *qq >= 'a' && *qq <= 'z';
            if (lower || text_w(&FONT_M, t) > 56)
                text_c(cx, 32, &FONT_B, t, hot ? K_ACC : K_TEXT);
            else
                text_c(cx, 29, &FONT_M, t, hot ? K_ACC : K_TEXT);
        }
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 54, 44, 5, 2, K_LINE);
        if (lo < 0) {
            int32_t m = cx - 22 + 44 * (0 - lo) / (hi - lo), e = cx - 22 + bw;
            cv_round(e < m ? e : m, 54, (e < m ? m - e : e - m) + 5, 5, 2, K_ACC);
        } else if (bw) {
            cv_round(cx - 22, 54, bw < 5 ? 5 : bw, 5, 2, K_ACC);
        }
    }
    cv_blit(0, KNB_Y);
}

static void leds(void)
{
    uint32_t b = 0, k = 0, i;
    static const int8_t VIEW_BTN[NVIEWS] = {B_HOME, B_SEQ, -1, B_FX, B_SEL, -1, B_GLO};
    if (VIEW_BTN[ui.view] >= 0)
        b |= 1u << VIEW_BTN[ui.view];
    else
        b |= 1u << B_EDIT;                       /* the pages without a button: EDIT lit */
    if (proj.hold)
        b |= 1u << B_PLAY;
    if (proj.layout)
        b |= 1u << B_REC;
    if (proj.par[P_ARPMODE] && (!om_arp_on || (ui.frame / 4u) & 1u || !(om_arp_note & 1u)))
        b |= 1u << B_ARP;
    if (proj.leds) {
        for (i = 0; i < NKEYS_; i++)             /* the keys held, or sounding under HOLD */
            if (ui.keys >> i & 1u || (proj.hold && (ui.held_chord_key == (int)i || ui.held_note_key == (int)i)))
                k |= 1u << i;
    }
    plat_glow(proj.leds == 1);
    plat_leds(b, k);
}

/* the CPU guard: the audio load is measured on the device (plat_cpu_pct, of the render's time budget).
 * Over 85 %: two voices fewer; under 55 % for a second: one back, up to all twelve. */
static void cpu_guard(void)
{
    static uint32_t calm_since, last_drop;
    uint32_t now = plat_ms(), pct = plat_cpu_pct();
    if (pct > 85u && now - last_drop > 250u && om_poly_max > 4u) {
        om_poly(om_poly_max - 2);
        last_drop = now;
        calm_since = now;
    } else if (pct < 55u) {
        if (om_poly_max < OM_NVOICE && now - calm_since > 1000u) {
            om_poly(om_poly_max + 1);
            calm_since = now;
        }
    } else {
        calm_since = now;
    }
}

#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms();
    if (!ui.dirty || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || om_nvoices || om_arp_on)
        return;
    if (project_save() == 0)
        ui.dirty = 0;
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.held_chord_key = ui.held_note_key = -1;
    ui.last_deg = -1;
    ui.last_note = -1;
    lcd_fill(0, 0, 240, 240, K_BG);
}

void ui_frame(void)
{
    input();
    cpu_guard();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
