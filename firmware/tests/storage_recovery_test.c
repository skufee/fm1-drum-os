/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual application retirement dispatcher + unchanged storage/sample code.
 * Peripheral/project-reference/autosave-cache doubles are explicit. No USB. */
#define PROTOCOL_TEST_ENTRY recovery_baseline_protocol
#include "release_protocol_test.c"
static uint8_t initial_nor[0x100000],initial_seed[DRUMREC6_SIZE],plan[48],seed[DRUMREC6_SIZE];
static uint32_t tests;
static int raw_fault;
static void ok(const char *s){tests++;printf("PASS %02u %s\n",tests,s);}
static void load_file(const char *name,void *dst,uint32_t n)
{
    const char *dir=getenv("FM1_RECOVERY_INPUT");char path[2048];FILE *f;assert(dir);
    assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);f=fopen(path,"rb");assert(f);
    assert(fread(dst,1,n,f)==n&&fgetc(f)==EOF);fclose(f);
}
static void output(const char *name,const void *raw,uint32_t n)
{
    const char *dir=getenv("FM1_TEST_OUTPUT");char path[2048];FILE *f;if(!dir)return;
    assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);f=fopen(path,"wb");assert(f);
    assert(fwrite(raw,1,n,f)==n);fclose(f);
}
static void reset(void)
{
    memcpy(nor,initial_nor,sizeof nor);quiet();memset(&sr,0,sizeof sr);save_backup_cancel();
    fail_read=fail_write=fail_erase=partial_commit=silent_bad_program=0;fail_prog_after=fail_erase_after=-1;
    test_read_fail_nth=0;test_reads=0;save_write_guard.active=0;save_io_failure_rc=save_io_failure_stage=0;
    persistent_dependency=0;dw_upload_slot=dw_restore_slot=-1;dw_work_backup=0;dw_backup_complete=-1;dw_ready=1;
    memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);memset(drum_usr_class,0,sizeof drum_usr_class);
    host_tracks_init();assert(drumrec6_decode(&drumrec3_buf,initial_seed,sizeof initial_seed));drumrec3_apply_live(&drumrec3_buf);
    for(uint32_t i=0;i<16;i++)drum_user_sample_scan(i);quiet();autosave_state=0;autosave_recovery_hold=0;
}
static void unchanged_outside(uint32_t base,uint32_t n)
{for(uint32_t i=0;i<sizeof nor;i++)if(i<base||i>=base+n)assert(nor_before[i]==nor[i]);}
static void backup_pair(uint32_t obj)
{
    uint32_t off;uint8_t p[5];assert(call(27,(uint8_t[]){0,(uint8_t)obj},2)==0);
    for(off=0;off<8192;off+=256){p[0]=1;dw_w16(p+1,off);dw_w16(p+3,256);assert(call(27,p,5)==0);}
}
static void prepare(void)
{backup_pair(7);assert(call(30,(uint8_t[]){0,7},2)==0&&reply_n==48);memcpy(plan,reply,48);}
static void read_seed(void)
{
    uint8_t p[5];uint32_t off,n;
    for(off=0;off<DRUMREC6_SIZE;off+=256){n=DRUMREC6_SIZE-off;if(n>256)n=256;p[0]=1;dw_w16(p+1,off);dw_w16(p+3,n);assert(call(30,p,5)==0&&reply_n==n+8);memcpy(seed+off,reply+8,n);}
    assert(st_crc32(seed,sizeof seed)==dw_u32(plan+20));assert(!memcmp(seed,initial_seed,sizeof seed));
}
static uint32_t commit(void)
{
    uint8_t p[15];p[0]=2;dw_w32(p+1,dw_u32(plan+16));dw_w32(p+5,dw_u32(plan+12));dw_w32(p+9,dw_u32(plan+20));p[13]=plan[3];p[14]=0x5a;return call(30,p,sizeof p);
}
static void bank_prepare(uint32_t bank)
{backup_sample(bank);assert(call(30,(uint8_t[]){3,(uint8_t)bank},2)==0&&reply_n==48);memcpy(plan,reply,48);}
static uint32_t bank_commit(uint32_t bank)
{uint8_t p[11];p[0]=4;dw_w32(p+1,dw_u32(plan+16));dw_w32(p+5,dw_u32(plan+12));p[9]=(uint8_t)bank;p[10]=0x5a;return call(30,p,11);}
static void raw_restore(uint32_t kind,uint32_t object,uint32_t copy,const uint8_t *source,uint32_t n)
{
    uint8_t p[265];uint32_t off,token,crc=st_crc32(source,n);
    if(kind)backup_sample(object);else backup_pair(object);
    p[0]=7;p[1]=(uint8_t)kind;p[2]=(uint8_t)object;p[3]=(uint8_t)copy;dw_w32(p+4,crc);memcpy(p+8,source,256);
    assert(call(30,p,264)==0&&reply_n==48);token=dw_u32(reply+16);off=dw_u32(reply+12);
    p[0]=8;dw_w32(p+1,token);dw_w32(p+5,off);dw_w32(p+9,crc);p[13]=0x5a;assert(call(30,p,14)==0);
    for(off=256;off<n;off+=256){p[0]=9;dw_w32(p+1,token);dw_w32(p+5,off);memcpy(p+9,source+off,256);if(raw_fault==3&&off+256u==n)p[9]^=1;assert(call(30,p,265)==0&&dw_u32(reply)==off+256);if(off==256){if(raw_fault==1){fail_read=1;assert(call(30,p,265)==6&&sr.raw==-81&&!sr.active);fail_read=0;return;}assert(call(30,p,265)==0);}}
    p[0]=10;dw_w32(p+1,token);dw_w32(p+5,crc);p[9]=0x5a;
    if(raw_fault==2){partial_commit=1;assert(call(30,p,10)==6&&sr.raw==-84);partial_commit=0;return;}
    if(raw_fault==3){assert(call(30,p,10)==5&&!sr.active&&sr.raw==-105);return;}
    assert(call(30,p,10)==0&&reply_n==20&&dw_u32(reply+8)==crc);
}
int main(void)
{
    uint8_t pair[8192],readback[DRUMREC6_SIZE];uint32_t i,j,reads_prepare,reads_commit,e,pr;st_hdr_t h;
    setvbuf(stdout,0,_IONBF,0);memset(initial_nor,255,sizeof initial_nor);
    load_file("autosave-AB.bin",pair,8192);memcpy(initial_nor+0x9f000,pair,4096);memcpy(initial_nor+0xfe000,pair+4096,4096);
    for(i=0;i<4;i++){char name[64];snprintf(name,sizeof name,"sample-bank-%u.bin",i);load_file(name,initial_nor+SMP_USER_OFF(i),SMP_USER_SIZE);snprintf(name,sizeof name,"project-%u-AB.bin",i+1);load_file(name,pair,8192);memcpy(initial_nor+st_sector(1+i,0),pair,8192);}
    load_file("current-runtime.drm",initial_seed,sizeof initial_seed);reset();output("actual-before.nor",nor,sizeof nor);
    assert(call(30,(uint8_t[]){0,7},2)==8);assert(!sr.active);ok("no full backup means no retirement plan or erase");
    prepare();read_seed();assert(plan[3]==0&&plan[26]==1&&plan[27]==22&&dw_u32(plan+4)==0x9f000);
    memcpy(nor_before,nor,sizeof nor);e=erases;pr=programs;assert(call(2,0,0)==0);assert(st_crc32(dw_transfer,DRUMREC6_SIZE)==dw_u32(plan+20));
    assert(call(16,(uint8_t[]){255},1)==3);assert(call(20,(uint8_t[]){255},1)==3);assert(erases==e&&programs==pr);
    ok("current runtime seed is exact1959-byte DRM6; status polls don't clobber it; edits/restore/SAVE blocked while staged");
    assert(commit()==0&&reply_n==20&&!reply[5]);assert(!sr.active);assert(!memcmp(nor_before+0xfe000,nor+0xfe000,4096));unchanged_outside(0x9f000,4096);
    assert(st_current(7,&h)==0&&h.seq==3&&h.len==DRUMREC6_SIZE&&!memcmp(st_buf,initial_seed,DRUMREC6_SIZE));
    output("after-copy-A.nor",nor,sizeof nor);ok("one confirmed A retirement atomically commits currentDRM6; originalFUN6B remains byte-identical");
    assert(!save_destination_format(7,1,0));assert(commit()==8);ok("first step does not authorize second copy or normal writes through remaining legacy");
    prepare();read_seed();assert(plan[3]==1&&plan[26]==0&&plan[27]==6);memcpy(nor_before,nor,sizeof nor);assert(commit()==0&&reply[5]);
    assert(!memcmp(nor_before+0x9f000,nor+0x9f000,4096));unchanged_outside(0xfe000,4096);assert(save_destination_format(7,1,0)==6);
    assert(st_current(7,&h)==1&&h.seq==4);output("after-copy-B.nor",nor,sizeof nor);ok("fresh second backup+confirmation commits B; A survives; normal A/B guard now acceptsDRM6");
    assert(st_load(7,readback,sizeof readback)==DRUMREC6_SIZE&&!memcmp(readback,initial_seed,sizeof readback));
    assert(save_commit(7,initial_seed,sizeof initial_seed)==0);assert(st_current(7,&h)==0&&h.seq==5);ok("unchanged ordinary commit/cold-selection/CRC works after retirement, without guard bypass");
    for(i=0;i<4;i++){if(i==1||i==2){backup_sample(i);assert(call(30,(uint8_t[]){3,(uint8_t)i},2)==8);continue;}bank_prepare(i);memcpy(nor_before,nor,sizeof nor);assert(bank_commit(i)==0&&reply[5]);unchanged_outside(SMP_USER_OFF(i),SMP_USER_SIZE);for(j=0;j<SMP_USER_SIZE;j++)assert(nor[SMP_USER_OFF(i)+j]==255);}
    for(i=0;i<16;i++){assert(drum_usr_class[i]==0);assert(drum_user_slot_access(i));}output("after-banks.nor",nor,sizeof nor);ok("only banks0/3 retired, banks1/2 never erased, all16 slices genuinely erased and accessible");
    for(i=0;i<4;i+=3){memcpy(nor_before,nor,sizeof nor);raw_restore(1,i,255,initial_nor+SMP_USER_OFF(i),SMP_USER_SIZE);unchanged_outside(SMP_USER_OFF(i),SMP_USER_SIZE);assert(!memcmp(nor+SMP_USER_OFF(i),initial_nor+SMP_USER_OFF(i),SMP_USER_SIZE));}
    memcpy(nor_before,nor,sizeof nor);raw_restore(0,7,0,initial_nor+0x9f000,4096);unchanged_outside(0x9f000,4096);
    memcpy(nor_before,nor,sizeof nor);raw_restore(0,7,1,initial_nor+0xfe000,4096);unchanged_outside(0xfe000,4096);
    assert(!memcmp(nor,initial_nor,sizeof nor));output("rollback-original.nor",nor,sizeof nor);
    assert(autosave_recovery_hold);assert(call(30,(uint8_t[]){6},1)==0&&autosave_recovery_hold);
    ok("explicit reverse restore writes firstpage/headerlast and restores both original bank images and A/B byte-for-byte, one target at a time");
    reset();prepare();read_seed();assert(commit()==0);prepare();read_seed();assert(commit()==0);memcpy(nor_before,nor,sizeof nor);
    drum_pad_mix_set(0,DRUM_MIX_LEVEL,80);raw_fault=2;raw_restore(0,7,0,nor_before+0x9f000,4096);raw_fault=0;
    assert(autosave_recovery_hold&&st_copy_state(7,0)==ST_STATE_VALID);assert(call(30,(uint8_t[]){6},1)==0&&autosave_recovery_hold);
    fm1_ms=dw_activity_ms+120001u;ed_drum_tick();assert(autosave_recovery_hold);unchanged_outside(0x9f000,4096);
    ok("even a failed partialfirstpage that yields validDRM cannot resume autosave; hold survives CANCEL and timeout");
    reset();memcpy(nor_before,nor,sizeof nor);raw_fault=1;raw_restore(0,7,0,initial_nor+0x9f000,4096);raw_fault=0;
    assert(autosave_recovery_hold&&!sr.active);assert(!memcmp(nor_before+0xfe000,nor+0xfe000,4096));unchanged_outside(0x9f000,4096);
    ok("duplicate-page read fault retains raw-81, fails rollback and preserves survivor/hold without retry");
    reset();memcpy(nor_before,nor,sizeof nor);raw_fault=3;raw_restore(0,7,0,initial_nor+0x9f000,4096);raw_fault=0;
    assert(autosave_recovery_hold&&st_copy_state(7,0)!=ST_STATE_VALID);for(i=0;i<256;i++)assert(nor[0x9f000+i]==255u);
    assert(!memcmp(nor_before+0xfe000,nor+0xfe000,4096));unchanged_outside(0x9f000,4096);
    ok("raw stream fullCRC mismatch refuses firstpage commit and preserves valid other-copy survivor/hold");
    /* No seed download, wrong token, changed RAM, changed original bytes. */
    reset();prepare();e=erases;assert(commit()==8&&erases==e);read_seed();plan[16]^=1;assert(commit()==8&&erases==e);plan[16]^=1;
    drum_pad_mix_set(0,DRUM_MIX_LEVEL,80);assert(commit()==5&&erases==e&&!sr.active);ok("incomplete seed/wrong token/current-runtime changed all refuse before erase");
    reset();prepare();read_seed();nor[0x9f000+100]^=1;e=erases;assert(commit()==4&&erases==e&&!sr.active);ok("changed original A/B bytes invalidate backup-bound authorization");
    /* Power interruption at either erase, every body page and commit header. */
    for(i=0;i<2;i++){reset();prepare();read_seed();memcpy(nor_before,nor,sizeof nor);fail_erase_after=(int)i;assert(commit()==6);assert(!memcmp(nor_before+0xfe000,nor+0xfe000,4096));unchanged_outside(0x9f000,4096);assert(st_current(7,&h)==(i?1:0));}
    ok("failure at pre-erase or journal erase preserves valid B; only intended A affected");
    for(i=0;i<=8;i++){reset();prepare();read_seed();memcpy(nor_before,nor,sizeof nor);fail_prog_after=(int)i;assert(commit()==6);assert(!memcmp(nor_before+0xfe000,nor+0xfe000,4096));unchanged_outside(0x9f000,4096);assert(st_current(7,&h)==1);}
    reset();prepare();read_seed();partial_commit=1;assert(commit()==6&&st_current(7,&h)==1);ok("all body-page/header/torn-header interruption boundaries leave old valid survivor cold-loadable");
    reset();prepare();read_seed();silent_bad_program=1;assert(commit()==6&&st_current(7,&h)==1);assert(!sr.active);ok("silent bad program rejected by original readbackCRC; no successful retirement ACK");
    /* Especially extra FUN6 classification reads, and final success reads. */
    reset();backup_pair(7);test_reads=0;assert(call(30,(uint8_t[]){0,7},2)==0);reads_prepare=test_reads;
    for(i=reads_prepare-12;i<=reads_prepare;i++){reset();backup_pair(7);test_reads=0;test_read_fail_nth=i;e=erases;assert(call(30,(uint8_t[]){0,7},2)!=0);assert(!sr.active&&erases==e);}
    ok("one-shot read faults during survivor reclassification never mistake validFUN6 for unknown or select B for erase");
    reset();prepare();read_seed();test_reads=0;assert(commit()==0);reads_commit=test_reads;
    for(i=reads_commit-12;i<=reads_commit;i++){reset();prepare();read_seed();test_reads=0;test_read_fail_nth=i;assert(commit()==6&&!sr.active&&autosave_state!=1);assert(!memcmp(initial_nor+0xfe000,nor+0xfe000,4096));}
    ok("final reports/pairCRC one-shot read failures cannot publish SAVED or CRC-zero success");
    reset();persistent_dependency=-1;backup_sample(0);assert(call(30,(uint8_t[]){3,0},2)==4);persistent_dependency=1;assert(call(30,(uint8_t[]){3,0},2)==9);ok("unknown persisted references or actual references prevent bank retirement before erase");
    reset();bank_prepare(0);persistent_dependency=1;e=erases;assert(bank_commit(0)==9&&erases==e);ok("dependencies rechecked at commit, not just preparation");
    for(i=0;i<20;i++){reset();bank_prepare(3);memcpy(nor_before,nor,sizeof nor);fail_erase_after=(int)i;assert(bank_commit(3)==6);unchanged_outside(SMP_USER_OFF(3),SMP_USER_SIZE);assert(!sr.active);}
    ok("bank interruption at each of20sector boundaries stays in exact80KiB target; new backup required");
    reset();backup_sample(0);backup_pair(7);e=erases;assert(call(30,(uint8_t[]){3,0},2)==4&&save_backup.active&&erases==e);assert(call(27,(uint8_t[]){2},1)==0);
    ok("bank preparation refuses an outstanding journal backup, never strands an unrelated AUTOSAVE lock");
    reset();bank_prepare(0);test_reads=0;assert(bank_commit(0)==0);reads_commit=test_reads;
    {const uint32_t faults[]={1,2,319,320,321,322};for(i=0;i<sizeof faults/sizeof faults[0];i++){reset();bank_prepare(0);memcpy(nor_before,nor,sizeof nor);test_reads=0;test_read_fail_nth=faults[i];assert(bank_commit(0)==6&&sr.raw==-81&&!sr.active);unchanged_outside(SMP_USER_OFF(0),SMP_USER_SIZE);}}
    for(i=reads_commit-2;i<=reads_commit;i++){reset();bank_prepare(0);memcpy(nor_before,nor,sizeof nor);test_reads=0;test_read_fail_nth=i;assert(bank_commit(0)==6&&sr.raw==-81&&!sr.active);unchanged_outside(SMP_USER_OFF(0),SMP_USER_SIZE);}
    ok("bank backup proof/final readback I/O faults retain raw-81 distinctly from changed content, no false success");
    reset();prepare();read_seed();assert(call(30,(uint8_t[]){6},1)==0&&!sr.active&&!save_backup.active);assert(commit()==8);ok("cancel consumes RAM proof only and cannot leave autosave backup lock");
    printf("STORAGE RECOVERY HOST PASS %u groups; physical data replay, no physical write\n",tests);return 0;
}
