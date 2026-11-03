/* SPDX-License-Identifier: GPL-3.0-only */
/* RC0: original native artwork following the owner's seven layout references.
 * Uses SLOOP's 240x124 band canvas and existing 240x240 LCD driver unchanged. */
#include "drum_waveforms.h"
#include "drum_fx.h"
/* Owner RC2 RGB authority; quantized only by the unchanged RGB565 driver. */
#define RC_BG RGB(249,121,5)
#define RC_INK RGB(57,18,16)
#define RC_DIM RC_INK
#define RC_CHASSIS RGB(176,177,171)
#define RC_BLACK RGB(0,0,0)
#define RC_KNOB RGB(207,197,171)
#define RC_PAD RGB(212,222,228)
#define RC_BACK RGB(106,111,114)
#define RC_PADNUM RGB(90,97,98)
#define RC_FACE RGB(204,94,2)
#define RC_GRID RC_FACE
#define RC_BLUE RGB(49,149,180)
#define RC_RED RGB(190,48,44)
#include "ui_native.h"

static void rc0_box(int32_t x,int32_t y,int32_t w,int32_t h,uint16_t c)
{
    cv_rect(x,y,w,1,c); cv_rect(x,y+h-1,w,1,c);
    cv_rect(x,y,1,h,c); cv_rect(x+w-1,y,1,h,c);
}
static void rc0_text(int32_t x,int32_t y,const char *s,uint16_t c)
{ rc1_text_box(x,y,234-x,s,c,0,0); }
static void rc0_num(int32_t x,int32_t y,int32_t n,uint16_t c)
{ char b[12]; fmt_int(b,n); rc0_text(x,y,b,c); }
/* Compact, unbroken 5x7 glyphs expanded to a 6x10 cell. */
static void rc0_small(int32_t x,int32_t y,const char *s,uint16_t c)
{ rc1_text_box(x,y,234-x,s,c,1,0); }
static void rc0_dials(const char *const labels[4],char values[4][16],const int32_t ratio[4])
{
    uint32_t k;
    cv_begin(240,56,RC_CHASSIS);
    cv_rect(0,0,240,1,RC_INK);
    for(k=0;k<4;k++) {
        int32_t cx=30+60*(int32_t)k, r=ratio[k];
        if(!labels[k][0]) continue;
        rc1_text_box(cx-27,3,54,labels[k],RC_BLACK,1,1);
        te_disc(cx,28,11,RC_BLACK); te_disc(cx,28,9,RC_KNOB);
        if(r>=0) {
            uint32_t a=(384+(uint32_t)clamp(r,0,1000)*768/1000)&1023u;
            cv_line(cx,28,cx+((SINE[(a+256)&1023]*7)>>15),28+((SINE[a]*7)>>15),RC_BLUE);
        } else cv_line(cx-4,28,cx+4,28,RC_DIM);
        rc1_text_box(cx-27,41,54,values[k],RC_BLACK,str_len(values[k])<=4u?2:0,1);
    }
    cv_blit(0,184);
}
static void rc0_header(const char *title)
{
    cv_begin(240,36,RC_CHASSIS);
    rc1_text_box(6,2,228,title,RC_BLACK,2,0);
    if(home_pad_screen()&&!vis_on&&!rc0_fx_page&&!rc1_midi_page) {
        uint32_t status=autosave_status();
        rc1_text_box(176,5,58,status==1u?"SAVED":status==2u?"NO SAVE":"WAIT",RC_BLACK,1,2);
    }
    { char n[20];fmt_int(n,song.g[G_BPM]);str_cpy(n+str_len(n)," BPM",sizeof n-str_len(n));rc1_text_box(6,20,68,n,RC_BLACK,0,0);
      str_cpy(n,"PAD ",sizeof n);fmt_int(n+4,drum_pad_sel+1u);rc1_text_box(88,20,58,n,RC_BLACK,0,0); }
    rc1_text_box(168,20,66,song.rec||ft_on?"REC":rec_wait?"ARM":song.playing?"PLAY":"STOP",song.rec||rec_wait||ft_on?RC_RED:RC_BLACK,0,2);
    cv_rect(0,35,240,1,RC_INK); cv_blit(0,0);
}
static void rc0_hint(const char *s)
{
    char line[64];uint32_t n=0;const char *p=ui.msg_t?ui.msg:s;
    uint16_t c=ui.msg_t?(rc0_project_arm||rc0_user_arm||ui.confirm||song.rec||rec_wait?RC_RED:RC_INK):RC_DIM;
    cv_begin(240,24,RC_BG);
    while(p[n]&&p[n]!='\n'&&n+1u<sizeof line){line[n]=p[n];n++;}line[n]=0;
    rc1_text_box(6,p[n]=='\n'?1:7,228,line,c,1,0);
    if(p[n]=='\n')rc1_text_box(6,13,228,p+n+1,c,1,0);
    cv_blit(0,160);
}
static const char *rc0_kit_name(void)
{
    uint32_t id=drum_kit_source_get();
    return id<drum_preset_count() ? drum_preset_name(id) : "CUSTOM / USER";
}
static void rc0_tiles(void)
{
    uint32_t i;
    for(i=0;i<8;i++) {
        int32_t x=5+47*(int32_t)(i%4), y=8+46*(int32_t)(i/4);
        uint16_t fill=pad_lit[i] ? RC_KNOB : RC_PAD;
        char n[8];
        cv_rect(x,y,43,40,fill); rc0_box(x,y,43,40,RC_INK);
        if(i==drum_pad_sel) rc0_box(x+2,y+2,39,36,RC_BLUE);
        { char b[8];fmt_int(b,i+1u);rc1_text_box(x+4,y+3,35,b,RC_PADNUM,1,0); }
        str_cpy(n,drum_custom_sample_name(drum_custom_sample_id_get(i)),sizeof n);
        /* Full name stays below the matrix; compact aliases fit padded tiles. */
        if(n[0]=='S'&&n[1]=='N'){n[2]='R';str_cpy(n+3,n+5,3);}
        rc1_text_box(x+2,y+22,39,n,RC_INK,rc1_ink(n,0,0)<=39?0:1,1);
    }
    rc1_text_box(6,103,110,drum_custom_sample_name(drum_custom_sample_id_get(drum_pad_sel)),RC_INK,0,0);
    rc1_text_box(121,108,113,rc0_kit_name(),RC_DIM,1,2);
    /* Final DAC output statistics, visual envelope and peak hold only. */
    for(i=0;i<2;i++) {
        int32_t x=200+(int32_t)i*18,h=(int32_t)drum_meter_level(8u+i)*81/1000;
        int32_t peak=(int32_t)drum_meter_peak(8u+i)*81/1000;
        rc0_box(x,9,15,88,RC_INK);cv_rect(x+2,11,11,84,RC_PAD);
        if(h)cv_rect(x+2,95-h,11,h,RC_INK);
        if(peak)cv_rect(x+2,95-peak,11,1,RC_INK);
    }
}
static void rc1_hold_draw(const char *title,const char *message,int32_t progress,const char *back_hint)
{
    char values[4][16]={{0}};const char *labels[4]={"","","",""};int32_t ratios[4]={-1,-1,-1,-1};
    rc0_header(title);cv_begin(240,124,RC_BG);
    rc1_text_box(8,14,224,message,RC_INK,0,1);
    rc0_box(12,54,216,20,RC_INK);
    cv_rect(15,57,210*clamp(progress,0,1000)/1000,14,RC_RED);
    rc1_text_box(8,91,224,"RELEASE TO CANCEL",RC_DIM,1,1);
    cv_blit(0,36);rc0_hint(back_hint);rc0_dials(labels,values,ratios);
}
static void rc0_fx_open(void)
{
    uint8_t next=rc0_fx_page==1u ? 2u : 1u;
    go_home();
    rc0_fx_page=next;
    ui.msg_t=0; ui.force=1;
}
static int rc0_project_on(void)
{ return !rc0_fx_page && !ui.home && cur_page()->graph==GR_SLOTS; }
static int rc0_special_input(uint32_t pressed,uint32_t home)
{
    uint32_t k; int32_t s;
    if(!rc0_fx_page && !rc0_project_on()) return 0;
    if(home==1u) { go_home(); return 1; }
    if(pressed & (1u<<panel.btn[B_PLAY])) transport_req=song.playing ? 2 : 1;
    if(rc0_fx_page) {
        uint32_t lane=rc0_fx_page==2u ? 8u : drum_pad_sel;
        uint32_t changed=0;
        if((s=panel_enc(EN_SELECT)) && rc0_fx_page==1u) {
            drum_pad_sel=(uint8_t)clamp((int32_t)drum_pad_sel+s,0,7); lane=drum_pad_sel;
            changed=1;
        }
        if((s=panel_enc(EN_PRESET))) {
            drum_fx_config_t cfg=drum_fx_get(lane);
            drum_fx_set(lane,(uint32_t)clamp((int32_t)cfg.type+s,0,lane==8u ? 6 : 8));
            changed=1;
        }
        panel_enc(EN_ALGO);
        for(k=0;k<4;k++) if((s=panel_enc(EN_K1+k))) {
            drum_fx_config_t cfg=drum_fx_get(lane);
            drum_fx_param_set(lane,k,(int32_t)cfg.p[k]+s);
            ui.hot_col=(uint8_t)k; ui.hot_t=40;
            changed=1;
        }
        if(changed) ui.force=1;
        return 1;
    }
    if((s=panel_enc(EN_SELECT))) {
        song.g[G_SLOT]=(int16_t)clamp(song.g[G_SLOT]+s,1,4); rc0_project_arm=0; ui.force=1;
    }
    panel_enc(EN_PRESET); panel_enc(EN_ALGO);
    /* The four knobs are deliberately inactive: navigation and commit use dedicated controls. */
    for(k=0;k<4;k++) panel_enc(EN_K1+k);
    for(k=0;k<2;k++) if(pressed & (1u<<panel.btn[k ? B_OCTUP : B_OCTDN])) {
        uint32_t slot=(uint32_t)clamp(song.g[G_SLOT]-1,0,3), op=k+1u;
        if(song.playing || song.rec || rec_wait || ft_on || transport_req) { ui_message("STOP FIRST"); continue; }
        if(rc0_project_arm==op && rc0_project_slot==slot && fm1_ms-rc0_confirm_ms<3000u) {
            rc0_project_arm=0;
            if(k) { if(project_used(slot)) project_load(slot); else ui_message("EMPTY PROJECT"); }
            else if(project_save(slot)) ui_message("PROJECT SAVED"); else ui_message("SAVE REFUSED");
        } else {
            rc0_project_arm=(uint8_t)op; rc0_project_slot=(uint8_t)slot; rc0_confirm_ms=fm1_ms;
            ui_message(k ? "OCT+ AGAIN: LOAD" : "OCT- AGAIN: SAVE");
        }
        ui.force=1;
    }
    return 1;
}
static int rc0_draw(void)
{
    static uint32_t last_tick;
    uint32_t i,k,lane=rc0_fx_page==2u ? 8u : drum_pad_sel;
    int midi=rc1_midi_page,home=home_pad_screen()&&!rc1_midi_page, project=rc0_project_on(), rec=rec_settings_active();
    char values[4][16]; const char *labels[4]={"PAD VOL","PAN","SWING","QTZ"};
    int32_t ratio[4]={0,0,0,0};
    if(!rc0_fx_page && !project && !rec && !home && !midi && !on_drum_page()) return 0;
    if(!ui.force && ((rc0_fx_page||home||(!midi&&!project&&!rec&&drum_page==0u)) ? (ui.frame&1u)!=0u : fm1_ms-last_tick<100u)) return 1;
    last_tick=fm1_ms;
    if(midi) rc0_header(midi==1 ? "MIDI ROUTING" : "MIDI NOTES");
    else if(rc0_fx_page) rc0_header(lane==8u ? "MASTER FX" : "LANE FX");
    else if(project) rc0_header("PROJECT MANAGER");
    else if(rec) rc0_header("RECORD SETTINGS");
    else if(home) { char title[16]="PROJECT "; fmt_int(title+8,clamp(song.g[G_SLOT],1,4)); rc0_header(title); }
    else if(drum_page==0u) {
        char t[32]="SEQ TR";fmt_int(t+6,drum_group*4u+1u);str_cpy(t+str_len(t),"-",2);fmt_int(t+str_len(t),drum_group*4u+4u);
        str_cpy(t+str_len(t),"  ",3);fmt_int(t+str_len(t),drum_cursor/16u*16u+1u);
        str_cpy(t+str_len(t),"-",2);fmt_int(t+str_len(t),drum_cursor/16u*16u+16u);rc0_header(t);
    } else rc0_header(drum_page==1u ? "SAMPLE MANAGER" : drum_page==2u ? "PAD EDIT" : "USER KITS");
    cv_begin(240,124,RC_BG);
    if(!midi&&!project&&!rec&&!rc0_fx_page&&(home||drum_page==1u))cv_rect(0,0,240,98,RC_BACK);
    if(midi) {
        static const uint8_t ids[2][4]={{G_DRCH,G_SYNC,G_ROUTE,G_MIDI},{G_CHOTH,0,0,0}};
        static const char *const names[2][4]={{"DRUM CHANNEL","CLOCK SOURCE","MIDI INPUT","MIDI OUTPUT"},{"OTHER CHANNEL","","",""}};
        static const char *const labs[2][4]={{"DRUM CH","SYNC","INPUT","OUTPUT"},{"OTHER CH","","",""}};
        for(k=0;k<4;k++) {
            const char *unit="";uint32_t id=ids[midi==1?0:1][k];
            labels[k]=labs[midi==1?0:1][k];ratio[k]=-1;values[k][0]=0;
            if(!labels[k][0])continue;
            param_format(&GP[id],song.g[id],values[k],&unit);
            if(unit[0]&&str_len(values[k])+str_len(unit)<sizeof values[k])str_cpy(values[k]+str_len(values[k]),unit,sizeof values[k]-str_len(values[k]));
            ratio[k]=RATIO(&GP[id],song.g[id]);
            rc1_text_box(6,8+(int32_t)k*29,132,names[midi==1?0:1][k],RC_INK,0,0);
            rc1_text_box(145,6+(int32_t)k*29,89,values[k],RC_INK,2,2);
        }
    } else if(rc0_fx_page) {
        drum_fx_config_t cfg=drum_fx_get(lane);
        if(lane<8u) { char n[12]="LANE ";fmt_int(n+5,lane+1u);rc1_text_box(6,3,62,n,RC_DIM,1,0);rc1_text_box(76,0,158,drum_custom_sample_name(drum_custom_sample_id_get(lane)),RC_INK,0,0); }
        rc1_text_box(6,23,176,drum_fx_name(lane,cfg.type),RC_INK,0,0);
        rc1_text_box(190,26,44,cfg.type ? "ON" : "OFF",cfg.type ? RC_BLUE : RC_DIM,1,2);
        cv_rect(5,47,230,70,RC_BG); rc0_box(5,47,230,70,RC_INK);
        rc1_text_box(12,52,216,"MASTER OUTPUT",RC_DIM,1,0);
        rc1_fx_wave_draw(11,65,218,47);
        for(k=0;k<4;k++) {
            labels[k]=drum_fx_param_name(lane,cfg.type,k);
            drum_fx_param_text(lane,cfg.type,k,cfg.p[k],values[k],sizeof values[k]); ratio[k]=cfg.type ? cfg.p[k]*10 : -1;
        }
    } else if(project) {
        for(i=0;i<4;i++) {
            int32_t y=4+(int32_t)i*29; uint32_t sel=(uint32_t)clamp(song.g[G_SLOT]-1,0,3)==i;
            if(sel) cv_rect(4,y,232,26,RC_FACE);
            rc1_text_box(8,y+7,12,sel?">":"",RC_BLUE,1,0);rc0_num(26,y+4,i+1,RC_INK);
            rc1_text_box(49,y+4,126,project_used(i) ? "PROJECT" : "EMPTY",RC_INK,0,0);
            rc1_text_box(177,y+7,55,project_used(i)?"SAVED":"--",RC_DIM,1,2);
        }
        for(k=0;k<4;k++) { labels[k]=""; values[k][0]=0; ratio[k]=-1; }
    } else if(rec) {
        static const char *const names[4]={"RECORD MODE","QUANTIZE GRID","COUNT-IN","METRONOME"};
        static const char *const modes[2]={"OVERDUB","REPLACE"}, *const grids[5]={"OFF","1/4","1/8","1/16","1/32"};
        static const char *const counts[3]={"OFF","1 BAR","2 BARS"};
        labels[0]="MODE"; labels[1]="GRID"; labels[2]="COUNT"; labels[3]="METRO";
        str_cpy(values[0],modes[rec_mode_get()],16); str_cpy(values[1],grids[rec_grid_get()],16);
        str_cpy(values[2],counts[rec_count_get()],16); str_cpy(values[3],song.g[G_CLOCK]==0 ? "OFF" : song.g[G_CLOCK]==1 ? "REC" : "ON",16);
        ratio[0]=rec_mode_get()*1000; ratio[1]=rec_grid_get()*250; ratio[2]=rec_count_get()*500; ratio[3]=song.g[G_CLOCK]*500;
        for(k=0;k<4;k++) { rc1_text_box(6,9+k*29,132,names[k],RC_INK,0,0);rc1_text_box(145,7+k*29,89,values[k],RC_INK,2,2); }
        str_cpy(values[0],rec_mode_get()?"REPL":"OVER",16); /* full mode above, unambiguous knob value */
    } else if(home || drum_page==1u) {
        rc0_tiles();
        fmt_int(values[0],drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*100/127); ratio[0]=drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*1000/127;
        fmt_int(values[1],drum_pad_mix_get(lane,DRUM_MIX_PAN)); ratio[1]=(drum_pad_mix_get(lane,DRUM_MIX_PAN)+64)*1000/127;
        fmt_int(values[2],song.g[G_SWING]); ratio[2]=song.g[G_SWING]*10;
        fmt_int(values[3],drum_qtz_strength_get()); ratio[3]=drum_qtz_strength_get()*10;
        if(!home) {
            labels[0]="CAT"; labels[1]="SAMPLE"; labels[2]="PAD LVL"; labels[3]="PREV VOL";
            str_cpy(values[0],DRUM_BROWSER_CATEGORIES[drum_browser_category],16);
            str_cpy(values[1],drum_custom_sample_name(drum_browser_sample),16);
            fmt_int(values[2],drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*100/127);
            fmt_int(values[3],drum_preview_level*100/127);
            ratio[0]=drum_browser_category*250; ratio[1]=(drum_browser_sample-DRUM_BROWSER_FIRST[drum_browser_category])*1000/(DRUM_BROWSER_COUNT[drum_browser_category]-1u);
            ratio[2]=drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*1000/127; ratio[3]=drum_preview_level*1000/127;
        }
    } else if(drum_page==0u) {
        uint32_t bank=drum_cursor/16u,len=trk_len(TDRUM);
        for(i=0;i<4;i++) {
            uint32_t ln=drum_group*4u+i; int32_t y=3+(int32_t)i*30;
            if(ln==drum_pad_sel) cv_rect(2,y,236,28,RC_FACE);
            if(ln==drum_pad_sel)cv_rect(3,y,3,28,RC_INK);
            { char n[4];fmt_int(n,ln+1u);rc1_text_box(10,y+3,12,n,RC_DIM,1,0); }
            rc1_text_box(27,y,118,drum_custom_sample_name(drum_custom_sample_id_get(ln)),RC_INK,0,0);
            /* Measured post-lane output, not the K1 setpoint. */
            rc0_box(154,y+3,80,9,RC_INK);
            {int32_t bar=(int32_t)drum_meter_level(ln)*76/1000,peak=(int32_t)drum_meter_peak(ln)*76/1000;
             if(bar)cv_rect(156,y+5,bar,5,RC_INK);
             if(peak)cv_rect(156+peak,y+4,1,7,RC_INK);}
            for(k=0;k<16;k++) {
                uint32_t p=bank*16u+k; int32_t x=7+(int32_t)k*14;
                uint16_t c=p<len && dstep_has(&TDRUM->dstep[p],ln) ? RC_INK : RC_DIM;
                if(p<len && dstep_has(&TDRUM->dstep[p],ln)) cv_rect(x,y+18,6,7,c); else te_disc(x+2,y+21,1,c);
                if(ln==drum_pad_sel && p==drum_cursor)rc0_box(x-2,y+16,10,11,RC_INK);
                if(p==TDRUM->seq_idx && song.playing) cv_rect(x-1,y+26,8,2,RC_INK);
            }
        }
        labels[0]="LEVEL"; labels[1]="STEPS";
        fmt_int(values[0],drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*100/127); fmt_int(values[1],len);
        fmt_int(values[2],song.g[G_SWING]); fmt_int(values[3],drum_qtz_strength_get());
        ratio[0]=drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*1000/127; ratio[1]=(len-1u)*1000/63; ratio[2]=song.g[G_SWING]*10; ratio[3]=drum_qtz_strength_get()*10;
    } else if(drum_page==2u) {
        uint32_t id=drum_custom_sample_id_get(lane); int32_t end=drum_pad_edit_get(lane,DRUM_EDIT_END),start=drum_pad_edit_get(lane,DRUM_EDIT_START);
        rc1_text_box(6,1,228,drum_custom_sample_name(id),RC_INK,0,0);
        cv_rect(5,23,230,94,RC_INK); rc0_box(5,23,230,94,RC_INK);
        {const int8_t *wave=id<55u?DRUM_WAVEFORM[id]:drum_user_waveform(id);if(wave)for(i=0;i<96;i++)cv_line(14+i*216u/95u,74-wave[i*2u]*35/127,14+i*216u/95u,74-wave[i*2u+1u]*35/127,RC_BG);}
        cv_line(14+start*216/100,24,14+start*216/100,115,RC_BG);cv_line(14+end*216/100,24,14+end*216/100,115,RC_BG);
        {char b[12]="S ";fmt_int(b+2,start);rc1_text_box(18,28,84,b,RC_BG,0,0);str_cpy(b,"E ",sizeof b);fmt_int(b+2,end);rc1_text_box(130,28,98,b,RC_BG,0,2);}
        labels[0]="TUNE"; labels[1]="START"; labels[2]="END"; labels[3]="DECAY";
        fmt_int(values[0],drum_pad_edit_get(lane,DRUM_EDIT_TUNE)); fmt_int(values[1],start);
        fmt_int(values[2],end); fmt_int(values[3],drum_pad_edit_get(lane,DRUM_EDIT_DECAY));
        ratio[0]=(drum_pad_edit_get(lane,DRUM_EDIT_TUNE)+24)*1000/48; ratio[1]=start*10; ratio[2]=end*10; ratio[3]=drum_pad_edit_get(lane,DRUM_EDIT_DECAY)*1000/127;
    } else {
        for(i=0;i<4;i++) { int32_t y=3+i*29; if(i==drum_user_slot) cv_rect(3,y,234,26,RC_FACE); rc0_text(8,y+4,i==drum_user_slot ? "> USER" : "  USER",RC_INK); rc0_num(66,y+4,i+1,RC_INK); rc0_text(103,y+4,drum_user_valid[i] ? "SAVED" : "EMPTY",RC_DIM); }
        labels[0]="LEVEL"; labels[1]="PAN"; labels[2]="TUNE"; labels[3]="DECAY";
        fmt_int(values[0],drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*100/127); fmt_int(values[1],drum_pad_mix_get(lane,DRUM_MIX_PAN));
        fmt_int(values[2],drum_pad_edit_get(lane,DRUM_EDIT_TUNE)); fmt_int(values[3],drum_pad_edit_get(lane,DRUM_EDIT_DECAY));
        ratio[0]=drum_pad_mix_get(lane,DRUM_MIX_LEVEL)*1000/127; ratio[1]=(drum_pad_mix_get(lane,DRUM_MIX_PAN)+64)*1000/127;
        ratio[2]=(drum_pad_edit_get(lane,DRUM_EDIT_TUNE)+24)*1000/48; ratio[3]=drum_pad_edit_get(lane,DRUM_EDIT_DECAY)*1000/127;
    }
    cv_blit(0,36);
    if(midi)rc0_hint("SELECT:PAGE HOME:BACK");
    else if(rc0_fx_page) rc0_hint(lane==8u ? "PRESET:FX/OFF HOME:BACK" : "SELECT:LANE PRESET:FX/OFF");
    else if(project) rc0_hint("SELECT:PROJECT\nOCT-:SAVE x2 OCT+:LOAD x2");
    else if(rec) rc0_hint("ARP:SETTINGS HOME:BACK");
    else if(home&&autosave_status()==2u)rc0_hint(autosave_error_text());
    else if(home) rc0_hint("SELECT:PAD ALGO:BPM HOLD HOME:MENU\nPRESET:KIT ENV:USER GLO:MIDI");
    else if(drum_page==1u) rc0_hint("SELECT:PAD OCT-:PREVIEW\nOCT+:ASSIGN HOME:BACK");
    else if(drum_page==2u) rc0_hint("SELECT:PAD  START < END\nHOME:BACK");
    else if(drum_page==3u) rc0_hint("SELECT:KIT ALGO:PAD\nOCT-:LOAD OCT+:SAVE x2");
    else rc0_hint("SELECT:LANE ALGO:PAGE\nEDIT+OCT-:UNDO OCT+:REDO");
    rc0_dials(labels,values,ratio); return 1;
}
