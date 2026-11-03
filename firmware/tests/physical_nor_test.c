/* SPDX-License-Identifier: GPL-3.0-only */
/* Real application dispatcher/scanner/codec/storage; explicit peripheral and
 * persisted-reference service doubles from release_protocol_test.c. No hardware. */
#define PROTOCOL_TEST_ENTRY previous_protocol_suite
#include "release_protocol_test.c"
static uint8_t rawpair[8192],report[160],wire[DRUMREC6_SIZE];
static void fresh(void)
{
    memset(nor,255,sizeof nor);memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);
    memset(drum_usr_class,0,sizeof drum_usr_class);memset(drum_user_valid,0,sizeof drum_user_valid);
    save_backup_cancel();memset(&save_diag,0,sizeof save_diag);quiet();
    persistent_dependency=fail_read=fail_write=0;dw_upload_slot=dw_restore_slot=-1;
    dw_ready=1;host_tracks_init();drum_preset_select(0);
}
static void unchanged(void)
{assert(!memcmp(nor_before,nor,sizeof nor));}
static void capture(void)
{drumrec3_capture_live(&drumrec3_buf);assert(drumrec6_encode(wire,sizeof wire,&drumrec3_buf)==sizeof wire);}
static void project_report(void)
{assert(call(27,(uint8_t[]){3,7},2)==0&&reply_n==152);memcpy(report,reply,152);}
static void sample_report(uint32_t slot)
{assert(call(27,(uint8_t[]){5,(uint8_t)slot},2)==0&&reply_n==160);memcpy(report,reply,160);}
static void seed_fsmp(uint32_t slot,uint32_t length)
{
    smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+drum_user_slot_off(slot));
    memset(h,0,sizeof *h);h->magic=SMP_USER_MAGIC;h->version=1;h->nz=1;
    if(slot>=4){h->rsv2[0]=DRUM_USER_CHILD_MAGIC;h->rsv2[1]=slot;}
    h->data_len=length;memset((uint8_t *)h+512,0x11,length);h->crc=st_crc32((uint8_t *)h+512,length);
    h->zone[0].n=length*2;h->zone[0].le=length*2-1;h->zone[0].rate=32768;h->zone[0].hi=127;
}
static void fixture(const char *name)
{
    const char *dir=getenv("FM1_TEST_OUTPUT");char path[2048];FILE *f;if(!dir)return;
    assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);f=fopen(path,"wb");assert(f);
    assert(fwrite(nor,1,sizeof nor,f)==sizeof nor);fclose(f);
}
static void load_physical(const char *name,void *dst,uint32_t n)
{
    const char *dir=getenv("FM1_PHYSICAL_BACKUP");char path[2048];FILE *f;assert(dir);
    assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);f=fopen(path,"rb");assert(f);
    assert(fread(dst,1,n,f)==n&&fgetc(f)==EOF);fclose(f);
}
static void save_report(const char *name,uint32_t n)
{
    const char *dir=getenv("FM1_TEST_OUTPUT");char path[2048];FILE *f;if(!dir)return;
    assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);f=fopen(path,"wb");assert(f);
    assert(fwrite(report,1,n,f)==n);fclose(f);
}
int main(void)
{
    uint32_t i,j,e,p,len;uint8_t request[6];save_diag_t before_diag;project_t fun;
    setvbuf(stdout,0,_IONBF,0);fresh();assert(call(1,0,0)==0&&(reply[14]&1));
    memcpy(nor_before,nor,sizeof nor);project_report();assert(report[24]==ST_STATE_ERASED&&report[25]==254&&report[88]==ST_STATE_ERASED);unchanged();
    assert(!save_backup.active);pass("read-only blank A/B report leaves NOR and recovery state untouched");
    /* Exact observed code path, with a fully identified synthetic historical
     * record. This is NOT claimed to be the missing owner's AUTOSAVE payload. */
    proj_capture(&fun);assert(project_legacy_wire_format(&fun,sizeof fun)==6);
    assert(st_save(OBJ_AUTOSAVE,&fun,sizeof fun)==0);memcpy(nor_before,nor,sizeof nor);
    assert(!save_destination_format(7,0,0));assert(save_diag.code==3&&save_diag.object==7&&save_diag.copy==0&&save_diag.stage==5&&save_diag.raw_rc==0);
    before_diag=save_diag;e=erases;p=programs;project_report();assert(report[24]==ST_STATE_VALID&&report[25]==22&&report[27]==6);
    assert(!memcmp(&before_diag,&save_diag,sizeof save_diag)&&!save_backup.active);unchanged();
    fixture("known-fun6.nor");pass("reproduce owner code3/A/classify/raw0 with valid FELU+FUN6; identify without erasing");
    for(i=0;i<8192;i+=256){request[0]=4;request[1]=7;dw_w16(request+2,i);dw_w16(request+4,256);assert(call(27,request,6)==0&&reply_n==264);memcpy(rawpair+i,reply+8,256);}
    assert(!memcmp(rawpair,nor+0x9f000,4096)&&!memcmp(rawpair+4096,nor+0xfe000,4096));
    assert(st_crc32(rawpair,8192)==dw_u32(report+16));assert(st_crc32(rawpair,4096)==dw_u32(report+44));
    assert(erases==e&&programs==p&&!save_backup.active);unchanged();pass("complete independent8KiB A/B raw CRC including nonadjacent sectors; no backup token/lock");
    assert(call(28,(uint8_t[]){0,0,0,0,0,0,0,0,0x5a},9)==4);unchanged();pass("read-only export cannot grant recovery or legacy migration permission");
    fresh();memset(st_buf,0x42,64);assert(st_save(7,st_buf,64)==0);project_report();assert(report[25]==0&&report[27]==5);
    assert(!save_destination_format(7,1,0)&&save_diag.code==3);pass("CRC-valid foreign payload remains unknown and protected, never FUN by guess");
    fresh();capture();assert(save_commit(7,wire,sizeof wire)==0);project_report();assert(report[25]==6&&report[27]==0);
    nor[0x9f000+256+100]^=1;memcpy(nor_before,nor,sizeof nor);project_report();assert(report[24]==ST_STATE_CORRUPT&&report[27]==4);unchanged();pass("known DRM6 and CRC-invalid payload are distinguished without repair");
    nor[0xfe000]=0;project_report();assert(report[88]==ST_STATE_CORRUPT&&report[91]==3&&!report[22]);pass("torn header and both-corrupt copies do not offer recovery");
    fresh();memcpy(nor+SMP_USER_OFF(0),"SLG2",4);memcpy(nor+SMP_USER_OFF(3),"UNKNOWN",7);
    for(i=0;i<16;i++)drum_user_sample_scan(i);memcpy(nor_before,nor,sizeof nor);persistent_dependency=-1;
    for(i=0;i<16;i++){sample_report(i);assert(((report[51]&4)!=0)==(i%4==0||i%4==3));assert((int8_t)report[54]==-1);if(i>=4&&(i%4==0||i%4==3))assert(report[50]==2);}
    sample_report(0);assert(report[49]==5&&report[50]==4);sample_report(3);assert(report[49]==0&&report[50]==4);
    unchanged();fixture("owner-pattern-synthetic.nor");pass("exact reported USER1/4/5/8/9/12/13/16 flags4 pattern; SLG2 signature-only, dependencies unknown separately");
    for(i=0;i<16;i++){
        sample_report(i);assert(dw_u32(report+4)==drum_user_slot_off(i)&&dw_u32(report+8)==20480&&dw_u32(report+16)==SMP_USER_OFF(i%4));
        for(j=0;j<4;j++){uint32_t a=st_sector(7,j&1);assert(drum_user_slot_off(i)+20480<=a||drum_user_slot_off(i)>=a+4096);}
    }pass("all16 offsets and4physical banks match real map, disjoint from AUTOSAVE/settings/OTA boundaries");
    fresh();seed_fsmp(0,24000);assert(drum_user_sample_scan(0));sample_report(4);assert(report[50]==3&&dw_u32(report+44)==24512);
    sample_report(8);assert(report[50]==1);pass("valid large FSMP parent reports declared extent and exact overlap, no blanket false ownership");
    fresh();seed_fsmp(0,256);{smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+SMP_USER_OFF(0));h->nz=2;h->zone[1]=h->zone[0];}sample_report(0);assert(report[49]==3&&report[50]==6);pass("valid multizone FSMP is known unsupported, not erased/unknown/corrupt");
    fresh();seed_fsmp(0,256);{smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+SMP_USER_OFF(0));h->zone[0].looped=1;}sample_report(0);assert(report[50]==12);pass("looped FSMP reports unsupported loop semantics");
    fresh();seed_fsmp(15,256);assert(drum_user_sample_scan(15));sample_report(15);assert(report[49]==2&&!report[50]&&(report[51]&2));
    {smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+drum_user_slot_off(15));h->rsv2[1]=14;}sample_report(15);assert(report[50]==9);
    nor[drum_user_slot_off(15)+512]^=1;sample_report(15);assert(report[50]==8);pass("valid child/tag mismatch/payload CRC are separate, real scanner readiness not mutated by diagnostics");
    fresh();capture();assert(save_commit(7,wire,sizeof wire)==0);memcpy(nor_before,nor,sizeof nor);before_diag=save_diag;e=erases;p=programs;
    fail_read=1;assert(call(27,(uint8_t[]){3,7},2)==6);assert(call(27,(uint8_t[]){5,0},2)==6);fail_read=0;
    assert(erases==e&&programs==p&&!memcmp(&before_diag,&save_diag,sizeof save_diag));unchanged();pass("read fault NACK, preserves last save diagnostic and every NOR byte");
    request[0]=4;request[1]=7;dw_w16(request+2,4090);dw_w16(request+4,20);assert(call(27,request,6)==1);
    dw_w16(request+2,8192);assert(call(27,request,6)==1);request[1]=0;dw_w16(request+2,0);assert(call(27,request,6)==1);
    assert(call(27,(uint8_t[]){5,16},2)==1);assert(call(27,(uint8_t[]){3,0},2)==1);assert(call(27,(uint8_t[]){4},1)==1);
    song.playing=1;assert(call(27,(uint8_t[]){3,7},2)==3);quiet();unchanged();pass("bounds/reserved objects/sector crossing/PLAY requests rejected; no unauthorized reads or writes");
    /* A malformed frame is rejected before any NOR work; actual service CRC. */
    build(99,27,(uint8_t[]){5,0},2);req[6]^=1;ed_drum_handle(req,req_n);assert(receive()==5);unchanged();pass("diagnostic request CRC fault rejected by actual7bit SysEx handler");
    {const uint32_t sizes[]={sizeof(project_v1_t),sizeof(project_v2_t),sizeof(project_v3_t),sizeof(project_v4_t),sizeof(project_v5_t),sizeof(project_t)};
     for(i=0;i<6;i++){len=sizes[i];assert(len<=sizeof st_buf);memset(st_buf,0,len);dw_w32(st_buf,PROJ_MAGIC_V1+i);dw_w32(st_buf+4,len);dw_w32(st_buf+len-4,proj_hash(st_buf,len-4));assert(project_legacy_wire_format(st_buf,len)==(int)i+1);st_buf[len-1]^=1;assert(!project_legacy_wire_format(st_buf,len));}}
    pass("all FUN1-6 numeric identities require exact source-defined size and FNV checksum; no FUNB guess");
    if(getenv("FM1_PHYSICAL_BACKUP")) {
        char name[80];fresh();load_physical("autosave-AB.bin",rawpair,8192);
        memcpy(nor+0x9f000,rawpair,4096);memcpy(nor+0xfe000,rawpair+4096,4096);
        for(i=0;i<4;i++){snprintf(name,sizeof name,"sample-bank-%u.bin",i);load_physical(name,nor+SMP_USER_OFF(i),SMP_USER_SIZE);}
        for(i=0;i<16;i++)drum_user_sample_scan(i);persistent_dependency=-1;
        memcpy(nor_before,nor,sizeof nor);e=erases;p=programs;
        assert(!save_destination_format(7,0,0)&&save_diag.code==3&&save_diag.copy==0&&save_diag.stage==5&&save_diag.raw_rc==0);
        project_report();assert(report[24]==1&&report[25]==0&&report[27]==5&&dw_u32(report+32)==3640&&dw_u32(report+36)==3);
        assert(dw_u32(report+80)==0x46554e42u&&report[88]==1&&report[89]==22&&dw_u32(report+100)==2);
        save_report("actual-autosave-report.bin",152);
        len=st_load(7,st_buf,sizeof st_buf);assert(len==3640&&!project_legacy_wire_format(st_buf,len)&&!save_wire_format(st_buf,len));
        for(i=0;i<16;i++) {
            sample_report(i);assert(((report[51]&4)!=0)==(i%4==0||i%4==3));
            if(i==0)assert(report[49]==5&&report[50]==4);
            if(i==3)assert(dw_u32(report+156)==4096&&report[49]==0&&report[50]==4);
            snprintf(name,sizeof name,"actual-sample-%u-report.bin",i);save_report(name,160);
        }
        assert(erases==e&&programs==p);unchanged();fixture("actual-partial-evidence.nor");
        pass("actual owner USB bytes: FUNB Aseq3 protected, FUN6 Bseq2 valid; exact two-bank protection, readonly CRC reports, no invented FUNB migration");
    }
    printf("PHYSICAL NOR DIAGNOSTICS PASS %u cases; host replays physical backups, not physical writes; unknown formats remain protected\n",case_count);return 0;
}
