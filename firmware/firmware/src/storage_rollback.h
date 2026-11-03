/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit reverse path for archived raw bytes. Fixed object/bank geometry,
 * current full-backup proof, separate confirmation, sequential pages, CRC and
 * first-page/header LAST. No arbitrary address and no automatic invocation. */
#ifndef FM1_STORAGE_ROLLBACK_H
#define FM1_STORAGE_ROLLBACK_H
static int rb_prepare(const uint8_t *p,uint32_t n,uint8_t *out)
{
    uint32_t crc,kind,obj,copy;st_hdr_t header;int rc;
    if(n!=264||p[1]>1u||sr.active)return 1;
    kind=p[1];obj=p[2];copy=p[3];
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(!kind) {
        if(!save_object_ok(obj)||copy>1u||!save_backup.active||save_backup.authorized||
           save_backup.object!=obj||save_backup.next!=8192u)return 4;
        if(!save_pair_crc(obj,&crc)||crc!=save_backup.crc||st_copy_state(obj,copy^1u)!=ST_STATE_VALID)return 4;
        memcpy(&header,p+8,sizeof header);
        if(header.magic!=ST_MAGIC||header.type!=obj||header.slot!=copy||header.len>ST_PAYLOAD_MAX||
           header.hcrc!=st_crc32(&header,sizeof header-4u))return 5;
    } else {
        if(obj>=4u||copy!=255u||save_backup.active||dw_backup_complete!=(int32_t)obj||!dw_backup_unchanged(obj))return 4;
        rc=sr_bank_dependencies(obj);if(rc)return rc;
        if(!nor_diag_crc(SMP_USER_OFF(obj),SMP_USER_SIZE,&crc,0))return 6;
    }
    memset(&sr,0,sizeof sr);sr.kind=(uint8_t)(kind+2u);sr.object=obj;sr.target=(uint8_t)copy;
    sr.survivor=(uint8_t)(kind?255u:copy^1u);sr.base=kind?SMP_USER_OFF(obj):st_sector(obj,copy);
    sr.length=kind?SMP_USER_SIZE:ST_SECTOR;sr.crc=crc;sr.seed_crc=dw_u32(p+4);
    sr.token=kind?((++save_backup_nonce*0x9e3779b9u)^crc^sr.base^0x52415731u):save_backup.token;
    if(!sr.token)sr.token=1;
    if(!kind&&!save_raw_crc(obj,copy^1u,&sr.survivor_crc))return sr_fail(6,save_io_failure_rc);
    memcpy(&dw_upload_header,p+8,256);sr.seed_next=256;sr.active=1;sr_plan(out);return 0;
}
static int rb_start(uint32_t token,uint32_t snapshot,uint32_t source)
{
    uint32_t crc,off;int rc;
    if(!sr.active||sr.kind<2u||sr.kind>3u||sr.step||token!=sr.token||snapshot!=sr.crc||source!=sr.seed_crc)return 8;
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(sr.kind==2u) {
        if(!save_backup.active||save_backup.authorized||save_backup.object!=sr.object||save_backup.token!=token||
           save_backup.next!=8192u||!save_pair_crc(sr.object,&crc)||crc!=snapshot||
           st_copy_state(sr.object,sr.survivor)!=ST_STATE_VALID)return sr_fail(4,save_io_failure_rc?save_io_failure_rc:-1);
        project_recovery_invalidated(sr.object);
    } else {
        if(!dw_backup_unchanged(sr.object))return sr_fail(save_io_failure_rc?6:4,save_io_failure_rc?save_io_failure_rc:-1);
        rc=sr_bank_dependencies(sr.object);if(rc)return sr_fail((uint32_t)rc,-1);
        for(off=sr.object;off<DRUM_USER_SAMPLE_COUNT;off+=4u){drum_user_sample_invalidate(off);drum_usr_class[off]=3;}
    }
    for(off=0;off<sr.length;off+=ST_SECTOR) {
        save_write_guard.sector=sr.base+off;save_write_guard.read_rc=0;save_write_guard.active=1;
        rc=st_erase(sr.base+off);save_write_guard.active=0;
        if(rc){if(sr.kind==3u){ed_smp_inval(sr.object);dw_sample_rescan_bank(sr.object);}return sr_fail(6,save_io_failure_rc?save_io_failure_rc:rc);}
        sr.sectors++;fm1_wdt_feed();
    }
    if(sr.kind==3u)ed_smp_inval(sr.object);
    sr.sequence=save_crc_more(0xffffffffu,(const uint8_t *)&dw_upload_header,256);
    sr.last=sr.last_n=0;sr.step=4;return 0;
}
static int rb_page(uint32_t token,uint32_t off,const uint8_t *data,uint32_t count,uint8_t *out)
{
    int rc;
    if(!sr.active||sr.kind<2u||sr.step!=4||token!=sr.token||count!=256u||off<256u||off>=sr.length||
       (off&255u)||count>sr.length-off)return 1;
    if(off!=sr.seed_next) {
        if(off!=sr.last||count!=sr.last_n||off+count!=sr.seed_next)return 8;
        save_io_failure_stage=0;save_io_failure_rc=0;
        if(st_read(sr.base+off,out,count))return sr_fail(6,save_io_failure_rc?save_io_failure_rc:-107);
        if(memcmp(out,data,count))return sr_fail(5,-105);
        dw_w32(out,sr.seed_next);return 0;
    }
    save_io_failure_stage=0;save_io_failure_rc=0;
    save_write_guard.sector=(sr.base+off)&~(ST_SECTOR-1u);save_write_guard.read_rc=0;save_write_guard.active=1;
    rc=st_prog(sr.base+off,data,count);save_write_guard.active=0;
    if(sr.kind==3u)ed_smp_inval(sr.object);
    if(rc||st_read(sr.base+off,out,count)||memcmp(out,data,count))return sr_fail(6,save_io_failure_rc?save_io_failure_rc:rc?rc:-105);
    sr.sequence=save_crc_more(sr.sequence,data,count);sr.last=off;sr.last_n=count;sr.seed_next+=count;
    dw_w32(out,sr.seed_next);return 0;
}
static int rb_finish(uint32_t token,uint32_t source,uint8_t *out)
{
    uint8_t b[64];uint32_t pos,crc,other;int rc;
    if(!sr.active||sr.kind<2u||sr.step!=4||token!=sr.token||source!=sr.seed_crc||sr.seed_next!=sr.length)return 8;
    if(~sr.sequence!=source)return sr_fail(5,-105);
    crc=save_crc_more(0xffffffffu,(const uint8_t *)&dw_upload_header,256);
    for(pos=256;pos<sr.length;pos+=sizeof b){if(st_read(sr.base+pos,b,sizeof b))return sr_fail(6,save_io_failure_rc?save_io_failure_rc:-107);crc=save_crc_more(crc,b,sizeof b);if(!(pos&4095u))fm1_wdt_feed();}
    if(~crc!=source)return sr_fail(5,-105);
    save_io_failure_stage=0;save_io_failure_rc=0;
    save_write_guard.sector=sr.base;save_write_guard.read_rc=0;save_write_guard.active=1;
    rc=st_prog(sr.base,&dw_upload_header,256);save_write_guard.active=0;
    if(sr.kind==3u)ed_smp_inval(sr.object);
    if(rc||!nor_diag_crc(sr.base,sr.length,&crc,0)||crc!=source)return sr_fail(6,save_io_failure_rc?save_io_failure_rc:rc?rc:-105);
    if(sr.kind==2u) {
        if(st_copy_state(sr.object,sr.target)!=ST_STATE_VALID||!save_raw_crc(sr.object,sr.survivor,&other)||other!=sr.survivor_crc)return sr_fail(6,save_io_failure_rc?save_io_failure_rc:-106);
        project_recovery_invalidated(sr.object);save_backup_cancel();
    } else dw_sample_rescan_bank(sr.object);
    sr.active=0;sr.step=1;dw_backup_complete=-1;
    memset(out,0,20);out[0]=1;out[1]=sr.kind;out[2]=(uint8_t)sr.object;out[3]=sr.target;out[4]=1;dw_w32(out+8,crc);return 0;
}
#endif
