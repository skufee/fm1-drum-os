/* SPDX-License-Identifier: GPL-3.0-only */
/* RC0 application DSP. Uses SLOOP fixed-point primitives, never peripheral APIs.
 * One mono processor per logical lane AFTER its voices sum; one stereo master.
 * Delay: eight independent 4096-byte companded rings. Adaptive decimation keeps
 * every musical division in bounds at 20..300 BPM. This is a bandwidth tradeoff,
 * not a full-rate delay. Ring fill counters invalidate old tails without a bulk
 * memset in the audio interrupt. No allocation, floating point or PCM decoding. */
#include "drum_fx.h"
#define DFX_RING 4096u
static drum_fx_config_t drum_fx_config[9];
typedef struct {
    int32_t lp, bp, env, slow, held, accum, delay_lp, delay_target;
    int32_t y1, y2, dc_x, dc_y, wet;
    uint32_t write, fill, phase, decim, length, count, noise, wow, flutter;
    int32_t smooth[4], fc, damping, rc, rd;
} drum_fx_state_t;
static drum_fx_state_t dfx_state[10]; /* lanes 0..7, master L/R 8/9 */
static int8_t dfx_delay[8][DFX_RING] __attribute__((section(".pool")));
static int16_t dfx_wow[2][256] __attribute__((section(".pool")));
static const char *const DFX_LNAME[9] = {"OFF","TEMPO DELAY","DISTORTION","SATURATION","FILTER","BIT CRUSH","TRANSIENT","RESONATOR","PUNCH DRIVE"};
static const char *const DFX_MNAME[7] = {"OFF","VINYL SIM","TAPE SIM","SATURATOR","BIT CRUSH","COMPRESSOR","ISOLATOR"};
static const char *const DFX_LPARAM[9][4] = {
 {"--","--","--","--"},{"TIME","FEEDBACK","TONE","MIX"},
 {"GAIN","SHAPE","TONE","MIX"},{"DRIVE","TONE","CHAR","MIX"},
 {"CUTOFF","RES","TYPE","DRIVE"},{"BITS","RATE","FILTER","MIX"},
 {"ATTACK","SUSTAIN","SPEED","MIX"},{"FREQ","DECAY","COLOR","MIX"},
 {"PUNCH","BODY","DRIVE","MIX"}};
static const char *const DFX_MPARAM[7][4] = {
 {"--","--","--","--"},{"TONE","NOISE","WOW/FLUT","MIX"},
 {"DRIVE","TONE","AGE","MIX"},{"DRIVE","TONE","CHAR","MIX"},
 {"BITS","RATE","LPF","MIX"},{"THRESH","ATTACK","RELEASE","MIX"},
 {"CUTOFF","RES","MODE","MIX"}};
static uint32_t dfx_enum(uint32_t p, uint32_t count) { return p >= 100u ? count-1u : p * count / 100u; }
static int32_t dfx_clip(int32_t x) { return clamp(x,-131071,131071); }
static int32_t dfx_mul(int32_t x, int32_t g) { return (int32_t)((int64_t)x*g >> 15); }
static uint32_t dfx_hz(uint32_t p) { return 30u * (uint32_t)pow2_q16((int32_t)p*16) / 65536u; }
static uint32_t dfx_cutoff(uint32_t p) { return 30u * pow2_q16((int32_t)(p*1275u/100u)) / 65536u; }
drum_fx_config_t drum_fx_get(uint32_t lane)
{
    drum_fx_config_t c = {0,{0,0,0,0}};
    /* Main-loop readers only; writers publish under IRQ masking. Do not
     * unmask interrupts inside callers' existing capture critical sections. */
    if (lane < 9u) c = drum_fx_config[lane];
    return c;
}
const char *drum_fx_name(uint32_t lane,uint32_t type)
{ return lane==8u ? DFX_MNAME[type<=6u?type:0] : DFX_LNAME[type<=8u?type:0]; }
const char *drum_fx_param_name(uint32_t lane,uint32_t type,uint32_t p)
{ return p>=4u ? "--" : lane==8u ? DFX_MPARAM[type<=6u?type:0][p] : DFX_LPARAM[type<=8u?type:0][p]; }
/* Small formatter independent of UI/libc; enum controls show the actual choice. */
void drum_fx_param_text(uint32_t lane,uint32_t type,uint32_t p,uint32_t value,char *out,unsigned cap)
{
    static const char *const divs[8]={"1/4","1/8","1/8D","1/8T","1/16","1/16D","1/16T","1/32"};
    static const char *const shapes[4]={"HARD","FUZZ","FOLD","ASYM"};
    static const char *const chars[3]={"SOFT","HARD","ASYM"};
    static const char *const modes[3]={"LPF","HPF","BPF"};
    const char *s=0,*unit=""; char b[12]; unsigned i=0,j=0; uint32_t n=value; int negative=0;
    if (!type) s="--";
    else if (lane<8u && type==1u && p==0u) s=divs[dfx_enum(value,8)];
    else if (lane<8u && type==2u && p==1u) s=shapes[dfx_enum(value,4)];
    else if (((lane<8u && type==3u)||(lane==8u && type==3u)) && p==2u) s=chars[dfx_enum(value,3)];
    else if (((lane<8u && type==4u)||(lane==8u && type==6u)) && p==2u) s=modes[dfx_enum(value,3)];
    if (!s) {
        if (((lane<8u && type==5u)||(lane==8u && type==4u)) && p==0u) n=1u+value*15u/100u;
        if (lane<8u && type==1u && p==1u) n=value*95u/100u;
        if (((lane<8u && type==4u)||(lane==8u && type==6u)) && p==0u) { n=dfx_cutoff(value); unit="H"; }
        if (lane<8u && type==7u && p==0u) { n=dfx_hz(value); unit="H"; }
        if (lane<8u && type==7u && p==1u) { n=(441u+value*661u)*1000u/FS; unit="MS"; }
        if (((lane<8u && type==5u)||(lane==8u && type==4u)) && p==1u) { n=FS/(1u+(100u-value)*31u/100u); unit="H"; }
        if (lane<8u && type==6u && p<2u) { negative=value<50u; n=(value<50u?50u-value:value-50u)*2u; }
        if (lane==8u && type==5u && p==1u) { n=(4u+value*40u)*1000u/FS; unit="MS"; }
        if (lane==8u && type==5u && p==2u) { n=(400u+value*160u)*1000u/FS; unit="MS"; }
        do { b[i++]=(char)('0'+n%10u); n/=10u; } while(n && i<10u);
        if(negative && cap>1u)out[j++]='-';
        { unsigned k=0; while(k<i && j+1u<cap) { out[j++]=b[i-k-1u]; k++; } }
        while(*unit && j+1u<cap)out[j++]=*unit++;
    } else while(s[j] && j+1u<cap) { out[j]=s[j]; j++; }
    if(cap) out[j]=0;
}
static void dfx_clear(uint32_t lane)
{
    memset(&dfx_state[lane],0,sizeof dfx_state[lane]);
    dfx_state[lane].noise=0x6d2b79f5u+lane*7919u;
    if(lane==8u) { memset(&dfx_state[9],0,sizeof dfx_state[9]); dfx_state[9].noise=0x79a13u; }
}
void drum_fx_restore(uint32_t lane,const drum_fx_config_t *cfg)
{
    uint32_t p;
    if(lane>=9u || !cfg || cfg->type>(lane==8u?6u:8u)) return;
    for(p=0;p<4;p++) if(cfg->p[p]>100u) return;
    fm1_irq_off(); drum_fx_config[lane]=*cfg; dfx_clear(lane); fm1_irq_on();
}
void drum_fx_set(uint32_t lane,uint32_t type)
{
    drum_fx_config_t c={0,{50,50,50,50}};
    if(lane>=9u || type>(lane==8u?6u:8u)) return;
    c.type=(uint8_t)type;
    if(lane<8u && type==1u) { c.p[0]=12; c.p[1]=35; c.p[2]=65; c.p[3]=30; }
    if(lane<8u && type==4u) { c.p[0]=85; c.p[1]=15; c.p[2]=0; c.p[3]=0; }
    if((lane<8u && type==5u)||(lane==8u && type==4u)) { c.p[0]=70; c.p[1]=75; c.p[2]=80; }
    if(lane==8u && type==1u) { c.p[0]=60; c.p[1]=10; c.p[2]=30; }
    drum_fx_restore(lane,&c);
    drum_mark_custom();
}
void drum_fx_param_set(uint32_t lane,uint32_t p,int32_t value)
{
    if(lane>=9u||p>=4u)return;
    fm1_irq_off(); drum_fx_config[lane].p[p]=(uint8_t)clamp(value,0,100); fm1_irq_on();
    drum_mark_custom();
}
void drum_fx_reset(void)
{
    uint32_t i; drum_fx_config_t c={0,{0,0,0,0}};
    for(i=0;i<9;i++) drum_fx_restore(i,&c);
}
int drum_fx_lanes_active(void)
{ uint32_t i; for(i=0;i<8;i++) if(drum_fx_config[i].type) return 1; return 0; }
/* Sign-magnitude companding: 4-bit mantissa, 3-bit exponent; exact silence. */
static int8_t dfx_pack(int32_t x)
{
    uint32_t a=(uint32_t)(x<0?-x:x)>>2,e=0;
    if(a>32767u)a=32767u;
    while(a>255u && e<7u) { a>>=1; e++; }
    a=(e<<4)|(a>>4); if(a>127u)a=127u;
    return (int8_t)(x<0?-(int32_t)a:(int32_t)a);
}
static int32_t dfx_unpack(int8_t x)
{
    uint32_t a=(uint32_t)(x<0?-(int32_t)x:x);
    int32_t y=(int32_t)((a&15u)<<4) << (a>>4);
    return x<0?-(y<<2):(y<<2);
}
static int32_t dfx_tone(drum_fx_state_t *s,int32_t x,uint32_t tone)
{ s->lp+=dfx_mul(x-s->lp,600+(int32_t)tone*300); return s->lp; }
static int32_t dfx_shape(int32_t x,uint32_t drive,uint32_t shape,int sat)
{
    int32_t y=(int32_t)((int64_t)x*(100u+drive*(sat?4u:12u))/100);
    if(sat) {
        if(shape==0u) return softclip(clamp(y,-262143,262143));
        if(shape==1u) return clamp(y,-24000,24000);
        return softclip(clamp(y+(y>0?y/3:0),-262143,262143));
    }
    if(shape==0u) return clamp(y,-22000,22000);
    if(shape==1u) return softclip(clamp(y*2,-524286,524286));
    if(shape==2u) { y=(y+32768)%131072; if(y<0)y+=131072; return y<65536?y-32768:98304-y; }
    return clamp(y,-12000,28000);
}
static int32_t dfx_filter(drum_fx_state_t *s,int32_t x,const uint32_t *p)
{
    /* Stable two-integrator SVF, f <= .45, damping .25..1.5; state bounded. */
    int32_t hp=dfx_clip(x-s->lp-dfx_mul(s->bp,s->damping));
    s->bp=dfx_clip(s->bp+dfx_mul(hp,s->fc));
    s->lp=dfx_clip(s->lp+dfx_mul(s->bp,s->fc));
    return dfx_enum(p[2],3)==0u?s->lp:dfx_enum(p[2],3)==1u?hp:s->bp;
}
static int32_t dfx_delay_sample(uint32_t lane,drum_fx_state_t *s,int32_t x,const uint32_t *p,uint32_t bpm)
{
    uint32_t dec=s->decim,len=s->length;
    int32_t y;
    (void)bpm;
    /* Bandlimit the downsampled input; no sample/hold aliasing by design. */
    s->delay_lp+=(x-s->delay_lp)/(int32_t)dec;
    if(++s->count>=dec) {
        s->count=0;
        y=s->fill>=len?dfx_unpack(dfx_delay[lane][(s->write+DFX_RING-len)&(DFX_RING-1u)]):0;
        s->held+=dfx_mul(y-s->held,500+(int32_t)p[2]*300);
        dfx_delay[lane][s->write]=dfx_pack(dfx_clip(s->delay_lp+dfx_mul(s->held,(int32_t)p[1]*311)));
        s->write=(s->write+1u)&(DFX_RING-1u); if(s->fill<DFX_RING)s->fill++;
        s->delay_target=s->held;
    }
    /* Reconstruct at audio rate rather than stepping the wet signal. */
    s->accum+=(s->delay_target-s->accum)/(int32_t)dec;
    return s->accum;
}
static int32_t dfx_dc(drum_fx_state_t *s,int32_t x)
{ int32_t y=dfx_clip(x-s->dc_x+dfx_mul(s->dc_y,32690)); s->dc_x=x; s->dc_y=y; return y; }
static void dfx_params(drum_fx_state_t *s,const drum_fx_config_t *c,uint32_t p[4])
{
    uint32_t j;
    for(j=0;j<4;j++) { int32_t target=(int32_t)c->p[j]*256; int32_t d=target-s->smooth[j];
        if(d && d<16 && d>-16)s->smooth[j]=target; else s->smooth[j]+=d/16;
        p[j]=(uint32_t)s->smooth[j]/256u; }
}
static void dfx_prepare(drum_fx_state_t *s,uint32_t type,uint32_t p[4],uint32_t bpm,int master)
{
    if((!master && type==4u)||(master && type==6u)) {
        s->fc=clamp((int32_t)(dfx_cutoff(p[0])*205887u/FS),60,14745);
        s->damping=49152-(int32_t)p[1]*400;
    }
    if(!master && type==1u) {
        static const uint8_t num[8]={24,12,18,8,6,9,4,3};
        uint32_t samples=(FS*60u/(bpm<20u?20u:bpm))*num[dfx_enum(p[0],8)]/24u;
        uint32_t dec=(samples+DFX_RING-2u)/(DFX_RING-1u),len;
        if(!dec)dec=1; len=samples/dec; if(!len)len=1;
        /* Fixed ring + movable read tap retains tails across clock-estimator
         * jitter/tempo changes. The reconstruction pole smooths tap changes;
         * a rate change can colour/pitch the tail, like retuning a tape delay. */
        s->length=len; s->decim=dec;
    }
    if(!master && type==7u) {
        uint32_t hz=dfx_hz(p[0]),ph=(uint32_t)(((uint64_t)hz<<32)/FS);
        uint32_t duration=441u+p[1]*661u;
        int32_t radius=1073741824-(int32_t)(1073741824u/duration);
        s->rc=(int32_t)(((int64_t)sine_i(ph+0x40000000u)*2*radius)>>15);
        s->rd=(int32_t)(((int64_t)radius*radius)>>30);
    }
}
static int32_t dfx_crush(drum_fx_state_t *s,int32_t x,const uint32_t *p)
{
    uint32_t hold=1u+(100u-p[1])*31u/100u,shift=15u-p[0]*15u/100u;
    int32_t y;
    if(!s->count) { s->held=x; s->count=hold; } s->count--;
    y=s->held<0?-(((-s->held)>>shift)<<shift):(s->held>>shift)<<shift;
    return dfx_tone(s,y,p[2]);
}
void drum_fx_lane_process(uint32_t lane,int32_t *b,uint32_t n,uint32_t bpm)
{
    drum_fx_config_t c; drum_fx_state_t *s; uint32_t p[4],i; int32_t x,y,mix;
    if(lane>=8u || !(c=drum_fx_config[lane]).type)return;
    s=&dfx_state[lane]; dfx_params(s,&c,p);
    if(c.type==1u)p[0]=c.p[0]; /* division is discrete, not interpolated */
    if(c.type==2u)p[1]=c.p[1];
    if(c.type==3u||c.type==4u)p[2]=c.p[2];
    dfx_prepare(s,c.type,p,bpm,0); mix=(int32_t)p[3]*32768/100;
    for(i=0;i<n;i++) {
        x=dfx_clip(b[i]); y=x;
        switch(c.type) {
        case 1: y=dfx_delay_sample(lane,s,x,p,bpm); break;
        case 2: y=dfx_tone(s,dfx_shape(x,p[0],dfx_enum(p[1],4),0),p[2]); break;
        case 3: y=dfx_tone(s,dfx_shape(x,p[0],dfx_enum(p[2],3),1),p[1]); break;
        case 4: y=dfx_filter(s,dfx_shape(x,p[3],0,1),p); mix=32768; break;
        case 5: y=dfx_crush(s,x,p); break;
        case 6: {
            int32_t a=x<0?-x:x,g;
            s->env+=(a-s->env)/(int32_t)(8u+p[2]*4u);
            s->slow+=(a-s->slow)/(int32_t)(256u+p[2]*40u);
            g=32768+(int32_t)(p[0]-50)*400*(s->env>s->slow?1:0)+(int32_t)(p[1]-50)*300*(s->env<=s->slow?1:0);
            y=dfx_mul(x,clamp(g,4096,65536)); break; }
        case 7: {
            y=dfx_clip(x/4+(int32_t)(((int64_t)s->rc*s->y1-(int64_t)s->rd*s->y2)>>30));
            s->y2=s->y1; s->y1=y;
            y=dfx_tone(s,y,p[2]); break; }
        case 8: {
            int32_t a=x<0?-x:x,g;
            s->env+=(a-s->env)/(a>s->env?16:1024);
            s->slow+=(a-s->slow)/2048;
            g=32768+(s->env>s->slow?(int32_t)p[0]*300:0);
            y=dfx_tone(s,x,20u+p[1]*3u/4u);
            y=dfx_shape(dfx_mul(x,g)+dfx_mul(y,(int32_t)p[1]*160),p[2],0,1); break; }
        }
        y=dfx_dc(s,dfx_clip(y));
        /* Wet ramps over each block even when selecting a new effect. */
        { int32_t d=mix-s->wet; s->wet+=(d<32&&d>-32)?d:d/32; }
        b[i]=b[i]+dfx_mul(y-b[i],s->wet);
    }
}
static int32_t dfx_modulate(drum_fx_state_t *s,uint32_t channel,int32_t x,uint32_t amount)
{
    uint32_t delay,idx; int32_t frac,a,b;
    dfx_wow[channel][s->write&255u]=(int16_t)clamp(x,-32768,32767);
    s->wow+=35791u; s->flutter+=601295u; /* .3675 Hz and 6.17 Hz */
    frac=(64<<8)+(int32_t)amount*(sine_i(s->wow)/64+sine_i(s->flutter)/256)/100;
    delay=(uint32_t)frac>>8; frac&=255;
    idx=(s->write-delay)&255u; a=dfx_wow[channel][idx]; b=dfx_wow[channel][(idx-1u)&255u];
    s->write++; if(s->fill<256u)s->fill++;
    return s->fill>80u?a+(b-a)*frac/256:0;
}
void drum_fx_master_process(int32_t *l,int32_t *r,uint32_t n,uint32_t bpm)
{
    drum_fx_config_t c=drum_fx_config[8]; uint32_t ch,i,p[4]; int32_t *b;
    (void)bpm; if(!c.type)return;
    for(ch=0;ch<2;ch++) {
        drum_fx_state_t *s=&dfx_state[8u+ch]; b=ch?r:l; dfx_params(s,&c,p);
        if(c.type==3u||c.type==6u)p[2]=c.p[2];
        dfx_prepare(s,c.type,p,bpm,1);
        for(i=0;i<n;i++) {
            int32_t x=dfx_clip(b[i]),y=x,mix=(int32_t)p[3]*32768/100;
            switch(c.type) {
            case 1: {
                int32_t nz;
                s->noise=s->noise*1664525u+1013904223u; nz=(int32_t)(s->noise>>16)-32768;
                y=dfx_modulate(s,ch,x,p[2]);
                y=dfx_tone(s,softclip(y),p[0]);
                y+=nz*(int32_t)p[1]/3200; break; }
            case 2:
                y=dfx_modulate(s,ch,x,p[2]/2u);
                y=dfx_tone(s,dfx_shape(y,p[0],0,1),p[1]);
                /* Age also adds deterministic low-level tape hiss, independent of drive. */
                s->noise=s->noise*1664525u+1013904223u;
                y+=((int32_t)(s->noise>>16)-32768)*(int32_t)p[2]/12800; break;
            case 3: y=dfx_tone(s,dfx_shape(x,p[0],dfx_enum(p[2],3),1),p[1]); break;
            case 4: y=dfx_crush(s,x,p); break;
            case 5: {
                /* Fixed 4:1 ratio above threshold, bounded +3 dB compensation. */
                int32_t a=x<0?-x:x,threshold=28000-(int32_t)p[0]*260,g=32768;
                s->env+=(a-s->env)/(int32_t)(a>s->env?4u+p[1]*40u:400u+p[2]*160u);
                if(s->env>threshold)g=((threshold+(s->env-threshold)/4)*8192)/(s->env/4?s->env/4:1);
                y=dfx_mul(x,g); y=dfx_mul(y,32768+(int32_t)p[0]*120); break; }
            case 6: y=dfx_filter(s,x,p); break;
            }
            y=dfx_dc(s,dfx_clip(y));
            { int32_t d=mix-s->wet; s->wet+=(d<32&&d>-32)?d:d/32; }
            b[i]=b[i]+dfx_mul(y-b[i],s->wet);
        }
    }
}
