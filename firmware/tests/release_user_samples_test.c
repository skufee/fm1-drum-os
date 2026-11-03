/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
static uint8_t test_user_flash[4][0x14000];
#define SMP_USER_XIP(k) (test_user_flash[(k)])
#define FELUCCA_DRUM_ONLY 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define PROJECT_TEST_NO_PROCESS 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define DRUMREC_HAVE_CORE 1
#include "../firmware/src/drum_record.c"
#undef DRUMREC_HAVE_CORE
static void make_slot(uint32_t k,uint32_t frames,uint8_t fill)
{
    smp_user_hdr_t *h=(smp_user_hdr_t *)test_user_flash[k];uint32_t n=(frames+1u)/2u;
    memset(test_user_flash[k],255,sizeof test_user_flash[k]);memset(h,0,sizeof *h);
    h->magic=SMP_USER_MAGIC;h->version=1;h->nz=1;memcpy(h->name,"USRDRUM",7);
    h->data_len=n;memset(test_user_flash[k]+512,fill,n);h->crc=drum_sample_crc(test_user_flash[k]+512,n);
    h->zone[0].n=frames;h->zone[0].le=frames-1u;h->zone[0].rate=32768;h->zone[0].hi=127;
}
static void clear_voices(void){memset(&drums,0,sizeof drums);}
static void render(void){int32_t l[CTL]={0},r[CTL]={0},rv[CTL]={0},dl[CTL]={0};drums_render(l,r,rv,dl,CTL);}
#ifndef USER_SAMPLE_TEST_ENTRY
#define USER_SAMPLE_TEST_ENTRY main
#endif
int USER_SAMPLE_TEST_ENTRY(void)
{
    uint32_t k,p,i,fp,oldage;drumrec3_t a,b;uint8_t wire[DRUMREC6_SIZE],old[DRUMREC5_SIZE];
    static const uint32_t lengths[4]={128,1500,8192,40000};
    host_tracks_init();assert(drum_custom_sample_count()==55&&NDRUM==6&&DRUMREC6_SIZE==1959u);
    for(k=0;k<4;k++){
        make_slot(k,lengths[k],(uint8_t)(0x11u+k*0x11u));assert(drum_user_sample_scan(k));
        fp=drum_sample_fingerprint(55+k);assert(fp&&drum_sample_available(55+k,fp));
        assert(drum_user_waveform(55+k));for(i=0;i<96;i++)assert(drum_usr_wave[k][i*2]<=drum_usr_wave[k][i*2+1]);
        for(p=0;p<100;p++){
            const smp_zone_t *z=&usr_zone[k][0];voice_t ref={0},seek={0};uint32_t pos=z->n*p/100u;
            while(ref.ph[0]<pos)sample_next(z,&ref,0);
            assert(drum_usr_pred[k][p]==ref.s[0]&&drum_usr_idx[k][p]==ref.s[1]);
            assert(drum_start_prime(z,55+k,&seek,p)==pos);assert(seek.s[3]==sample_next(z,&ref,0));
            assert(seek.ph[0]<=z->n);
        }
        assert(drum_custom_sample_id_set(k,55+k));drum_pad_edit_set(k,DRUM_EDIT_START,50);
    }
    puts("PASS four existing slots,400 exact START predictor/index states, four source lengths, bounded prime");
    assert(drum_custom_sample_id_set(4,55));assert(drum_custom_sample_id_set(5,56));
    clear_voices();for(i=0;i<6;i++)drum_on(LANE_NOTE[i],100);
    for(i=0;i<6;i++)assert(drums.v[i].active);
    for(p=0;p<240;p++){drum_on(LANE_NOTE[p%6],100);render();for(i=0;i<6;i++)assert(drums.v[i].ph[0]<=drums.end[i]);}
    assert(drum_user_kit_save(0));assert(drum_sample_slot_referenced(0));
    fp=drum_sample_refs[0];drum_preview(58,100);assert(drum_sample_refs[0]==fp&&drum_custom_sample_ids[0]==55);
    assert(drum_preset_select(0));assert(drum_user_kit_load(0));assert(drum_sample_refs[0]==fp);
    puts("PASS six user voices,240 retriggers, USER references, preview neutral state preserved");
    clear_voices();oldage=drums.age;test_user_flash[0][512]^=0x80;assert(!drum_user_sample_scan(0));
    drum_on(LANE_NOTE[0],100);assert(drums.age==oldage&&!drums.v[0].active);
    assert(drum_custom_sample_ids[0]==55&&drum_sample_refs[0]==fp);
    test_user_flash[0][512]^=0x80;assert(drum_user_sample_scan(0));assert(drum_sample_available(55,fp));
    make_slot(0,128,0x77);assert(drum_user_sample_scan(0));assert(!drum_sample_available(55,fp));
    drum_on(LANE_NOTE[0],100);assert(drums.age==oldage);
    assert(drum_sample_reference_set(0,55,fp));assert(drum_custom_sample_ids[0]==55);
    puts("PASS CRC-invalid/missing/replaced fingerprint falls back to silence, assignment retained");
    memset(&a,0,sizeof a);a.base.base.bpm=120;a.base.base.pattern_length=16;a.base.base.project_fields[5]=1;
    a.map_version=1;a.source_id=255;
    for(i=0;i<8;i++){a.base.base.pads[i].level=127;a.base.base.pads[i].end=100;a.sample_ids[i]=(uint8_t)i;}
    for(i=0;i<4;i++){a.sample_ids[i]=55+i;a.sample_refs[i]=drum_sample_fingerprint(55+i);a.starts[i]=35;}
    a.user_valid[0]=1;
    for(i=0;i<8;i++){
        a.user_kits[0][i].sample_id=a.sample_ids[i];a.user_kits[0][i].level=127;a.user_kits[0][i].end=100;
        a.user_sample_refs[0][i]=a.sample_refs[i];
    }
    assert(drumrec6_encode(wire,sizeof wire,&a)==sizeof wire);assert(drumrec6_decode(&b,wire,sizeof wire));assert(!memcmp(&a,&b,sizeof a));
    assert(!drumrec5_encode(old,sizeof old,&a));wire[DRUMREC6_SIZE-1]^=1;assert(!drumrec6_decode(&b,wire,sizeof wire));wire[DRUMREC6_SIZE-1]^=1;
    a.sample_refs[0]=0;assert(!drumrec6_encode(wire,sizeof wire,&a));
    for(i=0;i<4;i++){a.sample_ids[i]=(uint8_t)i;a.sample_refs[i]=0;a.user_kits[0][i].sample_id=(uint8_t)i;a.user_sample_refs[0][i]=0;}
    assert(drumrec5_encode(old,sizeof old,&a)==sizeof old);assert(drumrec5_decode(&b,old,sizeof old));
    for(i=0;i<8;i++){assert(!b.sample_refs[i]);for(k=0;k<4;k++)assert(!b.user_sample_refs[k][i]);}
    puts("PASS DRM6 exact current/USER fingerprints; DRM5 keeps embedded-only contract, CRC/range guards, oldrefs zero");
    memset(test_user_flash[3],255,sizeof test_user_flash[3]);test_user_flash[3][512]=0x11;
    assert(!drum_user_sample_scan(3)); /* data without committed header */
    make_slot(3,512,0x11);((smp_user_hdr_t *)test_user_flash[3])->nz=2;
    ((smp_user_hdr_t *)test_user_flash[3])->zone[1]=((smp_user_hdr_t *)test_user_flash[3])->zone[0];
    assert(!drum_user_sample_scan(3));assert(usr_nz[3]==2); /* legacy survives, not drum-relabelled */
    puts("PASS interrupted/headerless and multizone legacy slot never exposed as one-zone drum sample");
    printf("LEGACY USER SAMPLES PASS; checkpoints=%zuB refs=%zuB waveform=%zuB; no PCM cache\n",sizeof drum_usr_pred+sizeof drum_usr_idx,sizeof drum_sample_refs+sizeof drum_user_sample_refs,sizeof drum_usr_wave);
    return 0;
}
