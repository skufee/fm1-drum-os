/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Formats: 6 ("FUN6", written, SLOOP 2.6): format 5 with the ten parameters of 2.6 (P_ITYPE .. P_LPOL, just
 * before P_E0: the INSERT, the ANALOG filter TYPE, ENV SYNC, LFO 2) in a byte each, and each step's nudge (6 bits)
 * and fill condition (2 bits) in one byte: format 5 was full (3840 bytes, one flash object), this is 3816.
 * Read and converted: 5 ("FUN5", SLOOP 2.4 .. 2.5: format 4 plus, per track, the nudge of each step (micro), NLOCK
 * parameter locks, the steps' fill conditions (2 bits each) and P_TFLT, one parameter more just before P_E0;
 * converted by id, the 2.6 parameters at their defaults, a lock on P_E0.. moved with it), 4 ("FUN4", SLOOP 2.0 .. 2.3: PROJ_NP_V4 parameters, today's G_COUNT, 10-byte
 * steps with levels and ratchets, the drum track's 16 lanes; no nudge, no lock), 3 ("FUN3", SLOOP 1.x: 8-byte
 * steps, the drum track's notes become its lanes, the swings x 0.8 for the MPC scale), 2 ("FUN2") and 1
 * ("FUN1"), which held PROJ_NP_V2 parameters per track, mapped by count as user presets are (the first
 * PROJ_NP_V2 - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the parameters added since take their
 * defaults). Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the
 * engines added since were appended, no index moved; the drum track's byte (it has no engine) becomes 0.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), drums.c (the lanes), the engines and trk_def_engine (ui.c). */
#include "drum_record.h"
#define PROJ_MAGIC 0x46554E36u                 /* "FUN6": format 5 + the 2.6 parameters, nudge and condition in a byte */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": format 4 + per-step nudge, parameter locks, fill conditions (SLOOP 2.4) */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, P_COUNT parameters each, 10-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": SLOOP 1.x; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V5 61u                         /* P_COUNT of format 5 (P_E0 was 53) */
#define PROJ_NP_V4 58u                         /* P_COUNT of format 4 (P_E0 was 50) */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NG_V3 27u                         /* G_COUNT of formats 1..3 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NG 32u                            /* the globals of formats 4 and 5 (G_COUNT until 2.4). SLOOP 2.5's
                                                * G_DRDLY rides in the byte after sel (0 in older projects: off),
                                                * so the format and its size do not change */
/* The serialized track count is a wire-format property, not the runtime drum track count. */
#define PROJ_NTRK 4u
#define PROJ_DRUM_TRACK 3u             /* historical FUN wire index; runtime drum index is TRK_DRUM (0) */
_Static_assert(G_DRDLY == PROJ_NG && G_TXSRC == PROJ_NG + 5u && G_COUNT == G_BEND + 1u,
               "the globals a project holds, then G_DRDLY (then 2.6's GLO > MIDI: settings of the FM-1)");
#define PROJ_NC6 ((uint32_t)P_ITYPE)           /* format 6: the parameters kept in 16 bits, P_LEVEL .. P_VLEAD (as 5), */
#define PROJ_NX6 ((uint32_t)(P_E0 - P_ITYPE))  /* the 2.6 ones in a byte (0..127), then P_E0 .. P_E7 */
_Static_assert(PROJ_NC6 == PROJ_NP_V5 - 8u && PROJ_NX6 == 10u, "format 6: format 5's parameters, then ten bytes");
typedef struct {                               /* one track; the drum track ignores engine / preset */
    int16_t p[PROJ_NC6];                       /* P_LEVEL .. P_VLEAD (pt_get, pt_set) */
    int16_t e[8];                              /* P_E0 .. P_E7 */
    uint8_t x[PROJ_NX6];                       /* P_ITYPE .. P_LPOL (2.6) */
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];                  /* (the drum track: 16 lanes, the same size) */
    };
    uint8_t mf[NSTEP];                         /* each step's nudge (core.h MICRO_*, 6 bits) | its fill condition << 6 */
    plock_t lock[NLOCK];                       /* and its parameter locks (step LOCK_FREE = none) */
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2];                /* the selected track; 2.5: G_DRDLY */
    proj_trk_t t[PROJ_NTRK];
    uint32_t sum;
} project_t;
static int16_t pt_get(const proj_trk_t *t, uint32_t id)   /* p[id] of a stored track */
{
    return id < PROJ_NC6 ? t->p[id] : id < P_E0 ? (int16_t)t->x[id - PROJ_NC6] : t->e[(id - P_E0) & 7u];
}
static void pt_set(proj_trk_t *t, uint32_t id, int32_t v)
{
    if (id < PROJ_NC6)
        t->p[id] = (int16_t)v;
    else if (id < P_E0)
        t->x[id - PROJ_NC6] = (uint8_t)clamp(v, 0, 255);
    else
        t->e[(id - P_E0) & 7u] = (int16_t)v;
}
static int32_t pt_micro(const proj_trk_t *t, uint32_t i) { int32_t m = t->mf[i] & 63; return m >= 32 ? m - 64 : m; }
static uint32_t pt_fill(const proj_trk_t *t, uint32_t i) { return (uint32_t)t->mf[i] >> 6; }
static void pt_set_mf(proj_trk_t *t, uint32_t i, int32_t micro, uint32_t fill)
{
    t->mf[i] = (uint8_t)(((uint32_t)micro & 63u) | (fill & 3u) << 6);
}
typedef struct {                               /* a track of format 5 (SLOOP 2.4 .. 2.5), read only */
    int16_t p[PROJ_NP_V5];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
    int8_t micro[NSTEP];                       /* each step's nudge (core.h) */
    plock_t lock[NLOCK];                       /* and its parameter locks (step LOCK_FREE = none) */
    uint8_t fill[NSTEP / 4];                   /* and its fill condition, 2 bits a step (FC_*) */
} proj_trk_v5_t;
typedef struct {                               /* format 5, read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, drdly, rsv[2];
    proj_trk_v5_t t[PROJ_NTRK];
    uint32_t sum;
} project_v5_t;
typedef struct {                               /* a track of format 4 (SLOOP 2.0 .. 2.3), read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
} proj_trk_v4_t;
typedef struct {                               /* format 4, read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG];
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[PROJ_NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct { uint8_t note[4], n, time, flags, vel; } step8_t;   /* the steps of formats 1..3 */
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (SLOOP 1.x), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V3];
    uint8_t sel, rsv[3];
    proj_trk_v3_t t[PROJ_NTRK];
    uint32_t sum;
} project_v3_t;
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[PROJ_NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v4_t) == 3112u && sizeof(project_v5_t) == 3840u, "formats 1 / 2 / 3 / 4 / 5 as they were stored");
_Static_assert(sizeof(project_t) <= 3840u, "format 6 fits one flash object (storage.c ST_PAYLOAD_MAX)");
project_t proj_slot[4] __attribute__((section(".noinit")));
/* A project_t is the historical FUN working snapshot and intentionally does not grow for A3.2a.
 * Keep volatile timing alongside the four RAM-only sections instead. */
static int8_t section_raw[4][DRUM_RAW_LANES][NSTEP] __attribute__((section(".pool")));

static void section_raw_capture(uint32_t s)
{
    memcpy(section_raw[s & 3u], drum_raw_offset, sizeof drum_raw_offset);
}

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

/* ---- old formats -> format 5 */
/* an old step into a synth step (no level, no ratchet) */
static void step_from8(step_t *d, const step8_t *s)
{
    memcpy(d->note, s->note, 4);
    d->n = s->n;
    d->time = s->time;
    d->flags = s->flags;
    d->vel = s->vel;
    d->lvl = d->rat = 0;
}
/* an old drum step (GM notes) into the drum track's lanes; its velocity / accent -> their level */
static void dstep_from8(dstep_t *d, const step8_t *s)
{
    uint32_t i, lvl = (s->flags & SF_ACCENT) || s->vel > 115u ? LV_HARD : !s->vel ? LV_NORM : vel_lvl(s->vel);
    memset(d, 0, sizeof *d);
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++)
        dstep_set(d, lane_of_note(s->note[i] & 127u), lvl, 0);
}
static int16_t swing_from_v3(int32_t v) { return (int16_t)clamp((v * 4 + 2) / 5, 0, 100); }   /* /250 -> /200 */

/* the globals of formats 1..3 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_old(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < PROJ_NG; i++)
        g[i] = i < PROJ_NG_V3 ? g2[i] : GP[i].def;
    g[G_SWING] = swing_from_v3(g[G_SWING]);
}

/* no nudge, no lock, no condition (formats 1..4); the 2.6 parameters at their defaults (formats 1..5) */
static void proj_trk_plain(proj_trk_t *d)
{
    uint32_t k;
    memset(d->mf, 0, sizeof d->mf);
    for (k = PROJ_NC6; k < P_E0; k++)
        pt_set(d, k, TP[k].def);
    for (k = 0; k < NLOCK; k++) {
        d->lock[k].step = LOCK_FREE;
        d->lock[k].param = 0;
        d->lock[k].val = 0;
    }
}

/* a track of format 3 -> today's (by id up to P_SLDEPTH; P_E0.. moved) */
static void proj_trk_from_v3(proj_trk_t *d, const proj_trk_v3_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V3 - 8u;
    proj_trk_plain(d);
    for (k = 0; k < PROJ_NC6; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->e[k] = s->p[nc + k];
    d->p[P_SSWING] = swing_from_v3(d->p[P_SSWING]);
    d->p[P_ASWING] = swing_from_v3(d->p[P_ASWING]);
    d->engine = drum ? 0u : s->engine;
    d->preset = drum ? 0u : s->preset;
    for (k = 0; k < NSTEP; k++) {
        if (drum)
            dstep_from8(&d->dstep[k], &s->step[k]);
        else
            step_from8(&d->step[k], &s->step[k]);
    }
}

/* a track of formats 1 and 2 -> format 3 (mapped by count, see the top) */
static void proj_trk_v2_to_v3(proj_trk_v3_t *d, const proj_trk_v2_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V2 - 8u;
    for (k = 0; k < PROJ_NP_V3 - 8u; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[PROJ_NP_V3 - 8u + k] = s->p[nc + k];
    d->engine = drum ? 0u : s->engine;          /* (indices 0..7 as they were) */
    d->preset = drum ? 0u : s->preset;
    memcpy(d->step, s->step, sizeof d->step);
}

/* a format 4 project (n bytes in *v4) -> slot q as format 6: the same, no nudge, no lock, no condition, no FILTER */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    q->sel = v4->sel;
    for (i = 0; i < PROJ_NTRK; i++) {
        uint32_t k;
        proj_trk_plain(&q->t[i]);
        for (k = 0; k < PROJ_NC6; k++)                 /* by id up to P_CHORD, the parameters added since: */
            q->t[i].p[k] = k < PROJ_NP_V4 - 8u ? v4->t[i].p[k] : TP[k].def;   /* their defaults; P_E0.. moved */
        for (k = 0; k < 8u; k++)
            q->t[i].e[k] = v4->t[i].p[PROJ_NP_V4 - 8u + k];
        q->t[i].engine = v4->t[i].engine;
        q->t[i].preset = v4->t[i].preset;
        memcpy(q->t[i].step, v4->t[i].step, sizeof q->t[i].step);
    }
    q->sum = proj_sum(q);
    return 1;
}

/* a format 5 project (n bytes in *v5) -> slot q as format 6: by id, the 2.6 parameters at their defaults, each step's
 * nudge and condition into its byte, the locks as they were (a lock on P_E0.. follows its parameter's new id) */
static int proj_from_v5(project_t *q, const project_v5_t *v5, int n)
{
    uint32_t i, k;
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v5->g, sizeof q->g);
    q->sel = v5->sel;
    q->drdly = v5->drdly;
    for (i = 0; i < PROJ_NTRK; i++) {
        const proj_trk_v5_t *s = &v5->t[i];
        proj_trk_t *d = &q->t[i];
        proj_trk_plain(d);
        for (k = 0; k < PROJ_NC6; k++)
            d->p[k] = s->p[k];
        for (k = 0; k < 8u; k++)
            d->e[k] = s->p[PROJ_NP_V5 - 8u + k];
        d->engine = s->engine;
        d->preset = s->preset;
        memcpy(d->step, s->step, sizeof d->step);
        for (k = 0; k < NSTEP; k++)
            pt_set_mf(d, k, clamp(s->micro[k], MICRO_MIN, MICRO_MAX), (uint32_t)s->fill[k / 4u] >> (2u * (k % 4u)));
        for (k = 0; k < NLOCK; k++) {
            d->lock[k] = s->lock[k];
            if (s->lock[k].step != LOCK_FREE && s->lock[k].param >= PROJ_NP_V5 - 8u)   /* P_E0.. of format 5 */
                d->lock[k].param = (uint8_t)(s->lock[k].param - (PROJ_NP_V5 - 8u) + P_E0);
        }
    }
    q->sum = proj_sum(q);
    return 1;
}

/* a format 3 project -> slot q as format 6 */
static void proj_from_v3_ok(project_t *q, const project_v3_t *v3)
{
    uint32_t i;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_old(q->g, v3->g);
    q->sel = v3->sel;
    for (i = 0; i < PROJ_NTRK; i++)
        proj_trk_from_v3(&q->t[i], &v3->t[i], i == PROJ_DRUM_TRACK);
    q->sum = proj_sum(q);
}
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    proj_from_v3_ok(q, v3);
    return 1;
}

static project_v3_t proj_v3_tmp;               /* (formats 1, 2: through format 3) */
/* a format 2 project (n bytes in *v2) -> slot q as format 5 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v2->g, sizeof v3->g);
    v3->sel = v2->sel;
    for (i = 0; i < PROJ_NTRK; i++)
        proj_trk_v2_to_v3(&v3->t[i], &v2->t[i], i == PROJ_DRUM_TRACK);
    proj_from_v3_ok(q, v3);
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 5: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v1->g, sizeof v3->g);
    proj_trk_v2_to_v3(&v3->t[0], &v1->t, 0);
    proj_from_v3_ok(q, v3);
    for (i = 1; i < PROJ_NTRK; i++) {          /* historical wire tracks; drum-only runtime uses only index 3 */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            pt_set(&q->t[i], k, TP[k].def);
        q->t[i].engine = 0;
        q->t[i].preset = 0;
        memset(q->t[i].step, 0, sizeof q->t[i].step);
        if (i != PROJ_DRUM_TRACK)
            for (k = 0; k < NSTEP; k++)
                q->t[i].step[k].time = ST_REST;
        proj_trk_plain(&q->t[i]);
    }
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (any format) -> slot q as format 5; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
    return proj_from_v5(q, (const project_v5_t *)b, n) || proj_from_v4(q, (const project_v4_t *)b, n) ||
           proj_from_v3(q, (const project_v3_t *)b, n) ||
           proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n);
}

/* ---- the working project <-> a project_t */
/* 2.6: GLO > TEXTURE in a project (a song section has its own): in the slots of two globals a project no longer
 * applies (G_DRCH: a setting of the FM-1 now; G_INFO: the CPU readout) and the spare bytes. rsv[0]: 0x80 (there) | SRC
 * | WHEN << 4 | DUCK << 5, rsv[1]: RATE, g[G_DRCH]: LEVEL | (TONE + 64) << 7, g[G_INFO]: MOVE. Without the 0x80 (a
 * project before it, converted): the defaults, OFF */
static void proj_tx_put(project_t *p)
{
    p->rsv[0] = (uint8_t)(0x80u | ((uint32_t)song.g[G_TXSRC] & 15u) | ((uint32_t)song.g[G_TXWHEN] & 1u) << 4 |
                          ((uint32_t)song.g[G_TXDUCK] & 1u) << 5);
    p->rsv[1] = (uint8_t)song.g[G_TXRATE];
    p->g[G_DRCH] = (int16_t)((song.g[G_TXLVL] & 127) | ((song.g[G_TXTONE] + 64) & 127) << 7);
    p->g[G_INFO] = song.g[G_TXMOVE];
}
static void proj_tx_get(const project_t *p)
{
    static const uint8_t ID[7] = {G_TXSRC, G_TXLVL, G_TXTONE, G_TXMOVE, G_TXRATE, G_TXWHEN, G_TXDUCK};
    int32_t v[7];
    uint32_t i;
    if (!(p->rsv[0] & 0x80u)) {
        for (i = 0; i < 7u; i++)
            v[i] = GP[ID[i]].def;
    } else {
        v[0] = p->rsv[0] & 15u, v[5] = (p->rsv[0] >> 4) & 1u, v[6] = (p->rsv[0] >> 5) & 1u, v[4] = p->rsv[1];
        v[1] = p->g[G_DRCH] & 127, v[2] = ((p->g[G_DRCH] >> 7) & 127) - 64, v[3] = p->g[G_INFO];
    }
    for (i = 0; i < 7u; i++)
        song.g[ID[i]] = (int16_t)clamp(v[i], GP[ID[i]].min, GP[ID[i]].max);
}

static void proj_capture(project_t *p)        /* what is playing now, as a project */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PROJ_NG; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->drdly = (uint8_t)song.g[G_DRDLY];
    proj_tx_put(p);
    for (i = 0; i < NTRK; i++) {
        uint32_t w = NTRK == 1u ? PROJ_DRUM_TRACK : i;
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            pt_set(&p->t[w], k, trk[i].p[k]);
        p->t[w].engine = 0;
        p->t[w].preset = 0;
        memcpy(p->t[w].step, trk[i].step, sizeof trk[i].step);
        for (k = 0; k < NSTEP; k++)
            pt_set_mf(&p->t[w], k, trk[i].micro[k], (uint32_t)trk[i].fill[k / 4u] >> (2u * (k % 4u)));
        memcpy(p->t[w].lock, trk[i].lock, sizeof trk[i].lock);
    }
    p->sum = proj_sum(p);
}

/* a project's tracks (and its globals, all: a load; or only the drum level / reverb / delay: a song
 * section) into the working one, every value back inside its range. The audio ISR must not run
 * meanwhile (the song sections: called from it; a load: IRQ off) */
static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
    drum_qtz_strength_set(0); /* volatile A3.3 control: every project/section apply starts at raw timing */
    for (i = 0; i < PROJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE && i != G_DRCH &&
                      i != G_INFO
                : i == G_DRLVL || i == G_DRREV)                 /* (2.6: the drum channel a setting of the FM-1, GLO > MIDI;
                                                                 * its slot and G_INFO's hold the TEXTURE) */
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    song.g[G_DRDLY] = (int16_t)clamp(p->drdly, GP[G_DRDLY].min, GP[G_DRDLY].max);   /* (a load and a section, as REV) */
    proj_tx_get(p);                                     /* GLO > TEXTURE (2.6): a load and a section */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[NTRK == 1u ? PROJ_DRUM_TRACK : k];
        t->engine = t->eng_req = 0;
        t->user = 0;                                    /* (no user preset slot is saved) */
        t->lk_n = 0;                                    /* (the locks in force: the values come from the project) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i == P_E0 ? &DRUM_KIT_DESC : &TP[i];
            t->p[i] = (int16_t)(i == P_E0 ? drum_preset_compat_id(pt_get(s, i)) :
                                clamp(pt_get(s, i), d->min, d->max));
        }
        t->preset = 0;
        memcpy(t->step, s->step, sizeof t->step);
        memset(t->fill, 0, sizeof t->fill);
        for (i = 0; i < NSTEP; i++) {                   /* the nudges and conditions (3 means normal: 0) */
            uint32_t c = pt_fill(s, i);
            t->micro[i] = (int8_t)clamp(pt_micro(s, i), MICRO_MIN, MICRO_MAX);
            if (c != 3u)
                t->fill[i / 4u] |= (uint8_t)(c << (2u * (i % 4u)));
        }
        for (i = 0; i < NLOCK; i++) {
            const plock_t *l = &s->lock[i];
            uint32_t id = l->param;
            if (l->step < NSTEP && id < P_COUNT && p_lockable(id) && !(is_drum(t) && id == P_E0)) {
                const param_desc_t *d = id == P_E0 ? &DRUM_KIT_DESC : &TP[id];
                t->lock[i].step = l->step;
                t->lock[i].param = (uint8_t)id;
                t->lock[i].val = (int16_t)clamp(l->val, d->min, d->max);
            } else {
                t->lock[i].step = LOCK_FREE;            /* no such step or parameter: the slot is free */
                t->lock[i].param = 0;
                t->lock[i].val = 0;
            }
        }
        /* The internal value at wire track 3 is a drum pattern; no melodic engine state is applied. */
    }
}

/* The compact format is a projection of the live drum track. Older FUN
 * projects can be loaded into RAM and saved to a different erased slot. */
static const uint8_t drumrec_track_fields[11] =
    {P_LEVEL, P_PAN, P_MUTE, P_SDIV, P_SSWING, P_SGATE, P_DIST, P_CHOR, P_DLY, P_REV, P_E0};
static const uint8_t drumrec_global_fields[5] = {G_CLOCK, G_TUNE, G_DRLVL, G_DRREV, G_DRDLY};

static void drumrec_capture_live(drumrec_t *r)
{
    uint32_t i;
    memset(r, 0, sizeof *r);
    r->bpm = (uint16_t)song.g[G_BPM];
    r->swing = (uint16_t)song.g[G_SWING];
    r->selected_pad = drum_pad_sel;
    r->pattern_length = (uint8_t)TDRUM->p[P_SLEN];
    for (i = 0; i < 11u; i++) r->project_fields[i] = TDRUM->p[drumrec_track_fields[i]];
    for (i = 0; i < 5u; i++) r->project_fields[11u + i] = song.g[drumrec_global_fields[i]];
    for (i = 0; i < NSTEP; i++) {
        memcpy(r->steps[i], &TDRUM->dstep[i], sizeof(dstep_t));
        /* PAD09..PAD16 are hidden by the eight-pad product UI. */
        r->steps[i][1] = r->steps[i][4] = r->steps[i][5] = r->steps[i][8] = r->steps[i][9] = 0;
    }
    for (i = 0; i < 8u; i++) {
        r->pads[i].level = (uint8_t)drum_pad_mix_get(i, DRUM_MIX_LEVEL);
        r->pads[i].pan = (int8_t)drum_pad_mix_get(i, DRUM_MIX_PAN);
        r->pads[i].mute = (uint8_t)drum_pad_mix_get(i, DRUM_MIX_MUTE);
        r->pads[i].tune = (int8_t)drum_pad_edit_get(i, DRUM_EDIT_TUNE);
        r->pads[i].end = (uint8_t)drum_pad_edit_get(i, DRUM_EDIT_END);
        r->pads[i].decay = (uint8_t)drum_pad_edit_get(i, DRUM_EDIT_DECAY);
    }
}

static void drumrec_apply_live_with_raw(const drumrec_t *r, const int8_t *raw)
{
    uint32_t i;
    drum_qtz_strength_set(0); /* DRM2 restores raw data only; QTZ is not represented on wire */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    drum_mute = 0;
    fm1_irq_off();
    if (raw) memcpy(drum_raw_offset, raw, sizeof drum_raw_offset);
    else drum_raw_offset_clear();                  /* DRM1/FUN loads have raw offsets zero */
    song.g[G_BPM] = (int16_t)r->bpm;
    song.g[G_SWING] = (int16_t)r->swing;
    for (i = 0; i < 11u; i++) {
        uint32_t id = drumrec_track_fields[i];
        const param_desc_t *d = id == P_E0 ? &DRUM_KIT_DESC : &TP[id];
        TDRUM->p[id] = (int16_t)(id == P_E0 ? drum_preset_compat_id(r->project_fields[i]) :
                                clamp(r->project_fields[i], d->min, d->max));
    }
    for (i = 0; i < 5u; i++) {
        uint32_t id = drumrec_global_fields[i];
        song.g[id] = (int16_t)clamp(r->project_fields[11u + i], GP[id].min, GP[id].max);
    }
    TDRUM->p[P_SLEN] = r->pattern_length;
    memcpy(TDRUM->dstep, r->steps, sizeof r->steps);
    song.sel = TRK_DRUM;
    drum_pad_sel = r->selected_pad;
    fm1_irq_on();
    drum_legacy_preset_apply(r->project_fields[10]);
    memset(drum_user_valid, 0, sizeof drum_user_valid);
    memset(drum_user_pads, 0, sizeof drum_user_pads);
    memset(drum_user_fx, 0, sizeof drum_user_fx);
    for (i = 0; i < 8u; i++) {
        drum_pad_mix_set(i, DRUM_MIX_LEVEL, r->pads[i].level);
        drum_pad_mix_set(i, DRUM_MIX_PAN, r->pads[i].pan);
        drum_pad_mix_set(i, DRUM_MIX_MUTE, r->pads[i].mute);
        drum_pad_edit_set(i, DRUM_EDIT_TUNE, r->pads[i].tune);
        drum_pad_edit_set(i, DRUM_EDIT_START, 0);
        drum_pad_edit_set(i, DRUM_EDIT_END, r->pads[i].end);
        drum_pad_edit_set(i, DRUM_EDIT_DECAY, r->pads[i].decay);
    }
    /* Legacy indices refer to the historical sixteen-kit map, never the six
     * RC0 factories. Keep a truthful CUSTOM marker after restoring edits. */
    drum_kit_source_set(DRUM_KIT_SOURCE_CUSTOM);
}

static void drumrec_apply_live(const drumrec_t *r) { drumrec_apply_live_with_raw(r, 0); }

static void drumrec3_apply_live(const drumrec3_t *r)
{
    uint8_t valid[DRUM_USER_KITS], pads[DRUM_USER_KITS][DRUM_RAW_LANES][8];
    uint32_t i, k;
    drumrec_apply_live_with_raw(&r->base.base, &r->base.raw_offsets[0][0]);
    /* Apply the new live assignments after the compatibility factory select.
     * The existing sample voices and pad controls remain the only playback path. */
    for (i = 0; i < DRUMREC_PADS; i++) {
        drum_sample_reference_set(i, r->sample_ids[i], r->sample_refs[i]);
        drum_pad_mix_set(i, DRUM_MIX_LEVEL, r->base.base.pads[i].level);
        drum_pad_mix_set(i, DRUM_MIX_PAN, r->base.base.pads[i].pan);
        drum_pad_mix_set(i, DRUM_MIX_MUTE, r->base.base.pads[i].mute);
        drum_pad_edit_set(i, DRUM_EDIT_TUNE, r->base.base.pads[i].tune);
        drum_pad_edit_set(i, DRUM_EDIT_START, 0);
        drum_pad_edit_set(i, DRUM_EDIT_END, r->base.base.pads[i].end);
        drum_pad_edit_set(i, DRUM_EDIT_DECAY, r->base.base.pads[i].decay);
        drum_pad_edit_set(i, DRUM_EDIT_START, r->starts[i]);
    }
    memset(valid, 0, sizeof valid); memset(pads, 0, sizeof pads);
    for (k = 0; k < DRUM_USER_KITS; k++) {
        valid[k] = r->user_valid[k];
        for (i = 0; i < DRUMREC_PADS; i++) {
            const drumrec3_pad_t *p = &r->user_kits[k][i];
            pads[k][i][0] = p->sample_id; pads[k][i][1] = p->level; pads[k][i][2] = (uint8_t)p->pan;
            pads[k][i][3] = p->mute; pads[k][i][4] = (uint8_t)p->tune; pads[k][i][5] = p->end;
            pads[k][i][6] = p->decay; pads[k][i][7] = r->user_starts[k][i];
        }
    }
    drum_user_kit_import(valid, pads);
    drum_sample_refs_import(r->sample_refs,r->user_sample_refs);
    if (r->map_version == DRUM_FACTORY_MAP_VERSION) {
        for (i = 0; i < 9u; i++) drum_fx_restore(i, &r->fx[i]);
        drum_user_fx_import(r->user_fx);
        drum_kit_source_set(r->source_id);
    } else {
        /* DRM3 carried historical identity 0, but its exact assignments win. */
        drum_kit_source_set(DRUM_KIT_SOURCE_CUSTOM);
    }
}

static void drumrec3_capture_live(drumrec3_t *r)
{
    uint8_t valid[DRUM_USER_KITS], pads[DRUM_USER_KITS][DRUM_RAW_LANES][8];
    uint32_t i, k;
    memset(r, 0, sizeof *r);
    drumrec_capture_live(&r->base.base);
    memcpy(r->base.raw_offsets, drum_raw_offset, sizeof r->base.raw_offsets);
    r->source_id = (uint8_t)drum_kit_source_get();
    r->map_version = DRUM_FACTORY_MAP_VERSION;
    for (i = 0; i < 9u; i++) r->fx[i] = drum_fx_get(i);
    drum_user_fx_export(r->user_fx);
    drum_sample_refs_export(r->sample_refs,r->user_sample_refs);
    for (i = 0; i < DRUMREC_PADS; i++) {
        r->sample_ids[i] = (uint8_t)drum_custom_sample_id_get(i);
        r->starts[i] = (uint8_t)drum_pad_edit_get(i, DRUM_EDIT_START);
    }
    drum_user_kit_export(valid, pads);
    for (k = 0; k < DRUM_USER_KITS; k++) {
        r->user_valid[k] = valid[k];
        for (i = 0; i < DRUMREC_PADS; i++) {
            drumrec3_pad_t *p = &r->user_kits[k][i];
            p->sample_id = pads[k][i][0]; p->level = pads[k][i][1]; p->pan = (int8_t)pads[k][i][2];
            p->mute = pads[k][i][3]; p->tune = (int8_t)pads[k][i][4]; p->end = pads[k][i][5];
            p->decay = pads[k][i][6]; p->reserved = 0; r->user_starts[k][i] = pads[k][i][7];
        }
    }
}

static void drumrec_legacy_pad_defaults(void)
{
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        drum_pad_mix_set(i, DRUM_MIX_LEVEL, 127);
        drum_pad_mix_set(i, DRUM_MIX_PAN, 0);
        drum_pad_mix_set(i, DRUM_MIX_MUTE, 0);
        drum_pad_edit_set(i, DRUM_EDIT_START, 0);
        drum_pad_edit_set(i, DRUM_EDIT_TUNE, 0);
        drum_pad_edit_set(i, DRUM_EDIT_END, 100);
        drum_pad_edit_set(i, DRUM_EDIT_DECAY, 0);
    }
    drum_pad_sel = 0;
    drum_kit_source_set(DRUM_KIT_SOURCE_CUSTOM);
}

/* Shared policy seam, kept outside the flash implementation so host tests can
 * prove DRM1's inability to preserve nonzero volatile offsets. */
int project_raw_timing_safe(void) { return !drum_raw_offset_any(); }

enum { PROJECT_COPY_ERASED=0, PROJECT_COPY_DRM1=1, PROJECT_COPY_DRM2=2, PROJECT_COPY_DRM3=3, PROJECT_COPY_DRM4=4, PROJECT_COPY_DRM5=5, PROJECT_COPY_DRM6=6, PROJECT_COPY_BAD=7 };
/* Shared A/B format policy seam. Values describe independently validated
 * copies; BAD covers corrupt, unknown, unreadable and malformed records. */
int project_drm2_write_format(uint8_t a, uint8_t b, int allow_drm1, int raw_nonzero)
{
    if(a>PROJECT_COPY_DRM2 || b>PROJECT_COPY_DRM2 || a==PROJECT_COPY_BAD || b==PROJECT_COPY_BAD) return 0;
    if((a==PROJECT_COPY_DRM1 && b==PROJECT_COPY_DRM2) ||
       (a==PROJECT_COPY_DRM2 && b==PROJECT_COPY_DRM1)) return 0;
    if(a==PROJECT_COPY_DRM2 || b==PROJECT_COPY_DRM2) return 2;
    if(a==PROJECT_COPY_DRM1 || b==PROJECT_COPY_DRM1)
        return allow_drm1 && !raw_nonzero ? 1 : 0;
    return 2;
}

/* RC0 saves DRM4 in the same object. A valid compact legacy copy may remain
 * as rollback while the new copy commits. FUN, corrupt and unknown copies
 * remain fail-closed; no partition, protected sector, or low-level writer changes. */
static int project_drm3_write_format(uint8_t a, uint8_t b, int allow_drm1, int raw_nonzero)
{
    (void)raw_nonzero;
    if (a > PROJECT_COPY_DRM6 || b > PROJECT_COPY_DRM6) return 0;
    if ((a == PROJECT_COPY_DRM1 || b == PROJECT_COPY_DRM1) && !allow_drm1 &&
        a < PROJECT_COPY_DRM2 && b < PROJECT_COPY_DRM2) return 0;
    return 4; /* writes always preserve assignments, identity and every FX config */
}

/* Pure wear-interval policy seam shared with focused host regression. A boot
 * has not written a snapshot yet, regardless of the last session's journal. */
#define AUTOSAVE_IDLE 2500u
#define AUTOSAVE_GAP 20000u
static int project_autosave_write_due(uint32_t now, uint32_t last, uint32_t attempted)
{
    return !attempted || (uint32_t)(now - last) >= AUTOSAVE_GAP;
}

#define PROJECT_RECORD_SIZE DRUMREC6_SIZE
#define PROJECT_RECORD_VERSION 6
#define project_record_encode drumrec6_encode
#define project_record_decode drumrec6_decode
#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;           /* live sections / the song: in RAM, not yet in flash */
#if FELUCCA_FLASH
/* slot from flash into RAM (format 5, or an old one converted) */
static union {
    project_t v6;
    project_v5_t v5;
    project_v4_t v4;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} proj_tmp;
static drumrec3_t drumrec3_buf __attribute__((section(".pool")));
static uint8_t project_wire[PROJECT_RECORD_SIZE];
_Static_assert(PROJECT_RECORD_SIZE <= ST_PAYLOAD_MAX, "Compact project fits existing 3840-byte payload");
_Static_assert(sizeof(project_t) >= PROJECT_RECORD_SIZE, "project staging union fits latest drum record");
/* 0 erased, 1 FUN, 2 DRM1, 3 unreadable/ambiguous, 4 RAM section, 5 DRM2, 6 DRM3, 7 DRM4, 8 DRM5. */
static uint8_t proj_kind[4];

/* Read-only diagnosis and backup-gated repair around unchanged storage.c. */
#include "persistence_diag.h"
static uint32_t save_flash_jedec;
static int save_wire_format(const void *wire,uint32_t n)
{
    if(n==DRUMREC6_SIZE&&drumrec6_decode(&drumrec3_buf,wire,n))return 6;
    if(n==DRUMREC5_SIZE&&drumrec5_decode(&drumrec3_buf,wire,n))return 5;
    if(n==DRUMREC4_SIZE&&drumrec4_decode(&drumrec3_buf,wire,n))return 4;
    if(n==DRUMREC3_SIZE&&drumrec3_decode(&drumrec3_buf,wire,n))return 3;
    if(n==DRUMREC2_SIZE&&drumrec2_decode(&drumrec3_buf.base,wire,n))return 2;
    if(n==DRUMREC_SIZE&&drumrec_decode(&drumrec3_buf.base.base,wire,n))return 1;
    return 0;
}
static int project_destination_format(uint32_t obj,int allow_drm1)
{ return save_destination_format(obj,allow_drm1,!project_raw_timing_safe()); }
/* Upload/delete must not break live projects or any rollback copy. Unknown,
 * damaged or FUN data cannot prove absence of a dependency and stays closed. */
static int project_sample_slot_referenced(uint32_t slot)
{
    uint32_t obj,copy,lane,kit;save_copy_report_t report[2];
    if(slot>=DRUM_USER_SAMPLE_COUNT)return -1;
    for(obj=OBJ_PROJECT0;obj<=OBJ_AUTOSAVE;obj++) {
        if(!save_object_ok(obj))continue;
        if(!save_pair_report(obj,report))return -1;
        for(copy=0;copy<2;copy++) {
            int n;
            if(report[copy].state==ST_STATE_ERASED)continue;
            if(report[copy].state!=ST_STATE_VALID||!report[copy].format)return -1;
            if(report[copy].format<6u)continue;
            n=st_load_copy(obj,copy,&proj_tmp,sizeof proj_tmp);
            if(n!=(int)DRUMREC6_SIZE||!drumrec6_decode(&drumrec3_buf,&proj_tmp,(size_t)n))return -1;
            for(lane=0;lane<8u;lane++) {
                if(drumrec3_buf.sample_ids[lane]==55u+slot)return 1;
                for(kit=0;kit<4u;kit++)if(drumrec3_buf.user_valid[kit]&&drumrec3_buf.user_kits[kit][lane].sample_id==55u+slot)return 1;
            }
        }
    }
    return 0;
}
static int project_compact_destination(uint32_t obj,int format)
{
    int allowed;
    if(save_backup.active&&save_backup.object==obj&&!save_backup.authorized) {
        save_error(SAVE_ERR_NO_SAFE_RECOVERY_PATH,-1,obj,255,SAVE_STAGE_RECOVERY);return 0;
    }
    allowed=project_destination_format(obj,obj!=OBJ_AUTOSAVE||format==1);
    return allowed==format||(allowed==PROJECT_RECORD_VERSION&&format>=1&&format<=PROJECT_RECORD_VERSION);
}
static int project_store_wire(uint32_t obj,const void *wire,uint32_t n,int format)
{
    if(!wire||format<1||format>PROJECT_RECORD_VERSION||n!=save_version_size((uint32_t)format)) {
        save_error(SAVE_ERR_ENCODE_FAILED,-1,obj,255,SAVE_STAGE_CLASSIFY);return -101;
    }
    if(!project_compact_destination(obj,format))return -102;
    if(wire!=project_wire)memcpy(project_wire,wire,n);
    if(save_wire_format(project_wire,n)!=format||(format==1&&!project_raw_timing_safe())) {
        save_error(SAVE_ERR_ENCODE_FAILED,-1,obj,255,SAVE_STAGE_CLASSIFY);return -101;
    }
    return save_commit(obj,project_wire,n);
}

static int project_store_live(uint32_t obj, int allow_drm1)
{
    int format=project_destination_format(obj,allow_drm1);
    uint32_t n;
    drumrec3_capture_live(&drumrec3_buf);
    if(format==PROJECT_RECORD_VERSION) {
        n=(uint32_t)project_record_encode(project_wire,sizeof project_wire,&drumrec3_buf);
    } else if(format==4) {
        n=(uint32_t)drumrec4_encode(project_wire,sizeof project_wire,&drumrec3_buf);
    } else if(format==1) {
        if(!project_raw_timing_safe()) return -1;
        n=(uint32_t)drumrec_encode(project_wire,sizeof project_wire,&drumrec3_buf.base.base);
    } else if(format==2) {
        n=(uint32_t)drumrec2_encode(project_wire,sizeof project_wire,&drumrec3_buf.base);
    } else return -1;
    if(!n){save_error(SAVE_ERR_ENCODE_FAILED,-1,obj,255,SAVE_STAGE_CLASSIFY);return -101;}
    return project_store_wire(obj,project_wire,n,format);
}

static int project_store_drm2(uint32_t obj, const void *wire, uint32_t n)
{
    if(n!=DRUMREC2_SIZE || !wire || !drumrec2_decode(&drumrec3_buf.base,wire,n) ||
       st_object_state(obj,0)!=ST_STATE_ERASED) return -1;
    if(wire!=project_wire) memcpy(project_wire,wire,DRUMREC2_SIZE);
    return save_commit(obj,project_wire,DRUMREC2_SIZE);
}
static int project_store_drm1_restore(uint32_t obj, const void *wire, uint32_t n)
{
    if(n!=DRUMREC_SIZE || !wire || st_object_state(obj,0)!=ST_STATE_ERASED ||
       !drumrec_decode(&drumrec3_buf.base.base,wire,n)) return -1;
    memcpy(project_wire,wire,DRUMREC_SIZE);
    return save_commit(obj,project_wire,DRUMREC_SIZE);
}
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    uint8_t state = st_object_state(OBJ_PROJECT0 + (slot & 3u), 0);
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_tmp, sizeof proj_tmp);
    if(n==(int)DRUMREC6_SIZE&&drumrec6_decode(&drumrec3_buf,&proj_tmp,(size_t)n)) {
        q->magic=0;proj_kind[slot & 3u]=9;
    } else
    if (n == (int)DRUMREC5_SIZE && drumrec5_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        q->magic = 0;
        proj_kind[slot & 3u] = 8;
    } else if (n == (int)DRUMREC4_SIZE && drumrec4_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        q->magic = 0;
        proj_kind[slot & 3u] = 7;
    } else if (n == (int)DRUMREC3_SIZE && drumrec3_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        q->magic = 0;
        proj_kind[slot & 3u] = 6;
    } else if (n == (int)DRUMREC2_SIZE && drumrec2_decode(&drumrec3_buf.base, &proj_tmp, (size_t)n)) {
        q->magic = 0;
        proj_kind[slot & 3u] = 5;
    } else if (n == (int)DRUMREC_SIZE && drumrec_decode(&drumrec3_buf.base.base, &proj_tmp, (size_t)n)) {
        q->magic = 0;
        proj_kind[slot & 3u] = 2;
    } else if (proj_import(q, &proj_tmp, n)) {
        proj_kind[slot & 3u] = 1;
    } else {
        q->magic = 0;
        proj_kind[slot & 3u] = state == ST_STATE_ERASED ? 0 : 3;
    }
}
#endif

static int project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return 0; }
#endif
#if FELUCCA_FLASH
    /* A hardware build must never promote a RAM-only snapshot to SAVED/KIT
     * SAVED when the existing flash readiness check failed. */
    if(!flash_ok) {
        save_error(SAVE_ERR_FLASH_NOT_READY,-1,OBJ_PROJECT0+(slot&3u),255,SAVE_STAGE_CLASSIFY);
        ui_message("NO SAVE: FLASH");
        return 0;
    }
    if (flash_ok) {
        if (proj_kind[slot & 3u] == 4u) {
            ui_message("SECTION RAM ONLY");
            return 0;
        }
        if (project_store_live(OBJ_PROJECT0 + (slot & 3u), 1)) {
            /* Preserve the original error/stage; reclassification here used to
             * overwrite PROGRAM_FAILED with a secondary torn-copy error. */
            ui_message(save_diag.code==SAVE_ERR_DESTINATION_PROTECTED||save_diag.code==SAVE_ERR_UNKNOWN_RECORD||
              save_diag.code==SAVE_ERR_CRC_INVALID||save_diag.code==SAVE_ERR_NO_SAFE_RECOVERY_PATH?"SLOT PROTECTED":"SAVE ERROR");
            return 0;
        }
        p->magic=0;
        proj_kind[slot & 3u]=(uint8_t)(PROJECT_RECORD_VERSION+3u);
        sec_dirty &= (uint8_t)~(1u << (slot & 3u));
        ui_message("SAVED");
        return 1;
    }
#endif
    proj_capture(p);
    ui_message("SAVED (RAM)");
    return 1;
}

/* SAVE > PROJECT > ERASE (SLOOP 2.6, after a YouTube comment): the slot (section A..D) empty again. In flash as the
 * editor's restore leaves an empty slot (project_restore): an object of no bytes, which proj_fetch (older SLOOP too)
 * reads as no project. The song's steps that played that section are taken out (the default song if none is left),
 * so song mode still plays. Stopped only: a flash erase stops the audio ~50 ms */
static void project_erase(uint32_t slot)
{
    slot &= 3u;
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE ERASE"); return; }
#endif
#if FELUCCA_FLASH
    /* Zero-length A/B commits leave the prior payload in the other copy.
     * Until that mixed state has an explicit migration policy, ERASE cannot
     * safely make a legacy or compact destination reusable. */
    if (flash_ok && st_object_state(OBJ_PROJECT0 + slot, 0) != ST_STATE_ERASED) {
        ui_message("SLOT PROTECTED");
        return;
    }
#endif
    if (!proj_ok(&proj_slot[slot])) {
        ui_message("EMPTY SLOT");
        return;
    }
    /* A zero-length SLOOP object is not a DRM1 record. Clearing a RAM-only
     * section must never create one or erase a protected flash copy. */
    fm1_irq_off();
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    memset(section_raw[slot], 0, sizeof section_raw[slot]);
    proj_kind[slot] = 0;
    sec_dirty &= (uint8_t)~(1u << slot);
    if (live_sec == (int8_t)slot)
        live_sec = -1;
#if FELUCCA_ARRANGER
    if (arr_drop_scene(&arrangement, slot))
        song_dirty = 1;                                     /* (saved with the settings, when quiet) */
#endif
    fm1_irq_on();
    ui_message("ERASED");
}

/* Apply a normal project with zero raw offsets, or a RAM-only section with its sidecar timing.
 * The event pattern and offsets become visible atomically to the audio ISR. */
static void project_apply_with_raw(const project_t *p, const int8_t *raw)
{
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    drum_mute = 0;                                      /* (the live mutes: a performance's, not the project's) */
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    if (raw)
        memcpy(drum_raw_offset, raw, sizeof drum_raw_offset);
    else
        drum_raw_offset_clear();                        /* FUN/DRM1 have no raw lane timing */
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    /* FUN records predate START; working restore and manual LOAD both reset it. */
    { uint32_t lane; for(lane=0;lane<8u;lane++) drum_pad_edit_set(lane,DRUM_EDIT_START,0); }
    drum_legacy_preset_apply(pt_get(&p->t[PROJ_DRUM_TRACK], P_E0));
    memset(drum_user_valid, 0, sizeof drum_user_valid);
    memset(drum_user_pads, 0, sizeof drum_user_pads);
    memset(drum_user_fx, 0, sizeof drum_user_fx);
    sync_reload = 1;
    ui.force = 1;
}
static void project_apply(const project_t *p) { project_apply_with_raw(p, 0); }

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok) {
        if (proj_kind[slot & 3u] == 4u) {
            ui_message("SECTION RAM ONLY");
            return;
        }
        proj_fetch(slot);
        if (proj_kind[slot & 3u] == 2u || proj_kind[slot & 3u] == 5u || proj_kind[slot & 3u] == 6u || proj_kind[slot & 3u] == 7u || proj_kind[slot & 3u] == 8u || proj_kind[slot & 3u] == 9u) {
            if (proj_kind[slot & 3u] == 6u || proj_kind[slot & 3u] == 7u || proj_kind[slot & 3u] == 8u || proj_kind[slot & 3u] == 9u)
                drumrec3_apply_live(&drumrec3_buf);
            else if (proj_kind[slot & 3u] == 5u)
                drumrec_apply_live_with_raw(&drumrec3_buf.base.base,&drumrec3_buf.base.raw_offsets[0][0]);
            else
                drumrec_apply_live(&drumrec3_buf.base.base);
            sync_reload = 1;
            ui.force = 1;
            ui_message("LOADED");
            return;
        }
    }
#endif
    if (!proj_ok(p)) {
        ui_message(
#if FELUCCA_FLASH
            flash_ok && proj_kind[slot & 3u] == 3 ? "SLOT DAMAGED" :
#endif
            "EMPTY SLOT");
        return;
    }
    project_apply(p);
    drumrec_legacy_pad_defaults();
    song.sel = TRK_DRUM;
    ui_message("LOADED");
}

/* Working-project journal: no new object or sector. The first changed snapshot
 * can commit after idle; the wear interval applies BETWEEN successful commits,
 * not to the first 20 seconds of every boot/resume. */
static project_t autosave_buf __attribute__((section(".pool")));
static uint8_t autosave_wire[PROJECT_RECORD_SIZE] __attribute__((section(".pool")));
static uint8_t autosave_valid, autosave_written_session, autosave_state;
static uint32_t autosave_ms, autosave_checked, autosave_input_seen;

static int audio_quiet(void)
{
    uint32_t i;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active) return 0;
    return 1;
}
/* Read-only conservative UI indication: 0 WAIT, 1 SAVED, 2 BLOCKED.
 * SAVED means the last quiet-state comparison matched the durable snapshot;
 * any subsequent panel input or transport activity immediately withdraws it. */
static uint32_t autosave_status(void)
{
    if (!flash_ok) return 2u;
    if (song.playing || song.rec || rec_wait || ft_on || transport_req ||
        ui_input_ms != autosave_input_seen || !audio_quiet()) return 0u;
    if(save_backup.active&&save_backup.object==OBJ_AUTOSAVE&&!save_backup.authorized)return 0u;
    return autosave_state;
}
/* Cache only a canonical projection derived from the CRC-verified record,
 * never a post-apply RAM snapshot pretending to be persisted. */
static const char *autosave_error_text(void)
{ return !flash_ok?"FLASH_NOT_READY":save_error_name(save_diag.code); }
static void autosave_remember(const drumrec3_t *persisted)
{
    autosave_valid=(uint8_t)(persisted&&project_record_encode(autosave_wire,sizeof autosave_wire,persisted)==PROJECT_RECORD_SIZE);
    autosave_state=0;autosave_input_seen=ui_input_ms;
    if(autosave_valid) {
        drumrec3_capture_live(&drumrec3_buf);
        if(project_record_encode(project_wire,sizeof project_wire,&drumrec3_buf)==PROJECT_RECORD_SIZE&&
           !memcmp(project_wire,autosave_wire,PROJECT_RECORD_SIZE))autosave_state=1;
    }
}
static void autosave_write(int force_idle)
{
#if FELUCCA_FLASH
    if (!flash_ok) { autosave_state = 2;save_error(SAVE_ERR_FLASH_NOT_READY,-1,OBJ_AUTOSAVE,255,SAVE_STAGE_CLASSIFY); return; }
    if (song.playing || song.rec || rec_wait || ft_on || transport_req || !audio_quiet() ||
        (!force_idle && fm1_ms - ui_input_ms < AUTOSAVE_IDLE)) { autosave_state = 0; return; }
    if(save_backup.active&&save_backup.object==OBJ_AUTOSAVE&&!save_backup.authorized){autosave_state=0;return;}
    if (fm1_ms - autosave_checked < 250u) return;
#if FELUCCA_ARRANGER
    if (srec) return;
#endif
    autosave_checked = fm1_ms;
    fm1_irq_off();
    drumrec3_capture_live(&drumrec3_buf);
    fm1_irq_on();
    if (project_record_encode(project_wire, sizeof project_wire, &drumrec3_buf) != PROJECT_RECORD_SIZE) {
        autosave_state = 2;save_error(SAVE_ERR_ENCODE_FAILED,-1,OBJ_AUTOSAVE,255,SAVE_STAGE_CLASSIFY); return;
    }
    autosave_input_seen = ui_input_ms;
    if (autosave_valid && !memcmp(autosave_wire, project_wire, PROJECT_RECORD_SIZE)) {
        autosave_state = 1; return;
    }
    if (!project_autosave_write_due(fm1_ms, autosave_ms, autosave_written_session)) {
        if (autosave_state != 2u) autosave_state = 0;
        return;
    }
    autosave_state = 0;
    /* A failed/protected destination is never erased to make autosave succeed.
     * Retry diagnosis no faster than the normal wear interval. */
    if (project_store_wire(OBJ_AUTOSAVE, project_wire, PROJECT_RECORD_SIZE, PROJECT_RECORD_VERSION) == 0) {
        memcpy(autosave_wire, project_wire, PROJECT_RECORD_SIZE);
        autosave_valid = autosave_written_session = 1;
        autosave_state = 1;
    } else {
        autosave_state = 2;
        autosave_written_session = 1; /* also throttle failures, without a write */
    }
    autosave_ms = fm1_ms;
#endif
}
static void autosave_tick(void) { autosave_write(0); }

static uint8_t drch_seed;                           /* a settings object as 2.5 wrote it: the drum channel was the
                                                     * project's then (autosave_resume takes it from a 2.5 autosave) */
static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if(n==(int)DRUMREC6_SIZE&&drumrec6_decode(&drumrec3_buf,&proj_tmp,(size_t)n)) {
        drumrec3_apply_live(&drumrec3_buf);autosave_remember(&drumrec3_buf);return;
    }
    if (n == (int)DRUMREC5_SIZE && drumrec5_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        drumrec3_apply_live(&drumrec3_buf);
        autosave_remember(&drumrec3_buf);
        return;
    }
    if (n == (int)DRUMREC4_SIZE && drumrec4_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        drumrec3_apply_live(&drumrec3_buf);
        autosave_remember(&drumrec3_buf);
        return;
    }
    if (n == (int)DRUMREC3_SIZE && drumrec3_decode(&drumrec3_buf, &proj_tmp, (size_t)n)) {
        autosave_valid = 0;
        drumrec3_apply_live(&drumrec3_buf);
        autosave_remember(&drumrec3_buf);
        return;
    }
    if (n == (int)DRUMREC2_SIZE && drumrec2_decode(&drumrec3_buf.base, &proj_tmp, (size_t)n)) {
        autosave_valid = 0;
        drumrec_apply_live_with_raw(&drumrec3_buf.base.base,&drumrec3_buf.base.raw_offsets[0][0]);
        autosave_valid=0;autosave_state=0;
        return;
    }
    if (n == (int)DRUMREC_SIZE && drumrec_decode(&drumrec3_buf.base.base, &proj_tmp, (size_t)n)) {
        autosave_valid = 0;
        drumrec_apply_live(&drumrec3_buf.base.base); /* DRM1 has no raw timing */
        autosave_valid=0;autosave_state=0;
        return;
    }
    if (!proj_import(q, &proj_tmp, n))
        return;
    /* FUN1..FUN6 autosaves remain readable, but the destination guard below
     * will never convert their original bytes implicitly. */
    autosave_valid = 0;
    autosave_ms = fm1_ms;
    if (drch_seed && !(q->rsv[0] & 0x80u) && q->g[G_DRCH] >= 0 && q->g[G_DRCH] <= 16 && q->g[G_DRCH] != midi_set[3]) {
        midi_set[3] = (uint8_t)q->g[G_DRCH];          /* (2.6) updated from 2.5: GLO > DRUMS > CH as it was there (a
                                                     * project's then, a setting of the FM-1 now), kept in the settings */
        settings_later = 1;
    }
    drch_seed = 0;
    proj_apply(q, 1);
    drum_legacy_preset_apply(pt_get(&q->t[PROJ_DRUM_TRACK], P_E0));
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    TDRUM->engine = TDRUM->eng_req = 0;
    autosave_valid=0;autosave_state=0;
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t lights;                               /* SLOOP 2.3: the backlight (panel.c lights_word); appended,
                                                    * so 2.2 still reads its part (st_load cuts at its size) */
} persist_t;
#define PERSIST_SIZE_V22 __builtin_offsetof(persist_t, lights)   /* the settings as 2.2 wrote them (no lights) */
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V22 + 4u, "lights: the last word, no padding before it");
#if FELUCCA_ARRANGER
_Static_assert(__builtin_offsetof(persist_t, arrangement) == 48u && sizeof(arr_config_t) == 36u,
               "the web editor's Song page edits bytes 48..83 of the settings (web/editor.html ARR)");
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
/* Keep the historical settings object's SYN-bank byte range as zeroed reserved
 * bytes. This preserves all following MIDI offsets without retaining synth RAM. */
#define LEGACY_SYN_BANK_SIZE 1464u
static const uint8_t legacy_syn_bank[LEGACY_SYN_BANK_SIZE] = {0};
static void legacy_syn_bank_boot(void);
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    save_flash_jedec = FL_FAR(fl_jedec_ram)();
    flash_ok = save_flash_jedec == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < DRUM_USER_SAMPLE_COUNT; k++)
            drum_user_sample_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V22 && p.magic == PERSIST_MAGIC)
            p.lights = 0;                          /* from 2.2: backlight off */
        if (((n == (int)sizeof p || n == (int)PERSIST_SIZE_V22) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
            if (p.magic == PERSIST_MAGIC)
                lights_from_word(p.lights);
            else
                p.lights = 0;
#if FELUCCA_ARRANGER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                arrangement = p.arrangement;
            else
                p.arrangement = arrangement;
#endif
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on. A slot
         * still valid in RAM (a warm reset: an update, UPDATE MODE, a crash) may never have reached
         * flash (a live section stored while playing): marked to be written when quiet */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i])) {
                proj_fetch(i);
            } else {
                uint8_t state = st_object_state(OBJ_PROJECT0 + i, 0);
                proj_kind[i] = state == ST_STATE_ERASED ? 0 : 3;
                /* Warm .noinit RAM can be newer than flash, but must never
                 * trigger an implicit rewrite of a FUN/compact/corrupt slot. */
                if (state == ST_STATE_ERASED) {
                    proj_kind[i] = 4;          /* warm RAM-only section; never claim it was persisted */
                    sec_dirty |= (uint8_t)(1u << i);
                }
            }
    }
    legacy_syn_bank_boot();                         /* retain the old settings object's opaque byte range */
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot)
{
#if FELUCCA_FLASH
    if (flash_ok && proj_kind[slot & 3u]) return 1;
#endif
    return proj_ok(&proj_slot[slot & 3u]);
}

#if FELUCCA_FLASH
/* The settings object keeps its historical 1464-byte SYN-bank range after persist_t as opaque bytes, then the MIDI LEARN
 * map (seq.c ml_tab, ML_N entries), only while something is learned; 2.5 reads an object with the map as one without
 * its SYN kits (their defaults), one without it as its own */
/* the settings object's third part (2.6): the MIDI LEARN map (seq.c ml_tab, ML_N entries, 2 bytes each, low first),
 * then GLO > MIDI: CH1 CH2 CH3, the drum channel | OTHER << 7, GLO > MIDI 2's BEND, 3 spare. Written only while it is not
 * the default (nothing learned, channels 1 2 3 10, OTHER SEL, BEND 2): else the object is exactly 2.5's. 2.6 test
 * builds wrote 32 (the map) or 36 bytes (and the channels) */
#define MIDI_PART (2u * ML_N + 8u)
_Static_assert(sizeof(persist_t) + LEGACY_SYN_BANK_SIZE + MIDI_PART <= ST_PAYLOAD_MAX, "the settings object fits its sector");
static uint8_t midi_saved[MIDI_PART];
static uint8_t drch_seed;                           /* a settings object as 2.5 wrote it: the drum channel was the
                                                     * project's then (autosave_resume takes it from a 2.5 autosave) */               /* the part as flash has it (settings_save: written when it differs) */
static void midi_part_fill(uint8_t *b)
{
    uint32_t k;
    for (k = 0; k < ML_N; k++)
        b[2u * k] = (uint8_t)ml_tab[k], b[2u * k + 1u] = (uint8_t)(ml_tab[k] >> 8);
    b[2u * ML_N] = midi_set[0], b[2u * ML_N + 1u] = midi_set[1], b[2u * ML_N + 2u] = midi_set[2];
    b[2u * ML_N + 3u] = (uint8_t)(midi_set[3] | midi_set[4] << 7);
    b[2u * ML_N + 4u] = midi_set[5];
    b[2u * ML_N + 5u] = b[2u * ML_N + 6u] = b[2u * ML_N + 7u] = 0;
}
static int midi_part_default(const uint8_t *b)
{
    static const uint8_t def[6] = MIDI_SET_DEF;
    uint32_t k;
    for (k = 0; k < 2u * ML_N; k++)
        if (b[k])
            return 0;
    return b[2u * ML_N] == def[0] && b[2u * ML_N + 1u] == def[1] && b[2u * ML_N + 2u] == def[2] &&
           b[2u * ML_N + 3u] == (uint8_t)(def[3] | def[4] << 7) && b[2u * ML_N + 4u] == def[5];
}
/* n: 32 (the map), 36 (and the channels) or MIDI_PART; every entry checked (a CC that learns, a parameter this knows, a
 * channel 0..16, a range 1..12) */
static void midi_part_read(const uint8_t *b, uint32_t n)
{
    uint32_t k, e;
    fm1_irq_off();                                  /* (the ISR reads the map and the channels: never half of them) */
    for (k = 0; k < ML_N; k++) {
        e = (uint32_t)b[2u * k] | (uint32_t)b[2u * k + 1u] << 8;
        ml_tab[k] = (uint16_t)((e >> 9) && ml_free_cc(e & 127u) && ml_id(e >> 9) < P_COUNT ? e : 0u);
    }
    if (n >= 2u * ML_N + 4u) {
        for (k = 0; k < 3u; k++)
            midi_set[k] = (uint8_t)(b[2u * ML_N + k] <= 16u ? b[2u * ML_N + k] : k + 1u);
        midi_set[3] = (uint8_t)((b[2u * ML_N + 3u] & 31u) <= 16u ? b[2u * ML_N + 3u] & 31u : 10u);
        midi_set[4] = (uint8_t)(b[2u * ML_N + 3u] >> 7);
        song.g[G_CH1] = midi_set[0], song.g[G_CH2] = midi_set[1], song.g[G_CH3] = midi_set[2];
        song.g[G_DRCH] = midi_set[3], song.g[G_CHOTH] = midi_set[4];
    }
    if (n >= MIDI_PART) {
        midi_set[5] = (uint8_t)(b[2u * ML_N + 4u] >= 1u && b[2u * ML_N + 4u] <= 12u ? b[2u * ML_N + 4u] : 2u);
        song.g[G_BEND] = midi_set[5];
    }
    fm1_irq_on();
}
static int settings_write(const persist_t *p)
{
    uint8_t mp[MIDI_PART];
    midi_part_fill(mp);
    /* Keep the 2.5 object byte layout and old opaque bank unchanged. */
    if (st_save3(OBJ_SETTINGS, p, sizeof *p, legacy_syn_bank, sizeof legacy_syn_bank,
                 mp, midi_part_default(mp) ? 0u : sizeof mp))
        return -1;
    memcpy(midi_saved, mp, sizeof midi_saved);
    return 0;
}
static int legacy_syn_bank_valid(const uint8_t *b)
{
    return b[0] == 'D' && b[1] == 'S' && b[2] == 'U' && b[3] == '1' &&
           b[4] == 1u && b[5] == 0u && b[6] == 4u && b[7] == 0u;
}
static void legacy_syn_bank_boot(void)
{
    st_hdr_t h;
    uint32_t len = sizeof(persist_t) + LEGACY_SYN_BANK_SIZE, k;
    for (k = 0; k < ML_N; k++)
        ml_tab[k] = 0;
    if (st_current(OBJ_SETTINGS, &h) >= 0 &&
        (h.len == len || h.len == len + 2u * ML_N || h.len == len + 2u * ML_N + 4u || h.len == len + MIDI_PART) &&
        legacy_syn_bank_valid(st_buf + sizeof(persist_t))) {
        /* Validate the retired bank marker only to retain old MIDI-tail semantics;
         * the synth bytes are not imported and are zeroed on the next settings save. */
        if (h.len > len)
            midi_part_read(st_buf + len, h.len - len);
        drch_seed = h.len == len;
    }
    midi_part_fill(midi_saved);
}
#endif

static void persist_fill(persist_t *p)              /* the settings as they are now */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom;
    p->panel = panel;
    p->lights = lights_word();
#if FELUCCA_ARRANGER
    p->arrangement = arrangement;
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    persist_fill(&p);
    {
        uint8_t mp[MIDI_PART];
        midi_part_fill(mp);
        if (!memcmp(&p, &persist_saved, sizeof p) && !memcmp(mp, midi_saved, sizeof mp))
            return;                                /* unchanged: no erase cycle */
    }
    if (settings_write(&p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");

/* ---- backup restore (editor.c BK_PUT): each object checked as a load checks it, then written through the
 * same A/B commit as a save. rc: 0 ok, 2 not a valid object, 3 stop the song first, 4 flash */
static int panel_valid(const panel_t *q)           /* a permutation of the buttons and of the knobs */
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

static uint32_t settings_restore(const void *raw, uint32_t n)
{
    persist_t p;
    if (n != sizeof p && n != PERSIST_SIZE_V22)
        return 2;
    memset(&p, 0, sizeof p);
    memcpy(&p, raw, n);
    if (p.magic != PERSIST_MAGIC || p.palette >= NPALETTES || p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
        return 2;
#if FELUCCA_ARRANGER
    if (!arr_valid(&p.arrangement, 15u))
        return 2;
    if ((song.playing || transport_req) && memcmp(&p.arrangement, &arrangement, sizeof arrangement))
        return 3;                                    /* SLOOP 2.5: a new song order only when stopped (as on the SONG screen) */
#endif
    if (!flash_ok || settings_write(&p))
        return 4;
    persist_saved = p;
    settings.palette = p.palette;
    settings.lowcut = p.lowcut;
    settings.zoom = p.zoom;
    panel = p.panel;
#if FELUCCA_ARRANGER
    arrangement = p.arrangement;
#endif
    lights_from_word(p.lights);
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
    ui.force = 1;
    return 0;
}

/* Drum protocol v1 has an explicit confirmed restore action. Unlike the legacy
 * receiver it may replace a recognized DRM slot, through the same guarded A/B
 * writer as SAVE. Unknown/FUN/corrupt pairs retain their existing policy.
 * This does not apply a slot backup to the current live project. */
static uint32_t project_editor_restore(uint32_t slot,const void *raw,uint32_t n)
{
    int format;
    if(slot>=4u||!raw||n>sizeof proj_tmp)return 2;
    if(song.playing||transport_req)return 3;
    if(!flash_ok||proj_kind[slot]==4u)return 4;
    format=save_wire_format(raw,n);
    if(!format)return 2;
    if(project_store_wire(OBJ_PROJECT0+slot,raw,n,format))return 4;
    memset(&proj_slot[slot],0,sizeof proj_slot[slot]);
    proj_kind[slot]=(uint8_t)(format==1?2:format+3);
    sec_dirty&=(uint8_t)~(1u<<slot);
    return 0;
}
/* slot 0..3 (n 0: empty), or 4: the working project (loaded now) */
static uint32_t project_restore(uint32_t slot, const void *raw, uint32_t n)
{
    drumrec3_t *decoded=&drumrec3_buf;
    if (slot > 4u || n > sizeof proj_tmp || (n && !raw))
        return 2;
    if (song.playing || transport_req)
        return 3;
    if (n == 0u && slot < 4u) {
        if (!flash_ok || proj_kind[slot] == 4u || st_object_state(OBJ_PROJECT0 + slot, 0) != ST_STATE_ERASED)
            return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        memset(section_raw[slot],0,sizeof section_raw[slot]);
        proj_kind[slot] = 0;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    /* The backup receiver has no separate on-device overwrite confirmation:
     * only a fully erased project pair can be its destination. */
    if (slot < 4u && (!flash_ok || proj_kind[slot] == 4u || st_object_state(OBJ_PROJECT0 + slot, 0) != ST_STATE_ERASED))
        return 4;
    if(n==DRUMREC6_SIZE&&drumrec6_decode(decoded,raw,n)) {
        if(slot==4u){drumrec3_apply_live(decoded);return 0;}
        if(project_store_wire(OBJ_PROJECT0+slot,raw,n,6))return 4;
        memset(&proj_slot[slot],0,sizeof proj_slot[slot]);proj_kind[slot]=9;sec_dirty&=(uint8_t)~(1u<<slot);return 0;
    }
    if (n == DRUMREC5_SIZE && drumrec5_decode(decoded, raw, n)) {
        if (slot == 4u) { drumrec3_apply_live(decoded); return 0; }
        if (project_store_wire(OBJ_PROJECT0 + slot, raw, n, 5)) return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        proj_kind[slot] = 8;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (n == DRUMREC4_SIZE && drumrec4_decode(decoded, raw, n)) {
        if (slot == 4u) { drumrec3_apply_live(decoded); return 0; }
        if (project_store_wire(OBJ_PROJECT0 + slot, raw, n, 4)) return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        proj_kind[slot] = 7;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (n == DRUMREC3_SIZE && drumrec3_decode(decoded, raw, n)) {
        if (slot == 4u) {
            drumrec3_apply_live(decoded);
            return 0;
        }
        if (save_commit(OBJ_PROJECT0 + slot, raw, n))
            return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        proj_kind[slot] = 6;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (n == DRUMREC2_SIZE && drumrec2_decode(&decoded->base, raw, n)) {
        if (slot == 4u) {
            drumrec_apply_live_with_raw(&decoded->base.base,&decoded->base.raw_offsets[0][0]);
            return 0;
        }
        if (project_store_drm2(OBJ_PROJECT0 + slot, raw, n))
            return 4;
        memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
        proj_kind[slot] = 5;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    if (n == DRUMREC_SIZE && drumrec_decode(&decoded->base.base, raw, n)) {
        if (slot == 4u) {
            drumrec_apply_live(&decoded->base.base);
            return 0;
        }
        if (project_store_drm1_restore(OBJ_PROJECT0 + slot, raw, n)) return 4;
        memset(&proj_slot[slot],0,sizeof proj_slot[slot]);
        proj_kind[slot]=2;
        sec_dirty &= (uint8_t)~(1u << slot);
        return 0;
    }
    /* FUN1..FUN6 backups remain loadable into the working project, but saving
     * them to a slot would be an unapproved lossy conversion away from DRM1. */
    if (slot < 4u)
        return 2;
    if (!proj_import(&autosave_buf, raw, (int)n))
        return 2;
    project_apply(&autosave_buf);
    return 0;
}
#endif
#if FELUCCA_ARRANGER
static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#if FELUCCA_FLASH
    {
        uint32_t i;
        for (i = 0; i < arrangement.count; i++)
            if (proj_kind[arrangement.entry[i].scene & 3u] == 4u) {
                ui_message("SECTION RAM ONLY");
                return;
            }
    }
#endif
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): captured
 * into RAM at once (playing too). It remains session-only because DRM1 cannot represent the complete
 * project_t snapshot; sections and SONG REC data are not written to flash and are lost on reset. */
static int section_store(uint32_t s)
{
    s &= 3u;
#if FELUCCA_FLASH
    if (flash_ok && st_object_state(OBJ_PROJECT0 + s, 0) != ST_STATE_ERASED) {
        ui_message("SLOT PROTECTED");
        return 0;
    }
#endif
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(&proj_slot[s]);
    section_raw_capture(s);
    proj_kind[s] = 4;                           /* complete section snapshot exists only in RAM */
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint8_t)(1u << s);
    return 1;
}
static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    s &= 3u;
    project_apply_with_raw(&proj_slot[s], &section_raw[s][0][0]);
    live_sec = (int8_t)s;
}
#if FELUCCA_ARRANGER
static int arrangement_ram_only(void)
{
#if FELUCCA_FLASH
    uint32_t i;
    for (i = 0; i < arrangement.count; i++)
        if (proj_kind[arrangement.entry[i].scene & 3u] == 4u) return 1;
#endif
    return 0;
}
#endif
static void sections_write(void)                        /* the dirty sections and song into flash */
{
#if FELUCCA_FLASH
    /* A section is a complete four-track project snapshot. DRM1 deliberately
     * stores only the drum pattern and drum-facing controls, so writing it
     * here would silently drop synth tracks, locks and other project fields.
     * Keep the captured section usable in RAM for this session and refuse its
     * persistent write until the format can represent the full snapshot. */
    if (sec_dirty) {
        sec_dirty = 0;
        ui_message("SECTION FORMAT UNSUPPORTED");
    }
    if (!flash_ok)
#endif
        sec_dirty = 0;
    if (song_dirty && arrangement_ram_only()) {
        song_dirty = 0;
        ui_message("SECTION RAM ONLY");
    } else if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
    /* Forced flush skips the idle debounce, but keeps the minimum write gap,
     * stopped/quiet checks, CRC validation and destination guard. */
    autosave_write(1);
}
static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if ((uint32_t)song.g[G_SYNC] != lights_sync) {      /* GLO > SYSTEM > SYNC: kept with the settings */
        lights_sync = (uint8_t)song.g[G_SYNC];
        settings_later = 1;
    }
    if ((uint32_t)(song.g[G_MIDI] != 0) != lights_mout) {   /* GLO > SYSTEM > MIDI: the same */
        lights_mout = (uint8_t)(song.g[G_MIDI] != 0);
        settings_later = 1;
    }
    if ((uint32_t)song.g[G_ROUTE] != lights_min) {        /* GLO > SYSTEM > IN: the same (2.6: CH n too) */
        lights_min = (uint8_t)song.g[G_ROUTE];
        settings_later = 1;
    }
    {   /* 2.6: GLO > MIDI (CH1 CH2 CH3 OTHER) and GLO > DRUMS > CH: the same */
        static const uint8_t gid[6] = {G_CH1, G_CH2, G_CH3, G_DRCH, G_CHOTH, G_BEND};
        uint32_t k;
        for (k = 0; k < 6u; k++)
            if ((uint32_t)song.g[gid[k]] != midi_set[k]) {
                midi_set[k] = (uint8_t)song.g[gid[k]];
                settings_later = 1;
            }
    }
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((!sec_dirty && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
