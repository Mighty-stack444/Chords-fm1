/* SPDX-License-Identifier: GPL-3.0-only */
/* CHORDS project: one flash object (OBJ_PROJ). A project of another format, or none, gives the
 * defaults. Part of the unity build, after app.h. */
project_t proj;

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    proj.arp_last = 1;                           /* Up */
    proj.mel_set = 1;
    proj.mel_density = 55;
    proj.mel_bars = 1;                           /* 2 bars */
    proj.mel_range = 1;                          /* an octave over the chord */
    proj.mel_seed[0] = 0x2B;
    proj.mel_seed[1] = 0x5C;
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
    if (!proj.mel_set) {                         /* a 0.2 project: the melody's defaults */
        proj.mel_set = 1;
        proj.mel_density = 55;
        proj.mel_bars = 1;
        proj.mel_range = 1;
        proj.mel_seed[0] = 0x2B;
        proj.mel_seed[1] = 0x5C;
    }
    if (proj.mel_density > 100u || proj.mel_bars > 2u || proj.mel_range > 2u)
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
