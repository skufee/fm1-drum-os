/* SPDX-License-Identifier: GPL-3.0-only */
/* Application policy around unchanged storage.c. No HAL/layout/CRC bypass. */
#ifndef FM1_PERSISTENCE_DIAG_H
#define FM1_PERSISTENCE_DIAG_H
#include <stdint.h>
enum { SAVE_ERR_NONE, SAVE_ERR_FLASH_NOT_READY, SAVE_ERR_ENCODE_FAILED,
       SAVE_ERR_DESTINATION_PROTECTED, SAVE_ERR_UNKNOWN_RECORD, SAVE_ERR_CRC_INVALID,
       SAVE_ERR_ERASE_FAILED, SAVE_ERR_PROGRAM_FAILED, SAVE_ERR_READBACK_FAILED,
       SAVE_ERR_NO_SAFE_RECOVERY_PATH, SAVE_ERR_READ_FAILED };
enum { SAVE_STAGE_NONE, SAVE_STAGE_READ, SAVE_STAGE_ERASE, SAVE_STAGE_PROGRAM,
       SAVE_STAGE_VERIFY, SAVE_STAGE_CLASSIFY, SAVE_STAGE_RECOVERY };
typedef struct { uint8_t code,stage,object,copy; int32_t raw_rc; uint32_t count; } save_diag_t;
static save_diag_t save_diag;
/* Main-loop only write fence consumed by the application NOR adapters. */
static struct {uint32_t sector;int32_t read_rc;uint8_t active;} save_write_guard;
static uint8_t save_destination_valid_mask;
static int save_write_allowed(uint32_t off,uint32_t n)
{
    if(!save_write_guard.active)return 1;
    if(save_io_failure_stage==SAVE_STAGE_READ&&save_io_failure_rc)save_write_guard.read_rc=save_io_failure_rc;
    return off>=save_write_guard.sector&&off<=save_write_guard.sector+ST_SECTOR&&n&&n<=save_write_guard.sector+ST_SECTOR-off;
}
typedef struct { uint8_t state,format,owned,reserved; uint32_t sequence,length,payload_crc,raw_crc; } save_copy_report_t;
static struct { uint32_t object,crc,token,next,last,last_n; uint8_t active,authorized; } save_backup;
static uint32_t save_backup_nonce;
/* Defined by the application: decode exact complete DRM wire, returns version. */
static int save_wire_format(const void *wire,uint32_t n);
static void save_error(uint32_t code,int32_t raw,uint32_t obj,uint32_t copy,uint32_t stage)
{
    save_diag.code=(uint8_t)code;save_diag.raw_rc=raw;save_diag.object=(uint8_t)obj;
    save_diag.copy=(uint8_t)copy;save_diag.stage=(uint8_t)stage;save_diag.count++;
}
static const char *save_error_name(uint32_t code)
{
    static const char *const names[]={"NONE","FLASH_NOT_READY","ENCODE_FAILED","DESTINATION_PROTECTED",
      "UNKNOWN_RECORD","CRC_INVALID","ERASE_FAILED","PROGRAM_FAILED","READBACK_FAILED",
      "NO_SAFE_RECOVERY_PATH","READ_FAILED"};
    return code<sizeof names/sizeof names[0]?names[code]:"UNKNOWN_ERROR";
}
static uint32_t save_crc_more(uint32_t crc,const uint8_t *p,uint32_t n)
{
    uint32_t i;
    while(n--){crc^=*p++;for(i=0;i<8;i++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1u)));}
    return crc;
}
static int save_object_ok(uint32_t obj)
{ return (obj>=OBJ_PROJECT0&&obj<OBJ_PROJECT0+4u)||obj==OBJ_AUTOSAVE; }
static int save_raw_crc(uint32_t obj,uint32_t copy,uint32_t *result)
{
    uint8_t b[64];uint32_t o,crc=0xffffffffu;
    for(o=0;o<ST_SECTOR;o+=sizeof b) {
        int rc=st_read(st_sector(obj,copy)+o,b,sizeof b);
        if(rc){save_error(SAVE_ERR_READ_FAILED,rc,obj,copy,SAVE_STAGE_READ);return 0;}
        crc=save_crc_more(crc,b,sizeof b);
    }
    *result=~crc;return 1;
}
static uint32_t save_version_size(uint32_t version)
{
    switch(version){case 1:return DRUMREC_SIZE;case 2:return DRUMREC2_SIZE;case 3:return DRUMREC3_SIZE;
      case 4:return DRUMREC4_SIZE;case 5:return DRUMREC5_SIZE;
#ifdef DRUMREC6_SIZE
      case 6:return DRUMREC6_SIZE;
#endif
      default:return 0;}
}
static int save_owned_prefix(uint32_t obj,uint32_t copy,const st_hdr_t *h)
{
    uint8_t b[16];uint32_t v;
    if(st_read(st_sector(obj,copy)+ST_PAYLOAD_OFF,b,sizeof b))return 0;
    v=b[3]>='1'&&b[3]<='6'?(uint32_t)(b[3]-'0'):0u;
    return b[0]=='D'&&b[1]=='R'&&b[2]=='M'&&v&&b[4]==v&&!b[5]&&b[6]==16u&&!b[7]&&
      h->len==save_version_size(v)&&((uint32_t)b[8]|(uint32_t)b[9]<<8|(uint32_t)b[10]<<16|(uint32_t)b[11]<<24)==h->len-16u;
}
static int save_pair_report(uint32_t obj,save_copy_report_t out[2])
{
    uint32_t i;
    if(!out||!save_object_ok(obj))return 0;
    memset(out,0,2u*sizeof *out);
    for(i=0;i<2;i++) {
        st_hdr_t h;int n;
        out[i].state=st_copy_state(obj,i);
        if(!save_raw_crc(obj,i,&out[i].raw_crc)){out[i].state=ST_STATE_UNKNOWN;return 0;}
        if(st_head(obj,i,&h))continue;
        out[i].sequence=h.seq;out[i].length=h.len;out[i].payload_crc=h.crc;
        out[i].owned=(uint8_t)save_owned_prefix(obj,i,&h);
        if(out[i].state==ST_STATE_VALID) {
            n=st_load_copy(obj,i,st_buf,sizeof st_buf);
            if(n>=0)out[i].format=(uint8_t)save_wire_format(st_buf,(uint32_t)n);
        }
    }
    return 1;
}
static int save_pair_crc(uint32_t obj,uint32_t *result)
{
    uint8_t b[64];uint32_t i,o,crc=0xffffffffu;
    for(i=0;i<2;i++)for(o=0;o<ST_SECTOR;o+=sizeof b) {
        int rc=st_read(st_sector(obj,i)+o,b,sizeof b);
        if(rc){save_error(SAVE_ERR_READ_FAILED,rc,obj,i,SAVE_STAGE_READ);return 0;}
        crc=save_crc_more(crc,b,sizeof b);
    }
    *result=~crc;return 1;
}
/* A repair is only defined when one authentic known DRM survivor exists and
 * the other copy has an authentic committed DRM header but bad payload CRC.
 * Torn/unknown headers, FUN/foreign payloads, bothbad and readfail stay closed. */
static int save_repairable(uint32_t obj,uint32_t *badcopy)
{
    save_copy_report_t r[2];uint32_t a;
    if(!save_pair_report(obj,r))return 0;
    for(a=0;a<2;a++)if(r[a].state==ST_STATE_VALID&&r[a].format&&r[a^1u].state==ST_STATE_CORRUPT&&r[a^1u].owned) {
        if(badcopy)*badcopy=a^1u;return 1;
    }
    return 0;
}
static int save_backup_begin(uint32_t obj,uint32_t *crc,uint32_t *token)
{
    uint32_t c;
    if(!save_object_ok(obj)||!crc||!token||!save_pair_crc(obj,&c))return 0;
    memset(&save_backup,0,sizeof save_backup);
    save_backup.active=1;save_backup.object=obj;save_backup.crc=c;
    save_backup.token=(++save_backup_nonce*0x9E3779B9u)^c^0x464D3141u;
    if(!save_backup.token)save_backup.token=1;
    *crc=c;*token=save_backup.token;return 1;
}
/* Idempotent retry of the immediately preceding chunk is permitted. */
static int save_backup_read(uint32_t offset,uint32_t n,void *dst)
{
    uint32_t copy=offset/ST_SECTOR,within=offset%ST_SECTOR;int rc;
    if(!save_backup.active||!dst||!n||n>256u||offset>8192u||n>8192u-offset||n>ST_SECTOR-within||
      (offset!=save_backup.next&&!(offset==save_backup.last&&n==save_backup.last_n&&offset+n==save_backup.next)))return 0;
    rc=st_read(st_sector(save_backup.object,copy)+within,dst,n);
    if(rc){save_error(SAVE_ERR_READ_FAILED,rc,save_backup.object,copy,SAVE_STAGE_READ);return 0;}
    if(offset==save_backup.next){save_backup.last=offset;save_backup.last_n=n;save_backup.next+=n;}
    return 1;
}
/* Host must durably download the complete raw8KiB backup and explicitly ask
 * the owner to confirm. This grants ONE write to the damaged copy, never A. */
static int save_recovery_authorize(uint32_t token,uint32_t crc)
{
    uint32_t current,bad;
    if(!save_backup.active||save_backup.next!=8192u||token!=save_backup.token||crc!=save_backup.crc||
      !save_pair_crc(save_backup.object,&current)||current!=crc||!save_repairable(save_backup.object,&bad)) {
        save_error(SAVE_ERR_NO_SAFE_RECOVERY_PATH,-1,save_backup.object,255,SAVE_STAGE_RECOVERY);return 0;
    }
    save_backup.authorized=1;return 1;
}
static int save_recovery_permits(uint32_t obj)
{
    uint32_t crc,bad;
    return save_backup.active&&save_backup.authorized&&save_backup.object==obj&&
      save_pair_crc(obj,&crc)&&crc==save_backup.crc&&save_repairable(obj,&bad);
}
static int save_destination_format(uint32_t obj,int allow_drm1,int raw_nonzero)
{
    save_copy_report_t r[2];uint8_t format[2];uint32_t i;int repair;
    save_destination_valid_mask=0;
    if(!save_object_ok(obj)){save_error(SAVE_ERR_DESTINATION_PROTECTED,-1,obj,255,SAVE_STAGE_CLASSIFY);return 0;}
    save_io_failure_stage=0;save_io_failure_rc=0;
    if(!save_pair_report(obj,r)){save_error(SAVE_ERR_READ_FAILED,save_io_failure_rc,obj,255,SAVE_STAGE_READ);return 0;}
    repair=save_recovery_permits(obj);
    for(i=0;i<2;i++) {
        if(r[i].state==ST_STATE_ERASED){format[i]=PROJECT_COPY_ERASED;continue;}
        if(r[i].state==ST_STATE_VALID&&r[i].format){format[i]=r[i].format;save_destination_valid_mask|=(uint8_t)(1u<<i);continue;}
        if(r[i].state==ST_STATE_CORRUPT&&repair&&r[i].owned){format[i]=PROJECT_COPY_ERASED;continue;}
        save_error(r[i].state==ST_STATE_UNKNOWN?SAVE_ERR_READ_FAILED:
          r[i].state==ST_STATE_CORRUPT&&r[i].owned?SAVE_ERR_CRC_INVALID:
          r[i].state==ST_STATE_VALID?SAVE_ERR_DESTINATION_PROTECTED:SAVE_ERR_UNKNOWN_RECORD,
          save_io_failure_rc,obj,i,SAVE_STAGE_CLASSIFY);return 0;
    }
    if(!project_drm3_write_format(format[0],format[1],allow_drm1||repair,raw_nonzero)) {
        save_error(SAVE_ERR_DESTINATION_PROTECTED,-1,obj,255,SAVE_STAGE_CLASSIFY);return 0;
    }
    return PROJECT_RECORD_VERSION;
}

static void save_backup_cancel(void) { memset(&save_backup,0,sizeof save_backup); }
static int save_commit(uint32_t obj,const void *wire,uint32_t n)
{
    int rc,repair,survivor,chosen;uint32_t expected;st_hdr_t current;
    if(!wire||!save_wire_format(wire,n)){save_error(SAVE_ERR_ENCODE_FAILED,-1,obj,255,SAVE_STAGE_CLASSIFY);return -101;}
    if(save_backup.active&&save_backup.object==obj&&!save_backup.authorized) {
        save_error(SAVE_ERR_NO_SAFE_RECOVERY_PATH,-1,obj,255,SAVE_STAGE_RECOVERY);return -102;
    }
    if(!save_destination_format(obj,1,0))return -102;
    repair=save_recovery_permits(obj);
    save_io_failure_stage=0;save_io_failure_rc=0;
    survivor=st_current(obj,&current);
    if(save_io_failure_stage==SAVE_STAGE_READ) {
        rc=save_io_failure_rc;save_error(SAVE_ERR_READ_FAILED,rc,obj,255,SAVE_STAGE_READ);
        if(repair)save_backup_cancel();return rc;
    }
    if((save_destination_valid_mask&&survivor<0)||
       (survivor>=0&&!(save_destination_valid_mask&(1u<<(uint32_t)survivor)))) {
        save_error(SAVE_ERR_NO_SAFE_RECOVERY_PATH,-104,obj,255,SAVE_STAGE_CLASSIFY);
        if(repair)save_backup_cancel();return -104;
    }
    /* The unchanged journal orders sequences numerically. Wrapping to0 would
     * verify a write that cold LOAD does not choose over UINT32_MAX. */
    if(survivor>=0&&current.seq==0xffffffffu) {
        save_error(SAVE_ERR_NO_SAFE_RECOVERY_PATH,-103,obj,(uint32_t)survivor,SAVE_STAGE_CLASSIFY);
        if(repair)save_backup_cancel();return -103;
    }
    expected=survivor==0?1u:0u;
    save_write_guard.sector=st_sector(obj,expected);save_write_guard.read_rc=0;save_write_guard.active=1;
    save_io_failure_stage=0;save_io_failure_rc=0;
    rc=st_save(obj,wire,n);
    if(rc) {
        int32_t original=save_write_guard.read_rc?save_write_guard.read_rc:save_io_failure_rc?save_io_failure_rc:rc;
        save_error(save_write_guard.read_rc||save_io_failure_stage==SAVE_STAGE_READ?SAVE_ERR_READ_FAILED:
          rc==-104?SAVE_ERR_NO_SAFE_RECOVERY_PATH:
          save_io_failure_stage==SAVE_STAGE_ERASE?SAVE_ERR_ERASE_FAILED:
          save_io_failure_stage==SAVE_STAGE_PROGRAM?SAVE_ERR_PROGRAM_FAILED:SAVE_ERR_READBACK_FAILED,
          original,obj,expected,save_write_guard.read_rc?SAVE_STAGE_READ:
          save_io_failure_stage?save_io_failure_stage:SAVE_STAGE_VERIFY);
    } else {
        /* Read-back of a new copy is not enough: cold selection must actually
         * choose that exact copy and snapshot, even after a transient read. */
        save_io_failure_stage=0;save_io_failure_rc=0;
        chosen=st_current(obj,&current);
        if(save_io_failure_stage==SAVE_STAGE_READ) {
            rc=save_io_failure_rc;save_error(SAVE_ERR_READ_FAILED,rc,obj,expected,SAVE_STAGE_READ);
        } else if(chosen!=(int)expected||current.len!=n||memcmp(st_buf,wire,n)) {
            rc=-105;save_error(SAVE_ERR_READBACK_FAILED,save_write_guard.read_rc?save_write_guard.read_rc:rc,obj,expected,SAVE_STAGE_VERIFY);
        } else if(save_write_guard.read_rc) {
            /* A nonfatal earlier read still remains in the diagnostic history;
             * current data is nevertheless CRC-verified and cold-selected. */
            save_error(SAVE_ERR_READ_FAILED,save_write_guard.read_rc,obj,expected,SAVE_STAGE_READ);
        }
    }
    save_write_guard.active=0;
    if(repair)save_backup_cancel();
    return rc;
}

#endif
