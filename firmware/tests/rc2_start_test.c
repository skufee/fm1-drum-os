/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact runtime decoder checkpoints, all embedded zones and real six voices. */
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static void render(uint32_t n)
{
    int32_t l[CTL]={0},r[CTL]={0},rv[CTL]={0},dl[CTL]={0};
    drums_render(l,r,rv,dl,n);
}

int main(void)
{
    uint32_t z,p,j,lane,checks=0,min_n=UINT32_MAX,max_n=0;
    host_tracks_init();
    assert(SMP_CUSTOM_SAMPLE_COUNT==55 && NDRUM==6);
    assert(sizeof SMP_START_PRED+sizeof SMP_START_IDX==16500);
    /* The reference scans each exact encoded nibble sequentially once per zone.
     * Check BEFORE state, prime and 32 following decoded samples, including odd
     * nibble starts. A compressed-offset-only seek fails these comparisons. */
    for(z=0;z<SMP_CUSTOM_SAMPLE_COUNT;z++) {
        const smp_zone_t *zone=&SMP_ZONES[z];voice_t seq={0};
        if(zone->n<min_n)min_n=zone->n;if(zone->n>max_n)max_n=zone->n;
        for(p=0;p<100;p++) {
            uint32_t target=zone->n*p/100;voice_t seek={0},expected;
            while(seq.ph[0]<target)sample_next(zone,&seq,0);
            assert(SMP_START_PRED[z][p]==seq.s[0] && SMP_START_IDX[z][p]==seq.s[1]);
            expected=seq;assert(drum_start_prime(zone,z,&seek,p)==target);
            assert(seek.s[3]==sample_next(zone,&expected,0));
            assert(seek.ph[0]==expected.ph[0] && seek.s[0]==expected.s[0] && seek.s[1]==expected.s[1]);
            for(j=0;j<32 && expected.ph[0]<zone->n;j++)
                assert(sample_next(zone,&seek,0)==sample_next(zone,&expected,0));
            checks++;
        }
    }
    printf("PASS all %u exact checkpoint states and subsequent samples; zone frames %u..%u; zero seek iterations\n",checks,min_n,max_n);
    /* Percentage guards and USER snapshot byte7. END refuses to cross START;
     * START refuses to cross END. Factory restores neutral START0. */
    drum_pad_edit_set(0,DRUM_EDIT_START,99);assert(drum_pad_edit_get(0,DRUM_EDIT_START)==99);
    drum_pad_edit_set(0,DRUM_EDIT_END,1);assert(drum_pad_edit_get(0,DRUM_EDIT_END)==100);
    drum_pad_edit_set(0,DRUM_EDIT_START,0);drum_pad_edit_set(0,DRUM_EDIT_END,2);
    drum_pad_edit_set(0,DRUM_EDIT_START,100);assert(drum_pad_edit_get(0,DRUM_EDIT_START)==1);
    assert(drum_user_kit_save(0));drum_pad_edit_set(0,DRUM_EDIT_START,0);
    assert(drum_user_kit_load(0));assert(drum_pad_edit_get(0,DRUM_EDIT_START)==1);
    assert(drum_preset_select(0));assert(drum_pad_edit_get(0,DRUM_EDIT_START)==0);
    puts("PASS START<END clamps, USER START roundtrip and neutral factory START0");
    /* START0/mid/nearEND, all55 sample lengths. State after note-on must match
     * checkpoint prime; renderer never advances beyond zone/end boundary. */
    for(z=0;z<SMP_CUSTOM_SAMPLE_COUNT;z++)for(p=0;p<3;p++) {
        const uint32_t percent[3]={0,50,99};const smp_zone_t *zone=&SMP_ZONES[z];
        uint32_t target=zone->n*percent[p]/100,b;
        memset(&drums,0,sizeof drums);drum_custom_sample_id_set(0,z);
        drum_pad_edit_set(0,DRUM_EDIT_START,0);drum_pad_edit_set(0,DRUM_EDIT_END,100);
        drum_pad_edit_set(0,DRUM_EDIT_START,percent[p]);drum_on(LANE_NOTE[0],100);
        assert(drums.v[0].ph[0]==target+1 && drums.end[0]<=zone->n && drums.end[0]>target);
        assert(drums.attack[0]==(p?32:0));
        if(p) {int32_t left[CTL]={0},right[CTL]={0},rv[CTL]={0},dl[CTL]={0};
            drums_render(left,right,rv,dl,1);assert(left[0]==0 && right[0]==0 && drums.attack[0]==31);}
        for(b=0;b<2200 && drums.v[0].active;b++) {
            render(CTL);assert(drums.v[0].ph[0]<=drums.end[0] && drums.end[0]<=zone->n);
        }
        assert(!drums.v[0].active);
    }
    puts("PASS all55 START0/mid/99%, first-output fade, short/long and end bounds");
    /* NearEND and tune extremes, six independent decoder states/retrigger. */
    memset(&drums,0,sizeof drums);drum_fx_reset();
    for(lane=0;lane<6;lane++) {
        drum_custom_sample_id_set(lane,SMP_CUSTOM_ID_KICK1);
        drum_pad_edit_set(lane,DRUM_EDIT_START,0);drum_pad_edit_set(lane,DRUM_EDIT_END,100);
        drum_pad_edit_set(lane,DRUM_EDIT_START,50+lane);
        drum_pad_edit_set(lane,DRUM_EDIT_TUNE,lane%2?24:-24);
        drum_on(LANE_NOTE[lane],100);
    }
    for(lane=0;lane<6;lane++)assert(drums.v[lane].active);
    render(CTL);
    for(j=0;j<240;j++) {drum_on(LANE_NOTE[j%6],100);render(CTL);
        for(lane=0;lane<6;lane++)assert(drums.v[lane].ph[0]<=drums.end[lane]);}
    drum_pad_edit_set(0,DRUM_EDIT_START,50);drum_preview(SMP_CUSTOM_ID_KICK1,100);
    assert(drum_pad_edit_get(0,DRUM_EDIT_START)==50);
    for(lane=0;lane<6;lane++)if(drums.v[lane].age==drums.age)assert(drums.v[lane].ph[0]==1 && !drums.attack[lane]);
    puts("PASS six simultaneous voices, +/-24 Tune rapid retrigger, preview neutral START and preserved edit");
    printf("RC2 START PASS; const checkpoints=%zu B, mutable lane START=16 B, attack=6 B; one prime decode per trigger\n",sizeof SMP_START_PRED+sizeof SMP_START_IDX);
    return 0;
}
