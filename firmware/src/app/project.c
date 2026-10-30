/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults. Part of the unity build, after app.h. */
project_t proj;

/* the melody's settings, as far as the project has none (mel_set: what it has) */
static void mel_defaults(void)
{
    if (proj.mel_set < 1) {                      /* 0.2 and new projects */
        proj.mel_density = 55;
        proj.mel_bars = 1;                       /* 2 bars */
        proj.mel_range = 1;                      /* (0.3's "Mid", made +1 below) */
        proj.mel_seed[0] = 0x2B;
        proj.mel_seed[1] = 0x5C;
    }
    if (proj.mel_set < 2) {                      /* 0.3: its Register (0..2) is the octave 0..+2 */
        proj.mel_range = (uint8_t)(proj.mel_range + 1);
        proj.mel_vol = 70;
        proj.mel_swing = 0;
        proj.mel_gate = 70;
        proj.mel_var = 40;
        proj.mel_leap = 30;
        proj.mel_contour = 0;
        proj.mel_sync = 20;
        proj.mel_end = 0;
    }
    proj.mel_set = 2;
}

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    proj.arp_last = 1;                           /* Up */
    proj.mel_set = 0;
    mel_defaults();
    proj.leds = 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const om_param_t *p = om_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    if (proj.layout > 1u || proj.leds > 2u || proj.arp_last < 1u || proj.arp_last > 5u)
        return 0;
    mel_defaults();                              /* an older project: the melody's settings it lacks */
    if (proj.mel_density > 100u || proj.mel_bars > 2u || proj.mel_range > 4u || proj.mel_vol > 100u ||
        proj.mel_swing > 100u || proj.mel_gate < 10u || proj.mel_gate > 100u || proj.mel_var > 100u ||
        proj.mel_leap > 100u || proj.mel_contour > 4u || proj.mel_sync > 100u || proj.mel_end > 3u)
        return 0;
    return 1;
}

int project_load(void)
{
    int n;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (n >= 8 && project_valid())
        return 0;
    project_defaults();
    return -1;
}

int project_save(void) { return plat_store_save(OBJ_PROJ, &proj, sizeof proj); }

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
}
