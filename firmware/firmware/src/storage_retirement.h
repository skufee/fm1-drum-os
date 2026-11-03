/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit, backup-bound retirement of ONE archived copy/bank. Never called
 * from boot/tick/connect. Unknown bytes are not decoded or migrated. The normal
 * destination guard, unchanged storage.c and NOR layout remain intact. */
#ifndef FM1_STORAGE_RETIREMENT_H
#define FM1_STORAGE_RETIREMENT_H
static struct {
    uint32_t object,base,length,crc,token,seed_crc,survivor_crc,target_crc,sequence;
    uint32_t seed_next,last,last_n;
    int32_t raw;
    uint8_t active,kind,target,survivor,format,step,error,sectors;
} sr;
static void sr_invalidate(uint32_t state)
{
    sr.active=0;sr.step=(uint8_t)state;
    if(sr.kind==0||sr.kind==2)save_backup_cancel();
    dw_backup_complete=-1;
}
static int sr_fail(uint32_t code,int32_t raw)
{sr.error=(uint8_t)code;sr.raw=raw;sr_invalidate(2);return (int)code;}
static void sr_status(uint8_t *b)
{
    memset(b,0,32);b[0]=1;b[1]=sr.kind;b[2]=(uint8_t)sr.object;b[3]=sr.target;
    b[4]=sr.active;b[5]=sr.step;b[6]=sr.error;b[7]=sr.sectors;
    dw_w32(b+8,(uint32_t)sr.raw);dw_w32(b+12,sr.base);dw_w32(b+16,sr.length);
    dw_w32(b+20,sr.crc);dw_w32(b+24,sr.seed_crc);
    dw_w32(b+28,sr.seed_next|(autosave_recovery_hold?0x80000000u:0u));
}
static void sr_plan(uint8_t *b)
{
    memset(b,0,48);b[0]=1;b[1]=sr.kind;b[2]=(uint8_t)sr.object;b[3]=sr.target;
    dw_w32(b+4,sr.base);dw_w32(b+8,sr.length);dw_w32(b+12,sr.crc);dw_w32(b+16,sr.token);
    dw_w32(b+20,sr.seed_crc);dw_w16(b+24,sr.kind?0:DRUMREC6_SIZE);
    b[26]=sr.survivor;b[27]=sr.format;dw_w32(b+28,sr.sequence);
    dw_w32(b+32,sr.survivor_crc);dw_w32(b+36,sr.target_crc);
    dw_w32(b+40,sr.format>=1&&sr.format<=6?1u:2u);
}
/* Inspect a valid survivor without applying it. Unknown outer-valid records
 * may survive an explicitly approved retirement; no semantic claim is made. */
static int sr_format(uint32_t obj,uint32_t copy)
{
    int n,v;n=st_load_copy(obj,copy,st_buf,sizeof st_buf);if(n<0)return -1;
    v=save_wire_format(st_buf,(uint32_t)n);if(v)return v;
    v=project_legacy_wire_format(st_buf,(uint32_t)n);return v?v+16:0;
}
static int sr_prepare_journal(uint32_t obj)
{
    save_copy_report_t r[2];uint32_t crc,survivor;int fmt[2];st_hdr_t h;
    if(sr.active||!save_object_ok(obj)||!save_backup.active||save_backup.authorized||
       save_backup.object!=obj||save_backup.next!=8192u)return 8;
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(!save_pair_crc(obj,&crc)||crc!=save_backup.crc||!save_pair_report(obj,r))return save_io_failure_rc?6:4;
    if(r[0].state==ST_STATE_UNKNOWN||r[1].state==ST_STATE_UNKNOWN)return 6;
    if(r[0].state!=ST_STATE_VALID&&r[1].state!=ST_STATE_VALID)return 4;
    fmt[0]=r[0].state==ST_STATE_VALID?sr_format(obj,0):0;
    fmt[1]=r[1].state==ST_STATE_VALID?sr_format(obj,1):0;
    if(fmt[0]<0||fmt[1]<0||save_io_failure_stage==SAVE_STAGE_READ)return 6;
    /* A healthy modern pair needs no retirement. Never gratuitously erase it. */
    if(((r[0].state==ST_STATE_VALID&&r[0].format)||r[0].state==ST_STATE_ERASED)&&
       ((r[1].state==ST_STATE_VALID&&r[1].format)||r[1].state==ST_STATE_ERASED))return 8;
    if(r[0].format)survivor=0;
    else if(r[1].format)survivor=1;
    else if(fmt[0]>=17u&&fmt[0]<=22u)survivor=0;
    else if(fmt[1]>=17u&&fmt[1]<=22u)survivor=1;
    else {int current=st_current(obj,&h);if(current<0)return 4;survivor=(uint32_t)current;}
    if(st_head(obj,survivor,&h)||h.seq==0xffffffffu)return 4;
    memset(&sr,0,sizeof sr);sr.kind=0;sr.object=obj;sr.survivor=(uint8_t)survivor;
    sr.target=(uint8_t)(survivor^1u);sr.base=st_sector(obj,sr.target);sr.length=ST_SECTOR;
    sr.crc=crc;sr.token=save_backup.token;sr.format=(uint8_t)fmt[survivor];sr.sequence=h.seq;
    sr.survivor_crc=r[survivor].raw_crc;sr.target_crc=r[survivor^1u].raw_crc;
    drumrec3_capture_live(&drumrec3_buf);
    if(drumrec6_encode(dw_transfer,sizeof dw_transfer,&drumrec3_buf)!=DRUMREC6_SIZE)return sr_fail(8,-1);
    sr.seed_crc=st_crc32(dw_transfer,DRUMREC6_SIZE);sr.active=1;return 0;
}
static int sr_read_seed(uint32_t off,uint32_t count,uint8_t *b)
{
    if(!sr.active||sr.kind||!count||count>256u||off>DRUMREC6_SIZE||count>DRUMREC6_SIZE-off||
       (off!=sr.seed_next&&!(off==sr.last&&count==sr.last_n&&off+count==sr.seed_next)))return 8;
    dw_w16(b,off);dw_w16(b+2,DRUMREC6_SIZE);dw_w32(b+4,sr.seed_crc);memcpy(b+8,dw_transfer+off,count);
    if(off==sr.seed_next){sr.last=off;sr.last_n=count;sr.seed_next+=count;}return 0;
}
static int sr_commit_journal(uint32_t token,uint32_t crc,uint32_t seed,uint32_t target,uint8_t *b)
{
    uint32_t current,other_crc,live_crc;int rc,chosen,ready;st_hdr_t h;save_copy_report_t reports[2];
    if(!sr.active||sr.kind||sr.target!=target||token!=sr.token||crc!=sr.crc||seed!=sr.seed_crc||
       sr.seed_next!=DRUMREC6_SIZE||!save_backup.active||save_backup.authorized||
       save_backup.object!=sr.object||save_backup.token!=token||save_backup.next!=8192u)return 8;
    if(!save_pair_crc(sr.object,&current)||current!=crc||
       !save_raw_crc(sr.object,sr.survivor,&other_crc)||other_crc!=sr.survivor_crc)return sr_fail(4,-1);
    if(st_head(sr.object,sr.survivor,&h)||st_copy_state(sr.object,sr.survivor)!=ST_STATE_VALID||h.seq!=sr.sequence)return sr_fail(4,-1);
    drumrec3_capture_live(&drumrec3_buf);
    if(drumrec6_encode(st_buf,sizeof st_buf,&drumrec3_buf)!=DRUMREC6_SIZE)return sr_fail(8,-1);
    live_crc=st_crc32(st_buf,DRUMREC6_SIZE);
    if(live_crc!=seed||st_crc32(dw_transfer,DRUMREC6_SIZE)!=seed||!save_wire_format(dw_transfer,DRUMREC6_SIZE))return sr_fail(5,-1);
    /* One approved sector only. First erase removes the retired commit so the
     * unchanged journal necessarily writes that same copy, preserving the other.
     * st_save performs a second bounded erase of this already-erased sector. */
    save_write_guard.sector=sr.base;save_write_guard.read_rc=0;save_write_guard.active=1;
    save_io_failure_stage=0;save_io_failure_rc=0;
    rc=st_erase(sr.base);
    if(!rc){sr.sectors=1;rc=st_save(sr.object,dw_transfer,DRUMREC6_SIZE);}
    if(!rc) {
        chosen=st_current(sr.object,&h);
        if(chosen!=(int)sr.target||h.seq!=sr.sequence+1u||h.len!=DRUMREC6_SIZE||memcmp(st_buf,dw_transfer,DRUMREC6_SIZE))rc=-105;
        if(!rc&&(!save_raw_crc(sr.object,sr.survivor,&other_crc)||other_crc!=sr.survivor_crc))rc=-106;
        if(!rc&&(!save_pair_report(sr.object,reports)||!save_pair_crc(sr.object,&current)||
           reports[0].state==ST_STATE_UNKNOWN||reports[1].state==ST_STATE_UNKNOWN))rc=-107;
        if(!rc&&save_io_failure_stage==SAVE_STAGE_READ)rc=save_io_failure_rc?save_io_failure_rc:-107;
    }
    save_write_guard.active=0;
    if(rc) {
        int32_t raw=save_write_guard.read_rc?save_write_guard.read_rc:save_io_failure_rc?save_io_failure_rc:rc;
        save_error(save_io_failure_stage==SAVE_STAGE_READ?SAVE_ERR_READ_FAILED:
          save_io_failure_stage==SAVE_STAGE_ERASE?SAVE_ERR_ERASE_FAILED:
          save_io_failure_stage==SAVE_STAGE_PROGRAM?SAVE_ERR_PROGRAM_FAILED:SAVE_ERR_READBACK_FAILED,
          raw,sr.object,sr.target,save_io_failure_stage?save_io_failure_stage:SAVE_STAGE_VERIFY);
        return sr_fail(6,raw);
    }
    /* Only a verified commit can update the durable snapshot cache. */
    project_recovery_committed(sr.object,dw_transfer,DRUMREC6_SIZE);
    save_error(SAVE_ERR_NONE,0,sr.object,sr.target,SAVE_STAGE_VERIFY);
    save_backup_cancel();sr.active=0;sr.step=1;
    ready=((reports[0].state==ST_STATE_ERASED)||(reports[0].state==ST_STATE_VALID&&reports[0].format))&&
          ((reports[1].state==ST_STATE_ERASED)||(reports[1].state==ST_STATE_VALID&&reports[1].format));
    memset(b,0,20);b[0]=1;b[2]=(uint8_t)sr.object;b[3]=sr.target;b[4]=1;b[5]=(uint8_t)ready;
    dw_w32(b+8,h.seq);dw_w32(b+12,seed);dw_w32(b+16,current);return 0;
}
static int sr_bank_dependencies(uint32_t bank)
{
    uint32_t slot;int rc;
    for(slot=bank;slot<DRUM_USER_SAMPLE_COUNT;slot+=4u){rc=dw_sample_deps(slot);if(rc)return rc<0?4:9;}
    return 0;
}
static int sr_prepare_bank(uint32_t bank)
{
    uint32_t crc,first;int rc;
    if(sr.active)return 8;
    if(bank>=4u)return 1;
    if(save_backup.active||dw_backup_complete!=(int32_t)bank)return 4;
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(!dw_backup_unchanged(bank))return save_io_failure_rc?6:4;
    rc=sr_bank_dependencies(bank);if(rc)return save_io_failure_rc?6:rc;
    if(!nor_diag_crc(SMP_USER_OFF(bank),SMP_USER_SIZE,&crc,&first))return 6;
    if(first==0xffffffffu)return 8; /* already erased: no retirement needed */
    memset(&sr,0,sizeof sr);sr.kind=1;sr.object=bank;sr.target=sr.survivor=255;sr.base=SMP_USER_OFF(bank);
    sr.length=SMP_USER_SIZE;sr.crc=crc;sr.token=(++save_backup_nonce*0x9e3779b9u)^crc^sr.base^0x52544952u;
    if(!sr.token)sr.token=1;sr.active=1;return 0;
}
static int sr_commit_bank(uint32_t token,uint32_t crc,uint32_t bank,uint8_t *b)
{
    uint32_t off,actual,first,took;int rc;
    if(!sr.active||sr.kind!=1u||bank!=sr.object||token!=sr.token||crc!=sr.crc)return 8;
    if(bank>=4u||sr.base!=SMP_USER_OFF(bank)||sr.length!=SMP_USER_SIZE)return sr_fail(1,-1);
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(!dw_backup_unchanged(bank))return sr_fail(save_io_failure_rc?6:4,save_io_failure_rc?save_io_failure_rc:-1);
    rc=sr_bank_dependencies(bank);if(rc)return sr_fail(save_io_failure_rc?6:(uint32_t)rc,save_io_failure_rc?save_io_failure_rc:-1);
    for(off=bank;off<DRUM_USER_SAMPLE_COUNT;off+=4u){drum_user_sample_invalidate(off);drum_usr_class[off]=3;}
    /* No journal, settings, adjacent bank or calibration sector is in this loop. */
    for(off=0;off<SMP_USER_SIZE;off+=ST_SECTOR) {
        rc=fl_erase4k_quiet(sr.base+off,&took);
        if(rc){ed_smp_inval(bank);dw_sample_rescan_bank(bank);return sr_fail(6,rc);}
        sr.sectors++;fm1_wdt_feed();
    }
    ed_smp_inval(bank);
    if(!nor_diag_crc(sr.base,sr.length,&actual,&first)){dw_sample_rescan_bank(bank);return sr_fail(6,save_io_failure_rc?save_io_failure_rc:-107);}
    if(first!=0xffffffffu){dw_sample_rescan_bank(bank);return sr_fail(5,-105);}
    dw_sample_rescan_bank(bank);dw_backup_complete=-1;sr.active=0;sr.step=1;
    memset(b,0,20);b[0]=1;b[1]=1;b[2]=(uint8_t)bank;b[3]=255;b[4]=1;b[5]=1;dw_w32(b+8,actual);return 0;
}
#include "storage_rollback.h"
static int sr_dispatch(const uint8_t *p,uint32_t n,uint8_t *b,uint32_t *out)
{
    int rc;if(!n||p[0]>10)return 1;
    if(p[0]==5){if(n!=1)return 1;sr_status(b);*out=32;return 0;}
    if(p[0]==6){if(n!=1)return 1;sr_invalidate(3);return 0;}
    if(dw_write_busy()||dw_upload_slot>=0||dw_restore_slot>=0||dw_work_backup)return 3;
    if(!flash_ok)return 6;
    switch(p[0]) {
    case 0:if(n!=2)return 1;rc=sr_prepare_journal(p[1]);if(!rc){sr_plan(b);*out=48;}return rc;
    case 1:if(n!=5)return 1;rc=sr_read_seed(dw_u16(p+1),dw_u16(p+3),b);if(!rc)*out=8+dw_u16(p+3);return rc;
    case 2:if(n!=15||p[14]!=DW_CONFIRM)return 1;rc=sr_commit_journal(dw_u32(p+1),dw_u32(p+5),dw_u32(p+9),p[13],b);if(!rc)*out=20;return rc;
    case 3:if(n!=2)return 1;rc=sr_prepare_bank(p[1]);if(!rc){sr_plan(b);*out=48;}return rc;
    case 4:if(n!=11||p[10]!=DW_CONFIRM)return 1;rc=sr_commit_bank(dw_u32(p+1),dw_u32(p+5),p[9],b);if(!rc)*out=20;return rc;
    case 7:rc=rb_prepare(p,n,b);if(!rc)*out=48;return rc;
    case 8:if(n!=14||p[13]!=DW_CONFIRM)return 1;return rb_start(dw_u32(p+1),dw_u32(p+5),dw_u32(p+9));
    case 9:if(n!=265)return 1;rc=rb_page(dw_u32(p+1),dw_u32(p+5),p+9,256,b);if(!rc)*out=4;return rc;
    case 10:if(n!=10||p[9]!=DW_CONFIRM)return 1;rc=rb_finish(dw_u32(p+1),dw_u32(p+5),b);if(!rc)*out=20;return rc;
    default:return 1;
    }
}
#endif
