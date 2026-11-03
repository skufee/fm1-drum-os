/* SPDX-License-Identifier: GPL-3.0-only */
/* LIVE screens, 240 x 240, in a teenage-engineering-like style: black, four colours (one per
 * track and per knob: blue, green, yellow, orange), white for what you touch, red for recording,
 * big numbers, lowercase labels, four dials at the bottom that show what KNOB 1..4 do.
 * Screens: TRACKS (the performance view), DRUMS (GRID / KIT, pads that flash on each hit), the
 * LAYERS (a function button held: what the 16 white keys and the knobs do now), HOLD (a hold to
 * confirm: clear, save), REC (armed / free take), and unarmed RECORD SETTINGS. Every band is drawn
 * into the canvas only when its
 * signature changed. */
static uint8_t drum_page, drum_group, drum_cursor; /* GRID: lane group 0/1; KIT/browser unchanged */
static uint8_t drum_browser_category, drum_browser_sample, drum_user_slot;
static uint8_t drum_preview_level=100;
static uint8_t rc0_project_arm, rc0_project_slot, rc0_user_arm;
static uint32_t rc0_confirm_ms;
static void rc1_confirm_cancel(void)
{
    rc0_project_arm=rc0_user_arm=0;
    ui.arm=ui.arm_t=0;
}
static uint8_t drum_user_backup_valid[DRUM_USER_KITS];
static uint8_t drum_user_backup_pads[DRUM_USER_KITS][DRUM_RAW_LANES][8];
static uint8_t rec_settings_on;
static uint16_t pad_lit[DRUM_LANES];
#define MVP_DRUM_PADS 8u                        /* expose PAD01..08; SLOOP retains its 16-lane data model */
static const char *const DRUM_BROWSER_CATEGORIES[5] = {"KICK", "SNARE", "HAT", "OH", "PERC"};
static const uint8_t DRUM_BROWSER_FIRST[5] = {14, 39, 0, 26, 29};
static const uint8_t DRUM_BROWSER_COUNT[5] = {12, 16, 14, 3, 10};
static void trk_short_name(uint32_t c, char *b);
static void te_header(const char *title, uint16_t tc, uint32_t *cache);
static int on_drum_page(void) { return !rc0_fx_page && !ui.home && cur_page()->scope == SC_DRUM; }
static void drum_browser_sync(void)
{
    uint32_t id = drum_custom_sample_id_get(drum_pad_sel), i;
    if (id >= drum_custom_sample_count()) id = 0;
    drum_browser_sample = (uint8_t)id;
    for (i = 0; i < 5u; i++)
        if (id >= DRUM_BROWSER_FIRST[i] && id < (uint32_t)DRUM_BROWSER_FIRST[i] + DRUM_BROWSER_COUNT[i]) {
            drum_browser_category = (uint8_t)i;
            return;
        }
}

/* ---------------------------------------------------------------- style --- */
#define TE_G1 RGB(26, 26, 30)            /* tiles */
#define TE_G2 RGB(54, 54, 60)            /* empty steps, dial tracks */
#define TE_G3 RGB(118, 118, 126)         /* labels */
#define TE_G4 RGB(196, 196, 204)         /* secondary text */
#define TE_RED RGB(255, 44, 52)          /* recording, erasing */
static const uint16_t TE_COL[4] = {RGB(40, 124, 255), RGB(30, 204, 112), RGB(255, 198, 24), RGB(255, 98, 26)};
static const uint16_t TE_MID[4] = {RGB(26, 82, 170), RGB(20, 136, 76), RGB(170, 132, 16), RGB(170, 66, 18)};
static const uint16_t TE_DIM[4] = {RGB(14, 40, 86), RGB(10, 66, 38), RGB(86, 66, 8), RGB(86, 32, 8)};
#define TE_DRUM TE_COL[3]

static void te_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)     /* filled circle */
{
    int32_t y, x;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                cv_pset(cx + x, cy + y, c);
}
/* a dial: a 270-degree ring (lit up to the value), a pointer; ratio 0..1000, -1 = a plain ring */
static void te_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a], k;
        uint16_t col = ratio < 0 || i <= end ? c : dim;
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), col);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        te_disc(cx, cy, 2, C_WHITE);
    }
}
static void te_play_icon(int32_t x, int32_t y, int playing)
{
    int32_t i;
    if (playing)
        for (i = 0; i < 12; i++)
            cv_rect(x + i, y + i / 2, 1, 14 - i, C_WHITE);   /* a triangle */
    else
        cv_rect(x, y + 1, 12, 12, TE_G3);
}
static uint32_t studio_hash(uint32_t h, const char *p)
{ while (*p) h = h * 31u + (uint8_t)*p++; return h; }
static void te_lower(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n && s[i]; i++)
        d[i] = s[i] >= 'A' && s[i] <= 'Z' ? (char)(s[i] + 32) : s[i];
    d[i] = 0;
}
static void te_text_c(int32_t cx, int32_t y, const char *s, uint16_t c)   /* centred on cx */
{
    cv_text(cx - text_w(&FONT_S, s) / 2, y, &FONT_S, s, c);
}

/* the dial strip: KNOB 1..4, label + value under each (a dial with no label: an empty column); a
 * message replaces it */
static void te_dials(int32_t y0, const char *const lab[4], const char *const val[4], const int32_t ratio[4],
                     uint32_t sig, uint32_t *cache)
{
    uint32_t k;
    sig = studio_hash(sig, ui.msg_t ? ui.msg : "");
    for (k = 0; k < 4u; k++) {
        sig = studio_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]);
        sig = studio_hash(sig, val[k]);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, ui.msg_t ? 240u - (uint32_t)y0 : 40u, C_BLACK);
    if (ui.msg_t) {                                     /* a message: a white bar */
        cv_rect(0, 10, 240, 32, C_WHITE);
        cv_text((240 - text_w(&FONT_S, ui.msg)) / 2, 18, &FONT_S, ui.msg, C_BLACK);
        cv_blit(0, (uint32_t)y0);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int32_t cx = 30 + 60 * (int32_t)k;
        if (!lab[k][0])
            continue;
        te_dial(cx, 12, 11, ratio[k], TE_COL[k], TE_DIM[k]);
        te_text_c(cx, 24, lab[k], TE_G3);
    }
    cv_blit(0, (uint32_t)y0);
    cv_begin(240, 16, C_BLACK);                         /* the values: white while turned */
    for (k = 0; k < 4u; k++)
        te_text_c(30 + 60 * (int32_t)k, 0, val[k], ui.hot_t && ui.hot_col == k ? C_WHITE : TE_G4);
    cv_blit(0, (uint32_t)y0 + 40u);
}

static void studio_open(uint32_t scope)
{
    uint32_t i;
    if (scope == SC_SONG && rec_wait)
        rec_wait = 0;                                   /* (an arm does not follow into the song page) */
    for (i = 0; i < NPAGES; i++) if (PAGES[i].scope == scope) {
        ui.page = (uint8_t)i; ui.home = 0; song.seq_mode = 0; ui.force = 1;
        if (scope == SC_DRUM)
            drum_group = drum_pad_sel / 4u;
        ui.msg_t = 0; ui.entry_open = 0; return;
    }
}
static void studio_pad_edit_open(void)
{
    studio_open(SC_DRUM);
    drum_page = 2u;
    ui.force = 1;
}
static void studio_drum_seq_open(void)
{
    studio_open(SC_DRUM);
    drum_page = 0u;
    drum_group = drum_pad_sel / 4u;
    ui.force = 1;
}
static void rec_settings_open(void)
{
    if (song.playing || song.rec || rec_wait || ft_on) {
        ui_message("STOP FIRST");
        return;
    }
    rec_settings_on = 1;
    ui.msg_t = 0;
    ui.force = 1;
}
static void rec_settings_close(void)
{
    if (rec_settings_on) {
        rec_settings_on = 0;
        ui.force = 1;
    }
}
static uint32_t rec_settings_active(void) { return rec_settings_on; }
static void rec_settings_knobs(void)
{
    int32_t s;
    uint32_t changed = 0;
    if ((s = panel_enc(EN_K1)) != 0) {
        rec_mode_set((int32_t)rec_mode_get() + s);
        ui.hot_col = 0; ui.hot_t = 40; changed = 1;
    }
    if ((s = panel_enc(EN_K2)) != 0) {
        rec_grid_set((int32_t)rec_grid_get() + s);
        ui.hot_col = 1; ui.hot_t = 40; changed = 1;
    }
    if ((s = panel_enc(EN_K3)) != 0) {
        rec_count_set((int32_t)rec_count_get() + s);
        ui.hot_col = 2; ui.hot_t = 40; changed = 1;
    }
    if ((s = panel_enc(EN_K4)) != 0) {
        song.g[G_CLOCK] = (int16_t)clamp((int32_t)song.g[G_CLOCK] + s, 0, 2);
        ui.hot_col = 3; ui.hot_t = 40; changed = 1;
    }
    if (changed)
        ui.force = 1;
}
static void rec_settings_draw(void)
{
    static uint32_t head, body, dials;
    static char v[4][12];
    static const char *const lab[4] = {"mode", "grid", "count-in", "metronome"};
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4];
    static const char *const modes[] = {"OVERDUB", "REPLACE"};
    static const char *const grids[] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
    static const char *const counts[] = {"OFF", "1 BAR", "2 BARS"};
    uint32_t sig;
    str_cpy(v[0], modes[rec_mode_get()], sizeof v[0]);
    str_cpy(v[1], grids[rec_grid_get()], sizeof v[1]);
    str_cpy(v[2], counts[rec_count_get()], sizeof v[2]);
    str_cpy(v[3], song.g[G_CLOCK] == 0 ? "OFF" : song.g[G_CLOCK] == 1 ? "REC ONLY" : "ON", sizeof v[3]);
    ratio[0] = (int32_t)rec_mode_get() * 1000;
    ratio[1] = (int32_t)rec_grid_get() * 250;
    ratio[2] = (int32_t)rec_count_get() * 500;
    ratio[3] = (int32_t)song.g[G_CLOCK] * 500;
    te_header("record settings", TE_G4, &head);
    sig = rec_mode_get() * 31u + rec_grid_get() * 71u + rec_count_get() * 131u + (uint32_t)song.g[G_CLOCK] * 191u;
    if (ui.force || sig != body) {
        body = sig;
        cv_begin(240, 114, C_BLACK);
        cv_text(8, 8, &FONT_S, "RECORD SETTINGS", C_WHITE);
        cv_text(8, 34, &FONT_S, "MODE: OVERDUB / REPLACE", TE_G3);
        cv_text(8, 52, &FONT_S, "GRID OFF 1/4 1/8 1/16 1/32", TE_G3);
        cv_text(8, 70, &FONT_S, "COUNT-IN OFF / 1 BAR / 2 BAR", TE_G3);
        cv_text(8, 88, &FONT_S, "METRO OFF / REC ONLY / ON", TE_G3);
        cv_blit(0, 40);
        cv_begin(240, 16, C_BLACK);
        cv_text(8, 0, &FONT_S, "OCT-: back", TE_G3);
        cv_text(96, 0, &FONT_S, "REC: arm", TE_G3);
        cv_text(164, 0, &FONT_S, "PLAY: start", TE_G3);
        cv_blit(0, 224);
    }
    te_dials(184, lab, val, ratio, sig, &dials);
}
/* LIVE: REC, on any page. One record arm, on the selected track (it follows ALGORITHM). No
 * click (seq.c): playing, recording starts at once; stopped, REC arms and the first note
 * starts the loop (or, in an empty project, a free take that REC closes: seq.c ft_close).
 * REC again stops recording / cancels the arm (playback continues). */
static void rec_toggle(void)
{
    if (ft_owns_press())
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    if (song.rec || rec_wait) {
        song.rec = 0;
        rec_wait = 0;
        ci_on = 0;
        ui_message("REC OFF");
        return;
    }
    arrangement_enabled = 0;
    if (song.playing)
        rec_begin();                    /* playing: record now */
    else
        rec_wait = 1;                   /* stopped: PLAY starts transport/count-in */
}

/* the loop position of track t: "bar.beat" in its pattern (1.1 .. ) */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* the header of the live screens: BPM, transport, the loop position, the beat lights, REC */
static void te_header(const char *title, uint16_t tc, uint32_t *cache)
{
    char b[12];
    uint32_t beat = clk_beat, k, playing = song.playing;
    uint32_t sig = studio_hash((uint32_t)song.g[G_BPM] * 7u + playing * 3u + (song.rec != 0) * 1999u +
                               (playing ? beat * 131u + TSEL->seq_idx * 7919u : 0u) + (uint32_t)song.g[G_CLOCK] * 77u +
                               arrangement_enabled * 5u + (ui.bpm_t != 0) * 104729u + song.solo * 37u, title);
    sig = sig * 31u + tc;
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 40, C_BLACK);
    fmt_int(b, song.g[G_BPM]);
    cv_text(4, 4, &FONT_L, b, ui.bpm_t ? C_WHITE : TE_G4);
    cv_text(4 + text_w(&FONT_L, b) + 4, 20, &FONT_S, "bpm", TE_G3);
    te_play_icon(104, 6, (int)playing);
    if (playing) {                                     /* bar.beat in the selected track's loop */
        loop_pos(TSEL, b);
        cv_text(122, 5, &FONT_S, b, C_WHITE);
    } else {
        cv_text(122, 5, &FONT_S, arrangement_enabled ? "song" : "loop", TE_G3);
    }
    for (k = 0; k < 4u; k++)                           /* the four beats of the bar */
        cv_rect(104 + (int32_t)k * 9, 26, 7, 7, playing && beat % 4u == k ? (k ? C_WHITE : TE_RED) : TE_G2);
    if (song.rec) {
        te_disc(224, 12, 8, TE_RED);
        cv_text(176, 4, &FONT_S, "rec", TE_RED);
        if (song.g[G_CLOCK] != 0)                       /* the click is on */
            cv_text(168, 22, &FONT_S, "click", TE_G3);
    } else {
        cv_text(240 - text_w(&FONT_S, title) - 4, 5, &FONT_S, title, tc);
        if (song.solo)
            cv_text(236 - text_w(&FONT_S, "solo"), 22, &FONT_S, "solo", C_WHITE);
    }
    cv_rect(0, 39, 240, 1, TE_G1);
    cv_blit(0, 0);
}

/* --------------------------------------------------------------- TRACKS --- */
/* swing, MPC style: "62%" */
static void swing_str(char *b, int32_t v)            /* 0 straight .. 100 the most (params.c F_SWING) */
{
    fmt_int(b, clamp(v, 0, 100));
}
static void qtz_str(char *b, uint32_t n)
{
    fmt_int(b, (int32_t)n);
    str_cpy(b + str_len(b), "%", 8u - str_len(b));
}

static void studio_home_draw(void)
{
    static uint32_t head, tiles, footer;
    static const char *const lab[4] = {"pad vol", "swing", "pan", "qtz"};
    char title[16] = "PAD01";
    char v[4][8];
    const char *val[4] = {v[0], v[1], v[2], v[3]};
    int32_t ratio[4];
    uint32_t i, sig = drum_pad_sel + 1u;
    uint32_t lvl = drum_pad_mix_get(drum_pad_sel, DRUM_MIX_LEVEL);
    int32_t pan = drum_pad_mix_get(drum_pad_sel, DRUM_MIX_PAN);
    uint32_t qtz = drum_qtz_strength_get();

    title[3] = (char)('0' + (drum_pad_sel + 1u) / 10u);
    title[4] = (char)('0' + (drum_pad_sel + 1u) % 10u);
    te_header(title, TE_DRUM, &head);
    sig = sig * 31u + drum_kit();
    for (i = 0; i < 8u; i++)
        sig = sig * 31u + (pad_lit[i] != 0u);
    if (ui.force || sig != tiles) {
        tiles = sig;
        cv_begin(240, 124, C_BLACK);
        for (i = 0; i < 8u; i++) {
            int32_t x = 5 + (int32_t)(i % 4u) * 60;
            int32_t y = 4 + (int32_t)(i / 4u) * 58;
            char n[3] = {(char)('0' + (i + 1u) / 10u), (char)('0' + (i + 1u) % 10u), 0};
            uint16_t fill = pad_lit[i] ? TE_DRUM : TE_G1;
            cv_rect(x, y, 50, 50, fill);
            if (i == drum_pad_sel) {
                cv_rect(x, y, 50, 1, C_WHITE);
                cv_rect(x, y + 49, 50, 1, C_WHITE);
                cv_rect(x, y, 1, 50, C_WHITE);
                cv_rect(x + 49, y, 1, 50, C_WHITE);
            }
            te_text_c(x + 25, y + 8, n, pad_lit[i] ? C_BLACK : TE_G3);
            te_text_c(x + 25, y + 27, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_pad_sel ? C_WHITE : TE_G4);
        }
        cv_blit(0, 44);
        /* The shared canvas is capped at 124 rows; clear the last 16 rows the SEQ grid used. */
        cv_begin(240, 16, C_BLACK);
        cv_blit(0, 168);
    }
    fmt_int(v[0], (int32_t)lvl * 100 / 127);
    swing_str(v[1], song.g[G_SWING]);
    fmt_int(v[2], pan);
    qtz_str(v[3], qtz);
    ratio[0] = (int32_t)lvl * 1000 / 127;
    ratio[1] = song.g[G_SWING] * 10;
    ratio[2] = (pan + 64) * 1000 / 127;
    ratio[3] = (int32_t)qtz * 10;
    te_dials(184, lab, val, ratio, qtz * 31u + drum_pad_sel, &footer);
}

static void studio_tracks_draw(void)
{
    static uint32_t head, rows[NTRK], footer;
    uint32_t i, j;
    te_header("tracks", TE_G3, &head);
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        char b[24], e[16];
        uint32_t selected = song.sel == i, len = (uint32_t)clamp(t->p[P_SLEN], 1, 64);
        uint32_t level = i == TRK_DRUM ? song.g[G_DRLVL] : t->p[P_LEVEL];
        uint32_t silent = trk_silent(t) || !level, pos = t->seq_idx % len, rec = (song.rec >> i) & 1u;
        uint32_t solo = (song.solo >> i) & 1u, bank = song.playing ? pos / 16u : 0u, h;
        uint16_t col = TE_COL[i], dim = TE_DIM[i];
        if (i == TRK_DRUM) {
            str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
            te_lower(e, "drums", sizeof e);
        } else {
            trk_short_name(i, b);
            te_lower(e, ENGINES[t->eng_req % NENGINES]->name, sizeof e);
        }
        b[13] = 0;
        h = studio_hash(selected + level * 7u + silent * 997u + rec * 1999u + solo * 4999u + len * 37u +
                        song.playing * 7u + pos * 71u + bank * 13u, b);
        for (j = 0; j < len; j++) h = h * 31u + (uint32_t)trk_step_on(t, j);
        if (!ui.force && h == rows[i]) continue;
        rows[i] = h;
        cv_begin(240, 36, C_BLACK);
        cv_rect(2, 3, 26, 30, selected ? col : dim);   /* the track tile */
        {
            char n[2] = {(char)('1' + i), 0};
            cv_text(11, 10, &FONT_S, n, selected ? C_BLACK : col);
        }
        cv_text(34, 1, &FONT_S, b, selected ? C_WHITE : TE_G4);
        if (34 + text_w(&FONT_S, b) + 6 + text_w(&FONT_S, e) < 196)
            cv_text(34 + text_w(&FONT_S, b) + 6, 1, &FONT_S, e, selected ? col : TE_G3);
        if (rec) {
            cv_rect(200, 2, 36, 15, TE_RED);
            cv_text(206, 1, &FONT_S, "rec", C_WHITE);
        } else if (solo) {
            cv_rect(200, 2, 36, 15, C_WHITE);
            cv_text(202, 1, &FONT_S, "solo", C_BLACK);
        } else if (silent) {
            cv_text(204, 1, &FONT_S, "mute", TE_G3);
        }
        for (j = 0; j < 16u; j++) {                    /* the 16 steps in view */
            uint32_t p = bank * 16u + j, on = 0;
            if (len <= 16u) {
                if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
            } else {                                    /* > 16 and stopped: the whole pattern, folded */
                uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k;
                if (song.playing) {
                    if (p < len) on = trk_step_on(t, p) ? 2u : 1u;
                } else {
                    on = 1;
                    for (k = a; k < z; k++) if (trk_step_on(t, k)) on = 2;
                }
            }
            cv_rect(34 + (int32_t)j * 10, 21, 8, 9,
                    !on ? C_BLACK : on == 2u ? (silent ? TE_G3 : selected ? col : dim) : TE_G1);
            if (on == 2u && !selected && !silent)
                cv_rect(34 + (int32_t)j * 10, 21, 8, 2, col);
            if (song.playing && p == pos)
                cv_rect(34 + (int32_t)j * 10, 31, 8, 2, C_WHITE);
        }
        cv_rect(198, 23, 38, 5, TE_G1);                /* the level */
        if (!silent)
            cv_rect(198, 23, (int32_t)level * 38 / 127, 5, selected ? col : TE_G3);
        cv_blit(0, 40 + i * 36);
    }
    {   /* KNOB 1 swing (the global groove), 2 level, 3 steps, 4 pan of the selected track */
        track_t *t = TSEL;
        static char v[4][8];
        static const char *const lab[4] = {"swing", "level", "steps", "pan"};
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        int32_t ratio[4];
        uint32_t lvl = is_drum(t) ? song.g[G_DRLVL] : t->p[P_LEVEL];
        swing_str(v[0], song.g[G_SWING]);
        fmt_int(v[1], t->p[P_MUTE] ? 0 : (int32_t)lvl * 100 / 127);
        fmt_int(v[2], t->p[P_SLEN]);
        fmt_int(v[3], t->p[P_PAN]);
        ratio[0] = song.g[G_SWING] * 10;
        ratio[1] = t->p[P_MUTE] ? 0 : (int32_t)lvl * 1000 / 127;
        ratio[2] = (t->p[P_SLEN] - 1) * 1000 / 63;
        ratio[3] = (t->p[P_PAN] + 64) * 1000 / 127;
        te_dials(184, lab, val, ratio, song.sel, &footer);
    }
}

/* ---------------------------------------------------------------- DRUMS --- */
static const char *const LV_NAME[4] = {"norm", "ghost", "soft", "hard"};
static uint32_t lvl_rank(uint32_t lvl);                 /* (below) ghost 0, soft 1, norm 2, hard 3 */
static uint16_t lvl_col(uint32_t lvl)                   /* a hit's colour by its level */
{
    return lvl == LV_GHOST ? TE_DIM[3] : lvl == LV_SOFT ? TE_MID[3] : lvl == LV_HARD ? C_WHITE : TE_DRUM;
}
static uint8_t key_lit[27];                             /* menu NOTES, a synth track: frames its key stays lit */
static uint8_t key_lit_trk = 0xFF;                      /* (the track key_lit is for) */
static void pads_tick(void)                             /* once a frame: the hits since the last one */
{
    uint32_t i, hits, k;
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++) {
        if ((hits >> i) & 1u) pad_lit[i] = 6;
        else if (pad_lit[i]) pad_lit[i]--;
    }
    /* a synth track: each note it started lights its key 6 frames (~0.1 s), so a short sequencer note
     * (a 1/16 at GATE 50 %: 60..80 ms) is seen even when it ends between two frames */
    if (song.sel != key_lit_trk) {
        memset(key_lit, 0, sizeof key_lit);
        key_lit_trk = song.sel;
    }
    for (k = 0; k < 27u; k++)
        if (key_lit[k])
            key_lit[k]--;
}

static void drum_screen_draw(void)
{
    static uint32_t head, title_sig, body_sig, footer;
    uint32_t i, j, len = (uint32_t)clamp(TDRUM->p[P_SLEN], 1, 64), kit = drum_kit(), sig, bank;
    if (drum_cursor >= len) drum_cursor = (uint8_t)(len - 1u);
    bank = drum_cursor / 16u;
    {
        char st[16];
        const char *style = drum_page == 1u ? "sample" : drum_page == 3u ? "user kits" : DRUM_KIT_STYLES[kit];
        te_lower(st, style, sizeof st);
        st[12] = 0;
        te_header(st, TE_DRUM, &head);
    }
    sig = kit * 131u + drum_page * 7u + drum_group * 19u + drum_pad_sel * 977u + bank * 31u + len +
          drum_browser_category * 41u + drum_browser_sample * 53u + drum_user_slot * 67u; /* title band */
    if (ui.force || sig != title_sig) {
        char b[8];
        title_sig = sig;
        cv_begin(240, 44, C_BLACK);
        cv_rect(2, 4, 34, 34, TE_DRUM);
        fmt_int(b, (int32_t)kit + 1);
        cv_text(19 - text_w(&FONT_S, b) / 2, 13, &FONT_S, b, C_BLACK);
        if (drum_page == 2u) {
            char n[8];
            cv_text(44, 6, &FONT_L, "PAD EDIT", C_WHITE);
            fmt_int(n, (int32_t)drum_pad_sel + 1);
            cv_text(44, 24, &FONT_S, "PAD", TE_G3);
            cv_text(72, 24, &FONT_S, n, C_WHITE);
            cv_text(92, 24, &FONT_S, "SAMPLE KIT", TE_G3);
        } else if (drum_page == 1u) {
            char n[12];
            cv_text(44, 6, &FONT_L, "SAMPLE MGR", C_WHITE);
            cv_text(44, 24, &FONT_S, DRUM_BROWSER_CATEGORIES[drum_browser_category], TE_G3);
            cv_text(92, 24, &FONT_S, drum_custom_sample_name(drum_browser_sample), C_WHITE);
            fmt_int(n, drum_browser_sample);
            cv_text(190, 24, &FONT_S, n, TE_G3);
        } else if (drum_page == 3u) {
            char n[12];
            cv_text(44, 6, &FONT_L, "USER KITS", C_WHITE);
            fmt_int(n, drum_user_slot + 1u);
            cv_text(44, 24, &FONT_S, "USER", TE_G3);
            cv_text(78, 24, &FONT_S, n, C_WHITE);
            cv_text(108, 24, &FONT_S, drum_user_slot < DRUM_USER_KITS ? "4 SLOTS" : "--", TE_G3);
        } else {
            cv_text(44, 6, &FONT_L, DRUM_KIT_NAMES[kit], C_WHITE);
            if (!drum_page) {
                static const char *const GROUP[2] = {"TR1-4", "TR5-8"};
                static const char *const PAGE[4] = {"01-16", "17-32", "33-48", "49-64"};
                cv_text(44, 24, &FONT_S, GROUP[drum_group], C_WHITE);
                cv_text(94, 24, &FONT_S, PAGE[bank], TE_G3);
            }
        }
        cv_text(180, 4, &FONT_S, "grid", drum_page == 0 ? C_WHITE : TE_G3);
        cv_text(180, 22, &FONT_S, "browse", drum_page == 1 ? C_WHITE : TE_G3);
        cv_text(214, 4, &FONT_S, "edit", drum_page == 2 ? C_WHITE : TE_G3);
        cv_text(180, 38, &FONT_S, "user", drum_page == 3 ? C_WHITE : TE_G3);
        cv_rect(174, drum_page == 1 ? 26 : drum_page == 3 ? 42 : 8, 3, 9, TE_DRUM);
        cv_blit(0, 40);
    }
    sig = drum_page + drum_group * 13u + drum_pad_sel * 7u + drum_cursor * 101u + song.playing * 71u + len * 3u +
          drum_browser_category * 17u + drum_browser_sample * 23u + drum_user_slot * 29u;
    if (drum_page == 2u) {
        sig = sig * 31u + (uint32_t)(drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_TUNE) + 24);
        sig = sig * 31u + (uint32_t)drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_END);
        sig = sig * 31u + (uint32_t)drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_DECAY);
    }
    if (song.playing) sig = sig * 31u + TDRUM->seq_idx;
    for (i = 0; i < DRUM_LANES; i++) sig = sig * 3u + (pad_lit[i] != 0);
    for (i = 0; i < len; i++) {
        const dstep_t *s = &TDRUM->dstep[i];
        sig = sig * 31u + dstep_mask(s);
        sig = sig * 31u + s->lvl[0] + s->lvl[1] * 7u + s->lvl[2] * 49u + s->lvl[3] * 343u;
        sig = sig * 31u + s->rat[0] + s->rat[1] * 7u + s->rat[2] * 49u + s->rat[3] * 343u;
    }
    if (ui.force || sig != body_sig) {
        body_sig = sig;
        cv_begin(240, 100, C_BLACK);
        if (!drum_page) {                              /* SEQ: four rows from one of two groups; each is a SLOOP drum lane */
            for (i = 0; i < 4u; i++) {
                uint32_t lane = (uint32_t)drum_group * 4u + i;
                int32_t y = (int32_t)i * 24 + 2;
                cv_text(2, y + 7, &FONT_S, LANE_SHORT[lane], i == drum_pad_sel % 4u ? C_WHITE : TE_G3);
                if (i == drum_pad_sel % 4u) {
                    cv_rect(0, y, 1, 20, TE_DRUM);
                    cv_rect(0, y, 240, 1, TE_DRUM);
                    cv_rect(0, y + 19, 240, 1, TE_DRUM);
                }
                for (j = 0; j < 16u; j++) {
                    uint32_t p = bank * 16u + j;
                    const dstep_t *s = &TDRUM->dstep[p < NSTEP ? p : 0];
                    uint16_t c;
                    if (p >= len) continue;
                    c = dstep_has(s, lane) ? lvl_col(dstep_lvl(s, lane)) : i == drum_pad_sel % 4u ? TE_G2 : TE_G1;
                    if (song.playing && p == TDRUM->seq_idx && c == TE_G1) c = TE_G2;
                    cv_rect(42 + (int32_t)j * 12, y + 2, 10, 16, c);
                    if (dstep_has(s, lane) && dstep_rat(s, lane)) { /* ratchet: a notch per extra hit */
                        uint32_t r;
                        for (r = 0; r < dstep_rat(s, lane); r++)
                            cv_rect(43 + (int32_t)j * 12 + (int32_t)r * 2, y + 9, 1, 2, C_BLACK);
                    }
                    if (p == drum_cursor && i == drum_pad_sel % 4u) {
                        cv_rect(41 + (int32_t)j * 12, y + 1, 12, 1, C_WHITE);
                        cv_rect(41 + (int32_t)j * 12, y + 19, 12, 1, C_WHITE);
                    }
                }
            }
        } else if (drum_page == 3u) {
            for (i = 0; i < DRUM_USER_KITS; i++) {
                int32_t y = (int32_t)i * 24 + 4;
                uint16_t c = i == drum_user_slot ? TE_MID[3] : TE_G1;
                cv_rect(8, y, 224, 20, c);
                cv_text(18, y + 5, &FONT_S, i == drum_user_slot ? "USER" : "KIT", i == drum_user_slot ? C_WHITE : TE_G3);
                {
                    char n[8];
                    fmt_int(n, (int32_t)i + 1);
                    cv_text(55, y + 5, &FONT_S, n, i == drum_user_slot ? C_WHITE : TE_G3);
                }
                cv_text(100, y + 5, &FONT_S, i == drum_user_slot ? "EDITABLE" : "SAVED", i == drum_user_slot ? C_WHITE : TE_G3);
            }
        } else {                                       /* BROWSER / PAD EDIT: eight MVP pads, lit on each hit */
            for (i = 0; i < MVP_DRUM_PADS; i++) {
                int32_t x = 2 + (int32_t)(i % 4u) * 60, y = (int32_t)(i / 4u) * 25;
                uint16_t fill = pad_lit[i] ? TE_DRUM : drum_page == 2u && i == drum_pad_sel ? TE_MID[3] : TE_G1;
                cv_rect(x, y, 56, 23, fill);
                if (drum_page == 2u && i == drum_pad_sel)
                    cv_rect(x, y, 56, 1, C_WHITE);
                if (drum_page == 1u) {
                    char sn[8];
                    te_lower(sn, drum_custom_sample_name(drum_custom_sample_id_get(i)), sizeof sn);
                    te_text_c(x + 28, y + 4, sn, pad_lit[i] ? C_BLACK : i == drum_pad_sel ? C_WHITE : TE_G3);
                } else {
                    te_text_c(x + 28, y + 4, LANE_SHORT[i], pad_lit[i] ? C_BLACK : i == drum_pad_sel && drum_page == 2u ? C_WHITE : TE_G3);
                }
            }
        }
        cv_blit(0, 84);
    }
    {
        static char v[4][12];
        const char *val[4] = {v[0], v[1], v[2], v[3]};
        static const char *const LG[4] = {"level", "steps", "swing", "qtz"}, *const LK[4] = {"kit", "level", "reverb", "pan"};
        static const char *const LE[4] = {"tune", "start", "end", "decay"};
        int32_t ratio[4];
        if (!drum_page) {
            fmt_int(v[0], song.g[G_DRLVL] * 100 / 127);
            fmt_int(v[1], (int32_t)len);
            swing_str(v[2], song.g[G_SWING]);
            qtz_str(v[3], drum_qtz_strength_get());
            ratio[0] = song.g[G_DRLVL] * 1000 / 127;
            ratio[1] = (int32_t)(len - 1u) * 1000 / 63;
            ratio[2] = song.g[G_SWING] * 10;
            ratio[3] = (int32_t)drum_qtz_strength_get() * 10;
            te_dials(184, LG, val, ratio, drum_qtz_strength_get() * 31u + len * 7u +
                     song.g[G_SWING] * 13u + (uint32_t)song.g[G_DRLVL], &footer);
        } else if (drum_page == 1u) {
            static const char *const LB[4] = {"cat", "sample", "assign", "level"};
            fmt_int(v[0], drum_browser_category + 1u);
            fmt_int(v[1], drum_browser_sample);
            str_cpy(v[2], drum_kit_source_get() == DRUM_KIT_SOURCE_CUSTOM ? "CUSTOM" : "READY", sizeof v[2]);
            fmt_int(v[3], drum_pad_mix_get(drum_pad_sel, DRUM_MIX_LEVEL) * 100 / 127);
            ratio[0] = (int32_t)drum_browser_category * 1000 / 4;
            ratio[1] = ((int32_t)drum_browser_sample - DRUM_BROWSER_FIRST[drum_browser_category]) * 1000 /
                       (DRUM_BROWSER_COUNT[drum_browser_category] > 1u ? DRUM_BROWSER_COUNT[drum_browser_category] - 1u : 1u);
            ratio[2] = -1;
            ratio[3] = drum_pad_mix_get(drum_pad_sel, DRUM_MIX_LEVEL) * 1000 / 127;
            te_dials(184, LB, val, ratio, 2u + drum_browser_category * 7u + drum_browser_sample * 11u, &footer);
        } else if (drum_page == 3u) {
            static const char *const LU[4] = {"slot", "load", "save", "reset"};
            fmt_int(v[0], drum_user_slot + 1u);
            str_cpy(v[1], "KIT", sizeof v[1]);
            str_cpy(v[2], "KIT", sizeof v[2]);
            str_cpy(v[3], "FACT", sizeof v[3]);
            ratio[0] = (int32_t)drum_user_slot * 1000 / (DRUM_USER_KITS - 1u);
            ratio[1] = ratio[2] = ratio[3] = -1;
            te_dials(184, LU, val, ratio, drum_user_slot * 31u + 17u, &footer);
        } else {
            int sampled = drum_kit_has_sample_edit(kit);
            int32_t tune = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_TUNE);
            int32_t end = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_END);
            int32_t decay = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_DECAY);
            if (sampled) {
                fmt_int(v[0], tune);
                str_cpy(v[1], "0 N/A", sizeof v[1]);
                fmt_int(v[2], end);
                fmt_int(v[3], decay);
                ratio[0] = (tune + 24) * 1000 / 48;
                ratio[1] = -1;
                ratio[2] = (end - 1) * 1000 / 99;
                ratio[3] = decay * 1000 / 127;
            } else {
                uint32_t k;
                for (k = 0; k < 4u; k++) { str_cpy(v[k], "--", sizeof v[k]); ratio[k] = -1; }
            }
            te_dials(184, LE, val, ratio, sampled * 3u + 4u, &footer);
        }
    }
}

/* the level order of the knobs: ghost < soft < norm < hard */
static const uint8_t LV_UP[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static uint32_t lvl_rank(uint32_t lvl) { return lvl == LV_GHOST ? 0u : lvl == LV_SOFT ? 1u : lvl == LV_NORM ? 2u : 3u; }

/* the GRID page shown, no layer held: its keys are steps (seq.c kb_grid) */
/* The Browser uses the same white note keys as PAD EDIT for pad selection and
 * audition.  Keep the existing sequencer/PAD-EDIT paths and admit only the
 * Browser page as the additional drum-page consumer. */
static int grid_keys_on(void) { return on_drum_page() && drum_page <= 2u && song.sel == TRK_DRUM && !ui.menu && !song.rec && !rec_wait && !ft_on; }
/* a key down on the GRID page: the white keys are the 16 steps of this page of steps for the sound of
 * KNOB 1 — an empty one is set (NORM, the sound heard), a set one cleared; the first four black keys
 * pick the page (steps 1-16, 17-32, 33-48, 49-64), as in the SEQ layer. The cursor follows */
static void grid_key(uint32_t k)
{
    static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
    uint32_t len = trk_len(TDRUM), idx;
    int32_t w = punch_key(k);
    dstep_t *st;
    if (drum_page == 1u) {
        if (w >= 0 && (uint32_t)w < MVP_DRUM_PADS) {
            drum_pad_sel = (uint8_t)w;
            drum_group = drum_pad_sel / 4u;
            drum_browser_sync();
            drum_preview(drum_browser_sample,drum_preview_level);
            ui.force = 1;
        }
        return;
    }
    if (drum_page == 2u) {
        if (w >= 0 && (uint32_t)w < MVP_DRUM_PADS) {
            drum_pad_sel = (uint8_t)w;
            drum_group = drum_pad_sel / 4u;
            audition_lane(drum_pad_sel);
            ui.force = 1;
        }
        return;
    }
    if (w < 0) {
#if !FELUCCA_DRUM_ONLY
        if (k < 12u && PG[k] >= 0 && (uint32_t)PG[k] * 16u < len) {
            idx = (uint32_t)PG[k] * 16u + drum_cursor % 16u;
            drum_cursor = (uint8_t)(idx < len ? idx : len - 1u);
            ui.force = 1;
        }
#endif
        return;
    }
    idx = drum_cursor / 16u * 16u + (uint32_t)w;
    if (idx >= len)
        return;
    drum_cursor = (uint8_t)idx;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(TDRUM, ui.step_sess ? ui.step_sess : (ui.step_sess = (undo_sess += 4u) | 3u));
    st = &TDRUM->dstep[idx];
    fm1_irq_off();
    if (dstep_has(st, drum_pad_sel)) {
        dstep_clr(st, drum_pad_sel);
        drum_raw_offset_set(drum_pad_sel, idx, 0);
        if (!dstep_mask(st)) {                          /* an empty step keeps no lock, no nudge, no condition */
            lock_del(TDRUM, idx, P_COUNT);
            TDRUM->micro[idx] = 0;
            step_fill_set(TDRUM, idx, FC_NORM);
        }
    } else {
        dstep_set(st, drum_pad_sel, LV_NORM, 0);
        drum_raw_offset_set(drum_pad_sel, idx, 0);
        TDRUM->seq_active = 1;
    }
    fm1_irq_on();
    if (dstep_has(st, drum_pad_sel))
        audition_lane(drum_pad_sel);
    sync_reload = 1;
}

static void drum_screen_input(uint32_t pressed, uint32_t home)
{
    uint32_t k, b;
    int32_t s;
    if (song.sel != TRK_DRUM || home == 1u) {
        go_home();
        return;
    }
    for (k = 0; k < NB; k++) if ((pressed >> panel.btn[k]) & 1u) {
        b = k;
        if (b == B_PLAY) {
            if (ft_owns_press()) ;
            else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) ui_message("EMPTY SECTION: REC");
            else transport_req = song.playing ? 2 : 1;
        } else if (b == B_SEQ) {
            drum_page=0u;drum_group=drum_pad_sel/4u;
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_EDIT) {
            drum_page = 2u;
            ui.msg_t = 0;
            ui.force = 1;
        } else if (b == B_SAVE) {
            open_family(FAM_SAVE);
            return;
        } else if (drum_page==1u && (b==B_OCTUP || b==B_OCTDN)) {
            if(song.rec || rec_wait || ft_on) ui_message("STOP BEFORE ASSIGN");
            else if(b==B_OCTDN) drum_preview(drum_browser_sample,drum_preview_level);
            else if(drum_custom_sample_id_set(drum_pad_sel,drum_browser_sample)) ui_message("ASSIGNED");
            ui.force=1;
        } else if (drum_page==3u && (b==B_OCTUP || b==B_OCTDN)) {
            if(song.rec || rec_wait || ft_on || song.playing || transport_req) { ui_message("STOP FIRST"); continue; }
            if(b==B_OCTDN) { ui_message(drum_user_kit_load(drum_user_slot) ? "KIT LOADED" : "EMPTY KIT"); rc0_user_arm=0; }
            else if(rc0_user_arm==drum_user_slot+1u && fm1_ms-rc0_confirm_ms<3000u) {
                drum_fx_config_t backup[DRUM_USER_KITS][9];
                drum_user_kit_export(drum_user_backup_valid,drum_user_backup_pads);
                drum_user_fx_export(backup);
                rc0_user_arm=0;
                if(drum_user_kit_save(drum_user_slot) && project_save((uint32_t)clamp(song.g[G_SLOT]-1,0,3))) ui_message("KIT SAVED");
                else { drum_user_kit_import(drum_user_backup_valid,drum_user_backup_pads); drum_user_fx_import(backup); ui_message("SAVE REFUSED"); }
            } else { rc0_user_arm=(uint8_t)(drum_user_slot+1u); rc0_confirm_ms=fm1_ms; ui_message("OCT+ AGAIN: SAVE KIT"); }
            ui.force=1;
        }
    }
    if ((s = panel_enc(EN_SELECT))) {
        if(drum_page==3u) {
            drum_user_slot=(uint8_t)clamp((int32_t)drum_user_slot+s,0,DRUM_USER_KITS-1u);
            rc0_user_arm=0;
        } else {
            drum_pad_sel=(uint8_t)clamp((int32_t)drum_pad_sel+s,0,7);
            drum_group=drum_pad_sel/4u;
            if(drum_page==1u)drum_browser_sync();
        }
        ui.msg_t = 0;
        ui.force = 1;
    }
    if ((s = panel_enc(EN_ALGO)) && !ft_on) {
        if (drum_page == 0u && song.sel == TRK_DRUM) {
            uint32_t len=trk_len(TDRUM),bank=(uint32_t)clamp((int32_t)(drum_cursor/16u)+s,0,(int32_t)((len-1u)/16u));
            drum_cursor=(uint8_t)clamp((int32_t)(bank*16u+drum_cursor%16u),0,(int32_t)len-1);
            ui.force = 1;
        } else if (drum_page == 3u && song.sel == TRK_DRUM) {
            drum_pad_sel = (uint8_t)clamp((int32_t)drum_pad_sel + s, 0, MVP_DRUM_PADS - 1);
            drum_group = drum_pad_sel / 4u;
            ui.force = 1;
        }
    }
    panel_enc(EN_PRESET); /* factory selection lives on HOME only */
    for (k = 0; k < 4u; k++) if ((s = panel_enc(EN_K1 + k))) {
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (drum_page == 0u) {
            if (k == 0u) {
                drum_pad_mix_set(drum_pad_sel,DRUM_MIX_LEVEL,drum_pad_mix_get(drum_pad_sel,DRUM_MIX_LEVEL)+accel(EN_K1+k,s,127));
            } else if (k == 1u) {
                int32_t len = clamp((int32_t)TDRUM->p[P_SLEN] + accel(EN_K1 + k, s, 63), 1, NSTEP);
                TDRUM->p[P_SLEN] = (int16_t)len;
                if (drum_cursor >= (uint32_t)len) drum_cursor = (uint8_t)(len - 1);
                sync_reload = 1;
            } else if (k == 2u) {
                song.g[G_SWING] = (int16_t)clamp(song.g[G_SWING] + accel(EN_K1 + k, s, GP[G_SWING].max - GP[G_SWING].min), GP[G_SWING].min, GP[G_SWING].max);
            } else {
                drum_qtz_strength_set(clamp((int32_t)drum_qtz_strength_get() + s, 0, 100));
            }
            ui.force = 1;
        } else if (drum_page == 1u) {
            if (k == 0u) {
                drum_browser_category = (uint8_t)((drum_browser_category + (s > 0 ? 1u : 4u)) % 5u);
                drum_browser_sample = DRUM_BROWSER_FIRST[drum_browser_category];
            } else if (k == 1u) {
                uint32_t first = DRUM_BROWSER_FIRST[drum_browser_category], count = DRUM_BROWSER_COUNT[drum_browser_category];
                int32_t rel = (int32_t)drum_browser_sample - (int32_t)first;
                rel = (rel + s) % (int32_t)count;
                if (rel < 0) rel += (int32_t)count;
                drum_browser_sample = (uint8_t)(first + (uint32_t)rel);
            } else if (k == 2u) {
                drum_pad_mix_set(drum_pad_sel, DRUM_MIX_LEVEL, drum_pad_mix_get(drum_pad_sel, DRUM_MIX_LEVEL)+s);
            } else {
                drum_preview_level=(uint8_t)clamp((int32_t)drum_preview_level+s,0,127);
            }
        } else if (drum_page == 3u) {
            if (k == 0u) {
                drum_pad_mix_set(drum_pad_sel,DRUM_MIX_LEVEL,drum_pad_mix_get(drum_pad_sel,DRUM_MIX_LEVEL)+s);
            } else if (k == 1u) {
                drum_pad_mix_set(drum_pad_sel,DRUM_MIX_PAN,drum_pad_mix_get(drum_pad_sel,DRUM_MIX_PAN)+s);
            } else if (k == 2u) {
                drum_pad_edit_set(drum_pad_sel,DRUM_EDIT_TUNE,drum_pad_edit_get(drum_pad_sel,DRUM_EDIT_TUNE)+s);
            } else {
                drum_pad_edit_set(drum_pad_sel,DRUM_EDIT_DECAY,drum_pad_edit_get(drum_pad_sel,DRUM_EDIT_DECAY)+s);
            }
        } else if (drum_kit_has_sample_edit(drum_kit())) {
            int32_t value;
            if (k == 1u) { drum_pad_edit_set(drum_pad_sel,DRUM_EDIT_START,drum_pad_edit_get(drum_pad_sel,DRUM_EDIT_START)+accel(EN_K1+k,s,99)); ui.force=1; continue; }
            if (k == 0u) value = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_TUNE) + accel(EN_K1 + k, s, 48);
            else if (k == 2u) value = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_END) + accel(EN_K1 + k, s, 99);
            else value = drum_pad_edit_get(drum_pad_sel, DRUM_EDIT_DECAY) + accel(EN_K1 + k, s, 127);
            drum_pad_edit_set(drum_pad_sel, k == 0u ? DRUM_EDIT_TUNE : k == 2u ? DRUM_EDIT_END : DRUM_EDIT_DECAY, value);
            ui.force = 1;
        } else {
            ui_message("SAMPLE KIT ONLY");
        }
    }
}

/* ---------------------------------------------------------------- REC --- */
/* a big seven-segment digit, TE style: w x h, segments t thick */
static void te_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    uint32_t m = SEG[d % 10u];
    int32_t hh = h / 2;
    if (m & 0x01) cv_rect(x + t, y, w - 2 * t, t, c);                       /* a */
    if (m & 0x02) cv_rect(x + w - t, y + t, t, hh - t, c);                  /* b */
    if (m & 0x04) cv_rect(x + w - t, y + hh, t, hh - t, c);                 /* c */
    if (m & 0x08) cv_rect(x + t, y + h - t, w - 2 * t, t, c);               /* d */
    if (m & 0x10) cv_rect(x, y + hh, t, hh - t, c);                         /* e */
    if (m & 0x20) cv_rect(x, y + t, t, hh - t, c);                          /* f */
    if (m & 0x40) cv_rect(x + t, y + hh - t / 2, w - 2 * t, t, c);          /* g */
}

static uint8_t rec_shown;
/* the four tracks, compact, from y0: the one that records is framed in red */
static void rec_rows(uint32_t rt, uint32_t take, uint32_t y0, uint32_t *cache)
{
    uint32_t sig = rt * 3u + rec_wait + take * 5u + drum_kit() * 977u + y0, i, j;
    for (i = 0; i < NTRK; i++) {
        char b[16];
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        sig = studio_hash(sig, b) + (uint32_t)trk[i].p[P_SLEN] * 31u;
        for (j = 0; j < NSTEP; j++) sig = sig * 3u + (uint32_t)trk_step_on(&trk[i], j);
    }
    if (!ui.force && sig == *cache)
        return;
    *cache = sig;
    cv_begin(240, 76, C_BLACK);
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, 64), armed = i == rt;
        int32_t y = (int32_t)i * 19;
        char b[16];
        if (armed) cv_rect(0, y, 240, 18, TE_RED), cv_rect(1, y + 1, 238, 16, C_BLACK);
        cv_rect(4, y + 3, 12, 12, armed ? TE_COL[i] : TE_DIM[i]);
        if (i == TRK_DRUM) str_cpy(b, DRUM_KIT_NAMES[drum_kit()], sizeof b);
        else trk_short_name(i, b);
        b[10] = 0;
        cv_text(22, y + 1, &FONT_S, b, armed ? C_WHITE : TE_G3);
        for (j = 0; j < 16u; j++) {
            uint32_t a = j * len / 16u, z = (j + 1u) * len / 16u, k, on = 0;
            if (z == a) z = a + 1u;
            for (k = a; k < z && k < NSTEP; k++) if (trk_step_on(t, k)) on = 1;
            cv_rect(106 + (int32_t)j * 8, y + 5, 6, 8, on ? (armed ? TE_COL[i] : TE_DIM[i]) : TE_G1);
        }
    }
    cv_blit(0, y0);
}

/* REC armed (stopped: waiting for the first note, or PLAY), its count-in, or a free take running
 * (seq.c). Armed, the dials say how it records: KNOB 1 MODE free / tempo (an empty project),
 * KNOB 2 LENGTH (1, 2 or 4 bars), KNOB 3 START note / count (ui_input.c rec_knobs). The four tracks
 * stay in view; the REC LED blinks while armed, is lit during the take. */
static void rec_screen_draw(void)
{
    static uint32_t head, body, rows, foot;
    static uint8_t layout;
    uint32_t sig, take = ft_on, rt = take ? ft_trk % NTRK : song.sel;
    uint32_t blink = (fm1_ms / 250u) & 1u, secs = take ? ft_t * CTL / FS : 0u, bars = 0, bpm = 0;
    uint32_t lay = take ? 1u : 2u, count = ci_on;
    if (take)
        bars = ft_fit(ft_t, &bpm);
    if (!rec_shown || lay != layout) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.force = 1;
    }
    rec_shown = 1;
    layout = (uint8_t)lay;
    te_header(take ? "free take" : count ? "count-in" : "rec ready", TE_RED, &head);
    if (take) {                                         /* free take: the time, the loop it makes */
        sig = 1000003u + rt * 7919u + blink * 31u + secs * 131u + bars * 17u + bpm * 3u;
        if (ui.force || sig != body) {
            char b[24];
            uint32_t n = secs > 99u ? 99u : secs;
            body = sig;
            cv_begin(240, 106, C_BLACK);
            te_disc(18, 30, 9, blink ? TE_RED : TE_DIM[3]);
            if (n >= 10u)
                te_digit(36, 4, 32, 56, 7, n / 10u, C_WHITE);
            te_digit(76, 4, 32, 56, 7, n % 10u, C_WHITE);
            cv_text(112, 44, &FONT_S, "s", TE_G3);
            cv_rect(132, 6, 1, 54, TE_G1);
            if (bars) {
                fmt_int(b, (int32_t)bars);
                cv_text(146, 4, &FONT_L, b, TE_COL[rt & 3u]);
                cv_text(146 + text_w(&FONT_L, b) + 6, 18, &FONT_S, bars == 1u ? "bar" : "bars", TE_G3);
                fmt_int(b, (int32_t)bpm);
                cv_text(146, 42, &FONT_S, b, C_WHITE);
                cv_text(146 + text_w(&FONT_S, b) + 4, 42, &FONT_S, "bpm", TE_G3);
            } else {
                cv_text(146, 4, &FONT_L, "-", TE_G3);
            }
            te_text_c(120, 80, "press rec on the 1", TE_RED);
            cv_blit(0, 42);
        }
        rec_rows(rt, take, 148u, &rows);
        if (ui.force || foot != 1u) {
            foot = 1u;
            cv_begin(240, 16, C_BLACK);
            cv_text(4, 0, &FONT_S, "rec: close", TE_G3);
            cv_text(236 - text_w(&FONT_S, "play: drop"), 0, &FONT_S, "play: drop", TE_G3);
            cv_blit(0, 224);
        }
        return;
    }
    foot = 0;
    /* armed / counting in: what happens next */
    sig = 2000003u + rec_count_get() * 13u + rec_mode_get() * 23u + rec_grid_get() * 29u + count * 17u + ci_beat * 19u + blink * 31u;
    if (ui.force || sig != body) {
        static const char *const L1[3] = {"press play", "press play", "press play"};
        static const char *const L2[3] = {"record starts now", "record starts now", "count-in then rec"};
        static const char *const L3[3] = {"play: start  rec: cancel", "play: start  rec: cancel", "rec: cancel"};
        uint32_t m = rec_count_get() ? 2u : 1u;
        body = sig;
        cv_begin(240, 64, C_BLACK);
        if (count) {                                    /* the count-in: 4, 3, 2, 1 */
            uint32_t total_beats = (uint32_t)rec_count_get() * 4u;
            uint32_t remaining = ci_beat < total_beats ? total_beats - ci_beat : 1u;
            te_digit(96, 4, 30, 54, 6, remaining > 9u ? 9u : remaining, C_WHITE);
            te_disc(60, 31, 12, TE_RED);
            cv_text(144, 14, &FONT_S, rec_count_get() == 2u ? "2-bar count" : "1-bar count", TE_G4);
            cv_text(144, 34, &FONT_S, "rec: cancel", TE_G3);
        } else {
            te_disc(28, 30, 18, blink ? TE_RED : TE_DIM[3]);
            te_disc(28, 30, 7, C_BLACK);
            cv_text(60, 6, &FONT_S, L1[m], C_WHITE);
            cv_text(60, 24, &FONT_S, L2[m], TE_G4);
            cv_text(60, 42, &FONT_S, L3[m], TE_G3);
        }
        cv_blit(0, 42);
    }
    rec_rows(rt, 0u, 106u, &rows);
}

#include "ui_rc0.c"
