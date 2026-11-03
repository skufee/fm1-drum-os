/* SPDX-License-Identifier: GPL-3.0-only */
/* Sixteen bounded logical slots inside the existing four FSMP regions.
 * Original IDs55..58 keep subslot0; legacy large sources block overlaps.
 * No NOR layout/HAL change. Unknown and multizone parents stay protected. */
#ifndef FM1_DRUM_USER_SAMPLES_H
#define FM1_DRUM_USER_SAMPLES_H
#define DRUM_USER_SAMPLE_FIRST 55u
#define DRUM_USER_SAMPLE_COUNT 16u
#define DRUM_SAMPLE_COUNT 71u
#define DRUM_USER_SLICE 20480u
#define DRUM_USER_CAPACITY (DRUM_USER_SLICE-SMP_USER_DATA)
#define DRUM_USER_CHILD_MAGIC 0x31535244u /* DRS1, not a legacy FSMP zone */
static int16_t drum_usr_pred[16][100];
static uint8_t drum_usr_idx[16][100];
static volatile uint8_t drum_usr_ready[16];
static uint8_t drum_usr_class[16]; /* cached0empty/1valid/2intent/3protected */
static uint32_t drum_usr_fingerprint[16];
static char drum_usr_name[16][9];
static int8_t drum_usr_wave[16][192];
static uint32_t drum_sample_refs[8], drum_user_sample_refs[4][8];
static uint32_t drum_user_slot_off(uint32_t slot)
{ return SMP_USER_OFF(slot%4u)+(slot/4u)*DRUM_USER_SLICE; }
static const uint8_t *drum_user_slot_xip(uint32_t slot)
{ return smp_user_xip(slot%4u)+(slot/4u)*DRUM_USER_SLICE; }
/* Primary raw backups remain80KiB for legacy compatibility, children20KiB. */
static uint32_t drum_user_backup_size(uint32_t slot)
{ return slot<4u?SMP_USER_SIZE:DRUM_USER_SLICE; }
static smp_zone_t *drum_user_zone(uint32_t slot)
{ return &usr_zone[slot%4u][slot/4u]; }
static uint32_t drum_sample_crc(const void *data,uint32_t n)
{
    const uint8_t *p=(const uint8_t *)data;uint32_t crc=0xffffffffu,i;
    while(n--){crc^=*p++;for(i=0;i<8;i++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return ~crc;
}
static void drum_user_sample_invalidate(uint32_t slot)
{
    if(slot<DRUM_USER_SAMPLE_COUNT){fm1_irq_off();drum_usr_ready[slot]=0;drum_usr_fingerprint[slot]=0;fm1_irq_on();}
}
static int drum_user_sample_scan(uint32_t slot);
static int drum_user_scan_failed(uint32_t slot)
{
    uint32_t i;
    if(slot<4u&&drum_usr_class[slot]!=0u)for(i=slot+4u;i<DRUM_USER_SAMPLE_COUNT;i+=4u){drum_user_sample_invalidate(i);drum_usr_class[i]=3;}
    return 0;
}
/* Boot/stopped-write classification only. LIST reads this cache and never
 * scans a source or withdraws readiness while PLAY/REC is active. */
static void drum_user_classify_unavailable(uint32_t slot)
{
    const uint8_t *raw=drum_user_slot_xip(slot);uint32_t i;const uint8_t *m=raw+480;
    for(i=0;i<DRUM_USER_SLICE;i++)if(raw[i]!=255u)break;
    if(i==DRUM_USER_SLICE){drum_usr_class[slot]=0;return;}
    for(i=0;i<32;i++)if(raw[i]!=255u)return;
    if(!memcmp(m,"FUP1",4)&&m[4]==1&&m[5]==slot&&
       ((uint32_t)m[28]|(uint32_t)m[29]<<8|(uint32_t)m[30]<<16|(uint32_t)m[31]<<24)==drum_sample_crc(m,28))drum_usr_class[slot]=2;
}
/* A child is usable only outside a verified parent's declared data, or when
 * the entire primary20KiB is erased. Never reinterpret ADPCM bytes as headers. */
static int drum_user_slot_access(uint32_t slot)
{
    uint32_t bank=slot%4u,i;const uint8_t *raw=smp_user_xip(bank);
    const smp_user_hdr_t *parent=(const smp_user_hdr_t *)raw;
    if(slot>=DRUM_USER_SAMPLE_COUNT)return 0;
    if(slot<4u)return 1;
    if(drum_usr_ready[bank])
        return SMP_USER_DATA+parent->data_len<=(slot/4u)*DRUM_USER_SLICE;
    for(i=0;i<DRUM_USER_SLICE;i++)if(raw[i]!=255u)break;
    if(i==DRUM_USER_SLICE)return 1;
    if(drum_user_sample_scan(bank))return SMP_USER_DATA+parent->data_len<=(slot/4u)*DRUM_USER_SLICE;
    return 0;
}
static void drum_user_scan_sample(uint32_t slot,const smp_zone_t *z,voice_t *decoder)
{
    uint32_t col;int32_t v;sample_next(z,decoder,0);
    col=(decoder->ph[0]-1u)*96u/z->n;v=decoder->s[0]>>8;
    if(v<drum_usr_wave[slot][col*2u])drum_usr_wave[slot][col*2u]=(int8_t)v;
    if(v>drum_usr_wave[slot][col*2u+1u])drum_usr_wave[slot][col*2u+1u]=(int8_t)v;
#if FELUCCA_FLASH
    if(!(decoder->ph[0]&1023u))fm1_wdt_feed();
#endif
}
/* Main loop only, while stopped: bounded source scan, never a note-on seek. */
static int drum_user_sample_scan(uint32_t slot)
{
    const smp_user_hdr_t *h;const smp_zone_t *z;smp_zone_t resolved;voice_t decoder={0};uint32_t p,i,fp;
    if(slot>=DRUM_USER_SAMPLE_COUNT)return 0;
    drum_user_sample_invalidate(slot);
    drum_usr_class[slot]=3;
    if(!drum_user_slot_access(slot))return 0;
    if(slot<4u)smp_user_scan(slot); /* retain the legacy primary FSMP reader */
    h=(const smp_user_hdr_t *)drum_user_slot_xip(slot);
    if(h->magic!=SMP_USER_MAGIC||h->version!=1u||h->nz!=1u||
       (slot>=4u&&(h->rsv2[0]!=DRUM_USER_CHILD_MAGIC||h->rsv2[1]!=slot))||
       !h->data_len||h->data_len>(slot<4u?SMP_USER_SIZE:DRUM_USER_SLICE)-SMP_USER_DATA||
       !smp_zone_ok(&h->zone[0],h->data_len)||h->zone[0].off||h->zone[0].looped||
       h->zone[0].le+1u!=h->zone[0].n){drum_user_classify_unavailable(slot);return drum_user_scan_failed(slot);}
    if(drum_sample_crc(drum_user_slot_xip(slot)+SMP_USER_DATA,h->data_len)!=h->crc)return drum_user_scan_failed(slot);
    if(slot<4u)for(i=slot+4u;i<DRUM_USER_SAMPLE_COUNT;i+=4u)
        if(SMP_USER_DATA+h->data_len>(i/4u)*DRUM_USER_SLICE){drum_user_sample_invalidate(i);drum_usr_class[i]=3;}
    resolved=h->zone[0];
    resolved.off+=(uint32_t)(uintptr_t)(drum_user_slot_xip(slot)+SMP_USER_DATA)-(uint32_t)(uintptr_t)SMP_DATA;
    fm1_irq_off();*drum_user_zone(slot)=resolved;
    if(slot<4u)usr_nz[slot]=1;
    fm1_irq_on();
    z=drum_user_zone(slot);
    for(i=0;i<96;i++){drum_usr_wave[slot][i*2u]=127;drum_usr_wave[slot][i*2u+1u]=-128;}
    for(p=0;p<100;p++){
        uint32_t target=z->n*p/100u;
        while(decoder.ph[0]<target)drum_user_scan_sample(slot,z,&decoder);
        drum_usr_pred[slot][p]=(int16_t)decoder.s[0];drum_usr_idx[slot][p]=(uint8_t)decoder.s[1];
    }
    while(decoder.ph[0]<z->n)drum_user_scan_sample(slot,z,&decoder);
    for(i=0;i<96;i++)if(drum_usr_wave[slot][i*2u]>drum_usr_wave[slot][i*2u+1u]){
        drum_usr_wave[slot][i*2u]=i?drum_usr_wave[slot][i*2u-2u]:0;
        drum_usr_wave[slot][i*2u+1u]=i?drum_usr_wave[slot][i*2u-1u]:0;
    }
    for(i=0;i<8;i++)drum_usr_name[slot][i]=h->name[i]>=32&&h->name[i]<127?h->name[i]:0;
    drum_usr_name[slot][8]=0;
    fp=drum_sample_crc(h,sizeof *h);fm1_irq_off();
    drum_usr_fingerprint[slot]=fp?fp:1u;drum_usr_ready[slot]=1;drum_usr_class[slot]=1;fm1_irq_on();return 1;
}
static const int8_t *drum_user_waveform(uint32_t id)
{ return id>=55u&&id<DRUM_SAMPLE_COUNT&&drum_usr_ready[id-55u]?drum_usr_wave[id-55u]:0; }
static uint32_t drum_sample_fingerprint(uint32_t id)
{ return id>=55u&&id<DRUM_SAMPLE_COUNT&&drum_usr_ready[id-55u]?drum_usr_fingerprint[id-55u]:0u; }
static int drum_sample_available(uint32_t id,uint32_t fingerprint)
{ return id<55u?!fingerprint:id<DRUM_SAMPLE_COUNT&&fingerprint&&drum_sample_fingerprint(id)==fingerprint; }
static const char *drum_user_sample_name(uint32_t id)
{
    static const char *const fallback[16]={"USER1","USER2","USER3","USER4","USER5","USER6","USER7","USER8","USER9","USER10","USER11","USER12","USER13","USER14","USER15","USER16"};
    return id>=55u&&id<DRUM_SAMPLE_COUNT?(drum_usr_ready[id-55u]&&drum_usr_name[id-55u][0]?drum_usr_name[id-55u]:fallback[id-55u]):0;
}
void drum_sample_refs_export(uint32_t current[8],uint32_t users[4][8])
{
    uint32_t k,lane;fm1_irq_off();
    for(lane=0;lane<8;lane++)current[lane]=drum_sample_refs[lane];
    for(k=0;k<4;k++)for(lane=0;lane<8;lane++)users[k][lane]=drum_user_valid[k]?drum_user_sample_refs[k][lane]:0u;
    fm1_irq_on();
}
void drum_sample_refs_import(const uint32_t current[8],const uint32_t users[4][8])
{
    fm1_irq_off();memcpy(drum_sample_refs,current,sizeof drum_sample_refs);
    memcpy(drum_user_sample_refs,users,sizeof drum_user_sample_refs);fm1_irq_on();
}
#endif
