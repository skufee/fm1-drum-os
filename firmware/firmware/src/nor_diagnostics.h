/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop read-only diagnostics. Existing map/primitives remain authoritative.
 * No rescanning/publishing voices, backup lock, recovery token, erase or program.
 * Schema1: editor SysEx27 sub3 journal report, sub4 journal raw, sub5 USER report. */
#ifndef FM1_NOR_DIAGNOSTICS_H
#define FM1_NOR_DIAGNOSTICS_H
static int nor_diag_crc(uint32_t off,uint32_t n,uint32_t *result,uint32_t *first)
{
    uint8_t b[64];uint32_t pos,j,c=0xffffffffu;
    if(first)*first=0xffffffffu;
    for(pos=0;pos<n;pos+=sizeof b) {
        uint32_t k=n-pos<sizeof b?n-pos:sizeof b;
        if(st_read(off+pos,b,k))return 0;
        c=save_crc_more(c,b,k);
        if(first&&*first==0xffffffffu)for(j=0;j<k;j++)if(b[j]!=255u){*first=pos+j;break;}
        if(!(pos&4095u))fm1_wdt_feed();
    }
    *result=~c;return 1;
}
static int nor_diag_project(uint32_t obj,uint8_t *b)
{
    uint32_t copy,c,first;uint8_t pairbuf[64];uint32_t pair=0xffffffffu,pos;
    if(!save_object_ok(obj))return 1;
    memset(b,0,152);b[0]=1;b[2]=(uint8_t)obj;b[3]=2;
    dw_w32(b+4,8192);dw_w32(b+8,st_sector(obj,0));dw_w32(b+12,st_sector(obj,1));
    b[20]=255;b[21]=1;
    for(copy=0;copy<2u;copy++) {
        uint8_t *r=b+24+64u*copy;st_hdr_t h;int n,v;
        r[0]=st_copy_state(obj,copy);dw_w32(r+4,st_sector(obj,copy));
        if(!nor_diag_crc(st_sector(obj,copy),ST_SECTOR,&c,&first)||
           st_read(st_sector(obj,copy),&h,sizeof h)||
           st_read(st_sector(obj,copy)+ST_PAYLOAD_OFF,r+56,8))return 6;
        dw_w32(r+20,c);memcpy(r+24,&h,sizeof h);
        if(r[0]==ST_STATE_UNKNOWN){r[3]=2;b[21]=0;continue;}
        if(first==0xffffffffu){r[0]=ST_STATE_ERASED;r[1]=254;r[3]=1;continue;}
        if(st_head(obj,copy,&h)){r[3]=3;continue;}
        dw_w32(r+8,h.len);dw_w32(r+12,h.seq);dw_w32(r+16,h.crc);
        r[2]=(uint8_t)save_owned_prefix(obj,copy,&h);
        if(r[0]!=ST_STATE_VALID){r[3]=4;continue;}
        n=st_load_copy(obj,copy,st_buf,sizeof st_buf);
        if(n<0)return 6;
        v=save_wire_format(st_buf,(uint32_t)n);
        if(v)r[1]=(uint8_t)v;
        else if((v=project_legacy_wire_format(st_buf,(uint32_t)n))!=0){r[1]=(uint8_t)(16+v);r[3]=6;}
        else r[3]=n>=4&&st_buf[0]=='D'&&st_buf[1]=='R'&&st_buf[2]=='M'?7:5;
    }
    /* Same byte order as raw export: full A4096 then full B4096. */
    for(copy=0;copy<2;copy++)for(pos=0;pos<ST_SECTOR;pos+=sizeof pairbuf) {
        if(st_read(st_sector(obj,copy)+pos,pairbuf,sizeof pairbuf))return 6;
        pair=save_crc_more(pair,pairbuf,sizeof pairbuf);
    }
    dw_w32(b+16,~pair);
    {st_hdr_t h;int current=st_current(obj,&h);if(current>=0)b[20]=(uint8_t)current;}
    b[22]=(uint8_t)save_repairable(obj,0);
    return 0;
}
static int nor_diag_project_raw(uint32_t obj,uint32_t off,uint32_t n,uint8_t *b)
{
    uint32_t copy=off/ST_SECTOR,within=off%ST_SECTOR;
    if(!save_object_ok(obj)||!n||n>256u||off>=8192u||n>8192u-off||n>ST_SECTOR-within)return 1;
    if(st_read(st_sector(obj,copy)+within,b+8,n))return 6;
    dw_w16(b,off);dw_w16(b+2,n);dw_w32(b+4,8192);return 0;
}
static int nor_diag_sample(uint32_t slot,uint8_t *b)
{
    uint32_t bank,off,c,first,bankfirst,extent=0,i,k;int access=1;
    const uint8_t *raw,*parent;const smp_user_hdr_t *h,*ph;
    if(slot>=DRUM_USER_SAMPLE_COUNT)return 1;
    bank=slot%4u;off=drum_user_slot_off(slot);raw=drum_user_slot_xip(slot);
    parent=smp_user_xip(bank);h=(const smp_user_hdr_t *)raw;ph=(const smp_user_hdr_t *)parent;
    memset(b,0,160);b[0]=1;b[1]=1;b[2]=(uint8_t)slot;b[3]=(uint8_t)bank;
    dw_w32(b+4,off);dw_w32(b+8,DRUM_USER_SLICE);dw_w32(b+12,drum_user_backup_size(slot));
    dw_w32(b+16,SMP_USER_OFF(bank));dw_w32(b+20,SMP_USER_SIZE);
    if(!nor_diag_crc(off,DRUM_USER_SLICE,&c,&first))return 6;dw_w32(b+24,c);dw_w32(b+156,first);
    if(!nor_diag_crc(SMP_USER_OFF(bank),SMP_USER_SIZE,&c,&bankfirst))return 6;dw_w32(b+28,c);
    if(st_read(off,b+56,64)||st_read(SMP_USER_OFF(bank),b+120,32))return 6;
    /* Never call drum_user_slot_access here: it can rescan/withdraw a voice. */
    if(drum_usr_ready[bank])extent=SMP_USER_DATA+ph->data_len;
    if(slot>=4u) {
        if(extent)access=extent<=(slot/4u)*DRUM_USER_SLICE;
        else {for(i=0;i<DRUM_USER_SLICE&&parent[i]==255u;i++){}access=i==DRUM_USER_SLICE;}
    }
    dw_w32(b+44,extent);b[48]=drum_usr_class[slot];
    b[51]=(uint8_t)((access?1:0)|(drum_usr_ready[slot]?2:0)|(drum_usr_class[slot]==3?4:0));
    dw_w32(b+152,drum_usr_fingerprint[slot]);
    if(first==0xffffffffu){b[49]=1;b[50]=1;}
    else if(!memcmp(raw,"SLG2",4)){b[49]=5;b[50]=4;} /* Signature only, no asserted schema/ownership. */
    else if(h->magic==SMP_USER_MAGIC) {
        b[49]=4;b[50]=5;dw_w32(b+32,h->data_len);dw_w32(b+36,h->crc);
        if(h->version==1u) {
            b[50]=7;
            if(h->data_len&&h->data_len<=(slot<4u?SMP_USER_SIZE:DRUM_USER_SLICE)-SMP_USER_DATA) {
                if(!nor_diag_crc(off+SMP_USER_DATA,h->data_len,&c,0))return 6;
                dw_w32(b+40,c);b[51]|=8;
                if(c!=h->crc)b[50]=8;
                else if(!h->nz||h->nz>16u)b[50]=7;
                else {
                    for(i=0;i<h->nz&&smp_zone_ok(&h->zone[i],h->data_len);i++){}
                    if(i==h->nz) {
                        if(h->nz!=1u){b[49]=3;b[50]=6;}
                        else if(h->zone[0].looped){b[50]=12;}
                        else if(h->zone[0].off||h->zone[0].le+1u!=h->zone[0].n)b[50]=7;
                        else if(slot>=4u&&(h->rsv2[0]!=DRUM_USER_CHILD_MAGIC||h->rsv2[1]!=slot))b[50]=9;
                        else {b[49]=2;b[50]=0;}
                    }
                }
            }
        }
    } else {
        const uint8_t *m=raw+480;
        b[50]=4;
        for(i=0;i<32&&raw[i]==255u;i++){}
        if(i==32u&&!memcmp(m,"FUP1",4)&&m[4]==1&&m[5]==slot&&dw_u32(m+28)==st_crc32(m,28)){b[49]=6;b[50]=10;}
    }
    if(!access)b[50]=(uint8_t)(extent?3:2);
    /* References are separate from content protection. -1 means absence
     * cannot be proven (e.g. valid legacy/unknown AUTOSAVE), not a free slot. */
    for(i=0;i<8;i++) {
        if(drum_custom_sample_ids[i]==55u+slot)b[52]|=(uint8_t)(1u<<i);
        for(k=0;k<4;k++)if(drum_user_valid[k]&&drum_user_pads[k][i][0]==55u+slot)b[53]|=(uint8_t)(1u<<k);
    }
    b[54]=(uint8_t)(int8_t)project_sample_slot_referenced(slot);
    return 0;
}
static int nor_diag_dispatch(const uint8_t *p,uint32_t n,uint8_t *b,uint32_t *out)
{
    save_diag_t saved=save_diag;uint8_t stage=save_io_failure_stage;int32_t raw=save_io_failure_rc;int rc;
    if(!flash_ok)return 6;
    if(p[0]==3u){if(n!=2)return 1;rc=nor_diag_project(p[1],b);*out=152;}
    else if(p[0]==4u){if(n!=6)return 1;rc=nor_diag_project_raw(p[1],dw_u16(p+2),dw_u16(p+4),b);*out=8+dw_u16(p+4);}
    else {if(n!=2)return 1;rc=nor_diag_sample(p[1],b);*out=160;}
    save_diag=saved;save_io_failure_stage=stage;save_io_failure_rc=raw;
    if(rc)*out=0;return rc;
}
#endif
