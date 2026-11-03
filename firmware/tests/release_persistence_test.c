/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual unchanged storage.c + actual application classification/backup policy. */
#define PROJECT_TEST_NO_PROCESS 1
#define FELUCCA_RUNTIME_TRACKS 1
#define FELUCCA_RUNTIME_DRUM_TRACK 0
#define FELUCCA_DRUM_ONLY 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
#define PROJ_HOST 1
#define DRUMREC_HAVE_CORE 1
#include "../firmware/src/drum_record.c"
#undef DRUMREC_HAVE_CORE
static uint32_t trk_def_engine(uint32_t i){(void)i;return 0;}
#include "../firmware/src/project.c"
static uint8_t nor[0x100000],before[0x100000];
static uint8_t save_io_failure_stage;
static int32_t save_io_failure_rc;
static int fail_read,fail_erase,fail_after=-1,partial_header,silent_corrupt;
static uint32_t programs,erases,reads,read_fail_nth;
static int transient_commit_entry;
static int st_read(uint32_t off,void *dst,uint32_t n)
{
 assert(off+n<=sizeof nor);reads++;
 if(read_fail_nth&&reads==read_fail_nth){read_fail_nth=0;save_io_failure_stage=1;save_io_failure_rc=-81;return -81;}
 if(fail_read){save_io_failure_stage=1;save_io_failure_rc=-81;return -81;}
 memcpy(dst,nor+off,n);return 0;
}
static int save_write_allowed(uint32_t off,uint32_t n);
static int st_erase(uint32_t off)
{
 assert(!(off&4095));
 if(!save_write_allowed(off,4096u)){save_io_failure_stage=5;save_io_failure_rc=-104;return -104;}
 if(fail_erase){save_io_failure_stage=2;save_io_failure_rc=-82;return -82;}
 memset(nor+off,255,4096);erases++;return 0;
}
static int st_prog(uint32_t off,const void *src,uint32_t n)
{
 const uint8_t *b=src;uint32_t i;
 assert(n&&n<=256u&&(off&255u)+n<=256u);
 if(!save_write_allowed(off,n)){save_io_failure_stage=5;save_io_failure_rc=-104;return -104;}
 if(fail_after==0){save_io_failure_stage=3;save_io_failure_rc=-83;return -83;}
 if(fail_after>0)fail_after--;
 if(partial_header&&!(off&4095)) {
  for(i=0;i<n/2;i++)nor[off+i]&=b[i];save_io_failure_stage=3;save_io_failure_rc=-84;return -84;
 }
 for(i=0;i<n;i++)nor[off+i]&=b[i];programs++;
 if(silent_corrupt&&((off&4095u)==256u)){nor[off+100u]^=1;silent_corrupt=0;}
 return 0;
}
#include "../firmware/src/storage.c"
static drumrec3_t scratch;
/* Inject at the real primitive entry AFTER application classification. */
static int test_primitive_entry(uint32_t obj,const void *wire,uint32_t n)
{
 if(transient_commit_entry){read_fail_nth=reads+(uint32_t)transient_commit_entry;transient_commit_entry=0;printf("TRANSIENT read fault at actual st_current read ordinal %u\n",read_fail_nth);fflush(stdout);}
 return st_save(obj,wire,n);
}
#define st_save test_primitive_entry
#include "../firmware/src/persistence_diag.h"
#undef st_save
static int save_wire_format(const void *p,uint32_t n)
{
 if(n==DRUMREC6_SIZE&&drumrec6_decode(&scratch,p,n))return 6;
 if(n==DRUMREC5_SIZE&&drumrec5_decode(&scratch,p,n))return 5;
 if(n==DRUMREC4_SIZE&&drumrec4_decode(&scratch,p,n))return 4;
 if(n==DRUMREC3_SIZE&&drumrec3_decode(&scratch,p,n))return 3;
 if(n==DRUMREC2_SIZE&&drumrec2_decode(&scratch.base,p,n))return 2;
 if(n==DRUMREC_SIZE&&drumrec_decode(&scratch.base.base,p,n))return 1;
 return 0;
}
static uint8_t wireA[DRUMREC6_SIZE],wireB[DRUMREC6_SIZE],readback[ST_PAYLOAD_MAX],download[8192];
static uint32_t cases;
static void reset(void)
{
 memset(nor,255,sizeof nor);memset(&save_diag,0,sizeof save_diag);save_backup_cancel();
 fail_read=fail_erase=partial_header=silent_corrupt=0;fail_after=-1;
 save_io_failure_rc=save_io_failure_stage=0;reads=read_fail_nth=0;transient_commit_entry=0;
}
static void exact_current(const uint8_t *expected,uint32_t n)
{int got=st_load(OBJ_AUTOSAVE,readback,sizeof readback);assert(got==(int)n&&!memcmp(expected,readback,n));}
static void unchanged_outside(void)
{uint32_t i;for(i=0;i<sizeof nor;i++)if(before[i]!=nor[i])assert((i>=0x9f000&&i<0xa0000)||(i>=0xfe000&&i<0xff000));}
static void pass(const char *name){cases++;printf("PASS %02u %s\n",cases,name);}
static void write_pair_proof(const char *name,const uint8_t *image)
{
 const char *dir=getenv("FM1_TEST_OUTPUT");char path[2048];FILE *f;
 if(!dir||!*dir)return;
 assert(snprintf(path,sizeof path,"%s/%s",dir,name)>0);
 f=fopen(path,"wb");assert(f);
 assert(fwrite(image+0x9f000,1,4096,f)==4096&&fwrite(image+0xfe000,1,4096,f)==4096);fclose(f);
}
static void seed_pair(void)
{reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==0);}
static void backup_authorize(void)
{
 uint32_t crc,token,o;
 assert(save_backup_begin(OBJ_AUTOSAVE,&crc,&token));
 assert(!save_recovery_authorize(token,crc)); /* no partial backup is authorization */
 for(o=0;o<8192;o+=256){assert(save_backup_read(o,256,download+o));assert(save_backup_read(o,256,download+o));}
 assert(st_crc32(download,sizeof download)==crc);
 assert(save_recovery_authorize(token,crc));
}
int main(int argc,char **argv)
{
 drumrec3_t r;st_hdr_t h;uint32_t k,off,crc,token;int copy;
 (void)argc;(void)argv;
 host_tracks_init();drum_preset_select(0);drum_pad_edit_set(2,DRUM_EDIT_START,31);
 dstep_set(&TDRUM->dstep[61],7,LV_HARD,0);drum_raw_offset_set(7,61,-29);
 for(k=0;k<4;k++)assert(drum_user_kit_save(k));
 drumrec3_capture_live(&r);assert(drumrec6_encode(wireA,sizeof wireA,&r)==sizeof wireA);
 r.starts[2]=42;assert(drumrec6_encode(wireB,sizeof wireB,&r)==sizeof wireB);
 reset();memcpy(before,nor,sizeof nor);assert(save_destination_format(OBJ_AUTOSAVE,0,0)==6);
 assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);exact_current(wireA,sizeof wireA);unchanged_outside();pass("fresh blank + first commit + authorized-sector bounds");
 assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==0);exact_current(wireB,sizeof wireB);copy=st_current(OBJ_AUTOSAVE,&h);assert(copy==1&&h.seq==2);pass("second commit A/B sequence2");
 memset(&scratch,0,sizeof scratch);exact_current(wireB,sizeof wireB);assert(drumrec6_decode(&scratch,readback,sizeof wireB));drumrec3_apply_live(&scratch);drumrec3_capture_live(&r);assert(drumrec6_encode(readback,sizeof readback,&r)==sizeof wireB);assert(!memcmp(readback,wireB,sizeof wireB));pass("cold live projection equals exact stored raw/pattern/pads/FX/USER/START snapshot");
 memcpy(before,nor,sizeof nor);exact_current(wireB,sizeof wireB);assert(!memcmp(before,nor,sizeof nor));pass("cold no-new-commit leaves NOR byte identical");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);assert(st_copy_state(OBJ_AUTOSAVE,1)==ST_STATE_ERASED);assert(save_destination_format(OBJ_AUTOSAVE,0,0)==6);pass("A valid B erased permits next commit");
 seed_pair();nor[0xfe000+256+101]^=1;exact_current(wireA,sizeof wireA);
 /* Exact RC2 guard: any nonvalid/nonerased copy returns0 before codec. */
 assert(st_copy_state(OBJ_AUTOSAVE,0)==ST_STATE_VALID&&st_copy_state(OBJ_AUTOSAVE,1)==ST_STATE_CORRUPT);
 assert(save_destination_format(OBJ_AUTOSAVE,0,0)==0&&save_diag.code==SAVE_ERR_CRC_INVALID);
 printf("REPRODUCED RC2 NO SAVE: valid A + CRC-bad B is rejected, while cold load can still read A\n");
 memcpy(before,nor,sizeof nor);backup_authorize();assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==0);assert(!memcmp(before+0x9f000,nor+0x9f000,4096));exact_current(wireB,sizeof wireB);unchanged_outside();assert(!save_backup.authorized);pass("A valid B corrupt: backed-up confirmed one-use repair preserves A and cold-loads new B");
 seed_pair();nor[0x9f000+256+101]^=1;memcpy(before,nor,sizeof nor);backup_authorize();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);assert(!memcmp(before+0xfe000,nor+0xfe000,4096));exact_current(wireA,sizeof wireA);pass("A corrupt B valid symmetric confirmed repair preserves B");
 seed_pair();nor[0x9f000+256+101]^=1;nor[0xfe000+256+101]^=1;memcpy(before,nor,sizeof nor);assert(!save_destination_format(OBJ_AUTOSAVE,0,0));assert(save_backup_begin(OBJ_AUTOSAVE,&crc,&token));for(off=0;off<8192;off+=256)assert(save_backup_read(off,256,download+off));assert(!save_recovery_authorize(token,crc));assert(!memcmp(before,nor,sizeof nor));pass("both corrupt: exact backup allowed, repair blocked, bytes preserved");
 reset();nor[0x9f000]=0x12;memcpy(before,nor,sizeof nor);assert(!save_destination_format(OBJ_AUTOSAVE,0,0));assert(save_diag.code==SAVE_ERR_UNKNOWN_RECORD);assert(!memcmp(before,nor,sizeof nor));pass("unknown header is not erased and never treated blank");
 reset();assert(st_save(OBJ_AUTOSAVE,"FUN6",4)==0);memcpy(before,nor,sizeof nor);assert(!save_destination_format(OBJ_AUTOSAVE,0,0));assert(save_diag.code==SAVE_ERR_DESTINATION_PROTECTED);assert(!memcmp(before,nor,sizeof nor));pass("valid non-DRM/FUN object protected");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);memcpy(before,nor,sizeof nor);fail_erase=1;assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-82);assert(save_diag.code==SAVE_ERR_ERASE_FAILED&&save_diag.raw_rc==-82);fail_erase=0;exact_current(wireA,sizeof wireA);assert(!memcmp(before,nor,sizeof nor));pass("erase failure exact stage/raw error and survivor preserved");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);fail_after=0;assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-83);assert(save_diag.code==SAVE_ERR_PROGRAM_FAILED&&save_diag.raw_rc==-83);fail_after=-1;exact_current(wireA,sizeof wireA);pass("program failure exact raw error, previous state readable");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);silent_corrupt=1;assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-7);assert(save_diag.code==SAVE_ERR_READBACK_FAILED&&save_diag.stage==SAVE_STAGE_VERIFY);exact_current(wireA,sizeof wireA);pass("silent bad program caught by unchanged read-back/CRC verification");
 reset();fail_read=1;assert(!save_destination_format(OBJ_AUTOSAVE,0,0));assert(save_diag.code==SAVE_ERR_READ_FAILED&&save_diag.raw_rc==-81);fail_read=0;pass("read failure unknown classification preserves raw NOR rc");
 for(k=0;k<9;k++){reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);memcpy(before,nor,sizeof nor);fail_after=(int)k;assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)!=0);fail_after=-1;exact_current(wireA,sizeof wireA);assert(!memcmp(before+0x9f000,nor+0x9f000,4096));unchanged_outside();}
 pass("torn write at every body-page/header boundary preserves sole valid A");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);partial_header=1;assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-84);partial_header=0;exact_current(wireA,sizeof wireA);assert(!save_repairable(OBJ_AUTOSAVE,0));pass("partial header cannot prove ownership: fallback reads A, automatic repair forbidden");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);assert(save_backup_begin(OBJ_AUTOSAVE,&crc,&token));assert(!save_backup_read(256,256,download));for(off=0;off<8192;off+=256)assert(save_backup_read(off,256,download+off));nor[0xfe000+55]=0;assert(!save_recovery_authorize(token,crc));assert(save_diag.code==SAVE_ERR_NO_SAFE_RECOVERY_PATH);pass("backup coverage/token/current-pair CRC prevent partial or changed-state repair");
 reset();assert(project_autosave_write_due(5000,0,0));assert(!project_autosave_write_due(24999,5000,1));assert(project_autosave_write_due(25000,5000,1));assert(project_autosave_write_due(15000,0xffffec78u,1));pass("first save and20-second retry endurance/wrap policy unchanged");
 for(k=1;k<=6;k++) {size_t n=0;drumrec3_capture_live(&r);switch(k){case 1:n=drumrec_encode(readback,sizeof readback,&r.base.base);break;case 2:n=drumrec2_encode(readback,sizeof readback,&r.base);break;case 3:n=drumrec3_encode(readback,sizeof readback,&r);break;case 4:n=drumrec4_encode(readback,sizeof readback,&r);break;case 5:n=drumrec5_encode(readback,sizeof readback,&r);break;case 6:n=drumrec6_encode(readback,sizeof readback,&r);break;}assert(n&&save_wire_format(readback,(uint32_t)n)==(int)k);}
 pass("DRM1-6 exact version decoders retained; embedded-only historical compatibility");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);
 copy=st_current(OBJ_AUTOSAVE,&h);assert(copy==0);h.seq=0xffffffffu;h.hcrc=st_crc32(&h,sizeof h-4u);memcpy(nor+0x9f000,&h,sizeof h);
 /* Reproduce the unchanged primitive's wrap hazard: a verified new seq0
  * is not selected over the surviving max-sequence copy at cold LOAD. */
 assert(st_save(OBJ_AUTOSAVE,wireB,sizeof wireB)==0);exact_current(wireA,sizeof wireA);
 puts("REPRODUCED primitive max-sequence wrap: new verified seq0 does not beat persisted UINT32_MAX");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);
 copy=st_current(OBJ_AUTOSAVE,&h);assert(copy==0);h.seq=0xffffffffu;h.hcrc=st_crc32(&h,sizeof h-4u);memcpy(nor+0x9f000,&h,sizeof h);
 memcpy(before,nor,sizeof nor);off=programs;k=erases;
 assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-103);
 assert(save_diag.code==SAVE_ERR_NO_SAFE_RECOVERY_PATH&&save_diag.raw_rc==-103);
 assert(programs==off&&erases==k&&!memcmp(before,nor,sizeof nor));exact_current(wireA,sizeof wireA);
 pass("max A/B sequence fails closed before erase/program; old durable snapshot stays selected");
 reset();assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);memcpy(before,nor,sizeof nor);
 transient_commit_entry=1;fail_after=0;
 assert(save_commit(OBJ_AUTOSAVE,wireB,sizeof wireB)==-104);fail_after=-1;
 assert(save_diag.code==SAVE_ERR_READ_FAILED&&save_diag.raw_rc==-81);
 write_pair_proof("transient-before.raw8k",before);write_pair_proof("transient-after.raw8k",nor);
 assert(!memcmp(before+0x9f000,nor+0x9f000,4096));exact_current(wireA,sizeof wireA);
 pass("transient primitive read failure cannot redirect erase into sole valid survivor");
 seed_pair();memset(nor+0x9f000,255,4096);memcpy(before,nor,sizeof nor);
 transient_commit_entry=2; /* A erased read succeeds; B's header read fails once. */
 assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==-105);
 assert(save_diag.code==SAVE_ERR_READBACK_FAILED&&save_diag.raw_rc==-81);
 assert(!memcmp(before+0xfe000,nor+0xfe000,4096));exact_current(wireB,sizeof wireB);
 assert(!save_write_guard.active);assert(save_commit(OBJ_AUTOSAVE,wireA,sizeof wireA)==0);exact_current(wireA,sizeof wireA);
 pass("B survivor is preserved; wrong seq1 cannot be reported SAVED; healthy retry selects new seq3");
 printf("PERSISTENCE TARGETED PASS %u cases; programs=%u erases=%u; no physical hardware\n",cases,programs,erases);
 return 0;
}
