/* SPDX-License-Identifier: GPL-3.0-only */
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void warm(uint32_t lane)
{
    int32_t l[CTL],r[CTL]; unsigned b;
    for(b=0;b<256;b++) {
        memset(l,0,sizeof l);memset(r,0,sizeof r);
        if(lane<8) drum_fx_lane_process(lane,l,CTL,120);
        else drum_fx_master_process(l,r,CTL,120);
    }
}
static uint64_t runfx(uint32_t lane,uint32_t type,int param,int value)
{
    drum_fx_config_t c={(uint8_t)type,{63,57,61,83}};
    uint32_t b,i; int32_t l[CTL],r[CTL]; uint64_t hash=1469598103934665603ull,energy=0;
    if(param>=0)c.p[param]=(uint8_t)value;
    drum_fx_restore(lane,&c);warm(lane);
    for(b=0;b<1800;b++) {
        for(i=0;i<CTL;i++) {
            uint32_t pos=b*CTL+i;
            int32_t env=(pos%12000u)<600u?24000:((pos%12000u)<6000u?8000:0);
            l[i]=sine_i(pos*0x04f34567u)*env/32768;
            r[i]=l[i]/2;
        }
        if(lane<8)drum_fx_lane_process(lane,l,CTL,120);else drum_fx_master_process(l,r,CTL,120);
        for(i=0;i<CTL;i++) {
            assert(l[i]>=-131071 && l[i]<=131071);
            energy+=(uint64_t)(l[i]<0?-l[i]:l[i]);
            hash=(hash^(uint32_t)l[i])*1099511628211ull;
        }
    }
    assert(energy>1000u);return hash;
}
int main(void)
{
    uint32_t lane,t,p,b,i; int32_t l[CTL],r[CTL],before[CTL],rv[CTL],dl[CTL];
    host_tracks_init(); drum_fx_reset();
    for(i=0;i<CTL;i++)before[i]=l[i]=r[i]=(int32_t)i*12000-200000;
    drum_fx_lane_process(0,l,CTL,120);drum_fx_master_process(l,r,CTL,120);
    assert(!memcmp(l,before,sizeof l)&&!memcmp(r,before,sizeof r));
    /* Fresh effect states must preserve silence when intentional noise is off. */
    for(lane=0;lane<=8;lane+=8)for(t=1;t<=(lane==8?6u:8u);t++) {
        drum_fx_config_t c={(uint8_t)t,{50,50,50,100}};
        if(lane==8&&t==1)c.p[1]=0;
        if(lane==8&&t==2)c.p[2]=0;
        drum_fx_restore(lane,&c);
        for(b=0;b<200;b++) {
            memset(l,0,sizeof l);memset(r,0,sizeof r);
            if(lane<8)drum_fx_lane_process(lane,l,CTL,120);else drum_fx_master_process(l,r,CTL,120);
            for(i=0;i<CTL;i++)assert(l[i]==0&&r[i]==0);
        }
    }
    { drum_fx_config_t c={2,{50,50,50,0}};
      drum_fx_restore(0,&c);warm(0);
      for(i=0;i<CTL;i++)l[i]=before[i];
      drum_fx_lane_process(0,l,CTL,120);assert(!memcmp(l,before,sizeof l));
      c.type=3;drum_fx_restore(8,&c);warm(8);
      for(i=0;i<CTL;i++)l[i]=r[i]=before[i];
      drum_fx_master_process(l,r,CTL,120);assert(!memcmp(l,before,sizeof l)&&!memcmp(r,before,sizeof r));
    }
    for(lane=0;lane<=8;lane+=8) for(t=1;t<=(lane==8?6u:8u);t++) {
        uint64_t base=runfx(lane,t,-1,0);
        for(p=0;p<4;p++) { uint64_t a=runfx(lane,t,(int)p,0),z=runfx(lane,t,(int)p,100);
            if(a==z) { fprintf(stderr,"PARAM NO CHANGE lane=%u type=%u p=%u\n",lane,t,p);return 1; }
        }
        printf("FX %s %s energy/4 parameters PASS hash=%llu\n",lane==8?"MASTER":"LANE",drum_fx_name(lane,t),(unsigned long long)base);
    }
    /* Distortion on lane0 must not change lane1 at all; independent state. */
    drum_fx_reset();drum_fx_set(0,2);
    for(i=0;i<CTL;i++)l[i]=before[i]=i*190;
    drum_fx_lane_process(1,l,CTL,120);assert(!memcmp(l,before,sizeof l));
    /* A bypass lane keeps its exact captured pan/mute when another FX starts. */
    { unsigned char snapshot[sizeof drums]; int32_t oldl[CTL],oldr[CTL];
      drum_fx_reset();drum_preset_select(0);memset(&drums,0,sizeof drums);drums.set=-2;
      drum_on(LANE_NOTE[0],100);drum_pad_mix_set(0,DRUM_MIX_PAN,50);drum_pad_mix_set(0,DRUM_MIX_MUTE,1);
      memcpy(snapshot,&drums,sizeof drums);
      memset(l,0,sizeof l);memset(r,0,sizeof r);memset(rv,0,sizeof rv);memset(dl,0,sizeof dl);
      drums_render(l,r,rv,dl,CTL);memcpy(oldl,l,sizeof l);memcpy(oldr,r,sizeof r);
      memcpy(&drums,snapshot,sizeof drums);drum_fx_set(7,2);
      memset(l,0,sizeof l);memset(r,0,sizeof r);memset(rv,0,sizeof rv);memset(dl,0,sizeof dl);
      drums_render(l,r,rv,dl,CTL);assert(!memcmp(oldl,l,sizeof l)&&!memcmp(oldr,r,sizeof r));
    }
    /* Choke follows immutable sample categories, not old GM lane positions. */
    drum_preset_select(0);memset(&drums,0,sizeof drums);drums.set=-2;
    drum_on(LANE_NOTE[2],100);assert(drums.v[0].active&&drums.v[0].s[4]==26);
    drum_on(LANE_NOTE[1],100);for(i=0;i<NDRUM;i++)assert(!drums.v[i].active||drums.v[i].s[4]!=26);
    memset(&drums,0,sizeof drums);drum_on(LANE_NOTE[5],100);drum_on(LANE_NOTE[4],100);
    { unsigned active=0;for(i=0;i<NDRUM;i++)active+=drums.v[i].active!=0;assert(active==2); }
    /* Candidate preview remains audible on a muted lane with its FX enabled. */
    { uint64_t energy=0;uint32_t old_id;
      drum_preset_select(0);memset(&drums,0,sizeof drums);drums.set=-2;drum_pad_sel=0;
      old_id=drum_custom_sample_id_get(0);drum_pad_mix_set(0,DRUM_MIX_MUTE,1);drum_fx_set(0,2);
      drum_preview(19,127);assert(drum_custom_sample_id_get(0)==old_id&&drum_pad_mix_get(0,DRUM_MIX_MUTE)==1);
      for(b=0;b<200;b++){memset(l,0,sizeof l);memset(r,0,sizeof r);memset(rv,0,sizeof rv);memset(dl,0,sizeof dl);
        drums_render(l,r,rv,dl,CTL);for(i=0;i<CTL;i++)energy+=(uint64_t)(l[i]<0?-l[i]:l[i]);}
      assert(energy>1000);
    }
    /* Eight independent quarter delays, including stopped/no-voice tails. */
    for(lane=0;lane<8;lane++) { drum_fx_config_t c={1,{0,50,100,100}}; drum_fx_restore(lane,&c);warm(lane); }
    { uint32_t peaks[8]={0};
      for(b=0;b<1800;b++)for(lane=0;lane<8;lane++) {
        memset(l,0,sizeof l);
        if(b==0)for(i=0;i<64u&&i<CTL;i++)l[i]=12000+(int32_t)lane*1000;
        drum_fx_lane_process(lane,l,CTL,120);
        if(b>600&&b<760)for(i=0;i<CTL;i++)if(l[i]>1000)peaks[lane]++;
      }
      for(lane=0;lane<8;lane++){assert(peaks[lane]);assert(dfx_state[lane].length<=4095);assert(dfx_state[lane].decim==6);}
    }
    /* Extremes stay bounded and every time division fits at min/max tempo. */
    for(t=0;t<8;t++)for(lane=0;lane<8;lane++) {
        drum_fx_config_t c={1,{(uint8_t)(t*100/7),100,100,100}};
        drum_fx_restore(lane,&c);
        for(b=0;b<128;b++) { for(i=0;i<CTL;i++)l[i]=i&1?131071:-131071;
            drum_fx_lane_process(lane,l,CTL,b&1?20:300);
            assert(dfx_state[lane].length>=1&&dfx_state[lane].length<=4095);
        }
    }
    drum_fx_reset();song.g[G_DRLVL]=100;drum_preset_select(0);
    for(lane=0;lane<6;lane++)drum_on(LANE_NOTE[lane],100);
    drum_fx_set(0,2);drum_fx_set(1,1);drum_fx_set(8,5);
    for(b=0;b<400;b++){memset(l,0,sizeof l);memset(r,0,sizeof r);memset(rv,0,sizeof rv);memset(dl,0,sizeof dl);drums_render(l,r,rv,dl,CTL);drum_fx_master_process(l,r,CTL,120);}
    puts("RC0 FX PASS: bypass,14 DSP types,56 parameter endpoints,independence,8 delay tails,tempo bounds,six voice routing");
    return 0;
}
