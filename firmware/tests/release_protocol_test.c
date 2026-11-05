/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual editor_drum.c dispatcher; USB/panel/NOR peripherals and project service
 * are explicit host doubles. Real codec, voices, sample scan, storage.c and
 * persistence classification execute. This is NOT physical USB verification. */
#include <stdint.h>
static uint8_t nor[0x100000],nor_before[0x100000];
#define SMP_USER_XIP(k) (nor+((k)<3u?0xa0000u+(k)*0x14000u:0xe7000u))
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
static uint8_t save_io_failure_stage;
static int32_t save_io_failure_rc;
static uint32_t erases,programs,wire_sends,rec_calls;
static int fail_write,persistent_dependency,fail_project_load,fail_read,fail_erase;
static int fail_prog_after=-1,fail_erase_after=-1,partial_commit,silent_bad_program;
static uint32_t test_reads,test_read_fail_nth;
static int save_write_allowed(uint32_t off,uint32_t n);
static int st_read(uint32_t off,void *dst,uint32_t n){assert(off+n<=sizeof nor);test_reads++;if(fail_read||(test_read_fail_nth&&test_reads==test_read_fail_nth)){save_io_failure_stage=1;save_io_failure_rc=-81;return -81;}memcpy(dst,nor+off,n);return 0;}
static int st_erase(uint32_t off){assert(!(off&4095u)&&off+4096<=sizeof nor);if(!save_write_allowed(off,4096)){save_io_failure_stage=5;save_io_failure_rc=-104;return -104;}if(fail_erase||fail_erase_after==0){save_io_failure_stage=2;save_io_failure_rc=-82;return -82;}if(fail_erase_after>0)fail_erase_after--;memset(nor+off,255,4096);erases++;return 0;}
static int st_prog(uint32_t off,const void *src,uint32_t n){const uint8_t *p=src;uint32_t i;assert(n&&n<=256&&(off&255)+n<=256);if(!save_write_allowed(off,n)){save_io_failure_stage=5;save_io_failure_rc=-104;return -104;}if(fail_write||fail_prog_after==0){save_io_failure_stage=3;save_io_failure_rc=-83;return -83;}if(fail_prog_after>0)fail_prog_after--;if(partial_commit&&!(off&4095u)){for(i=0;i<n/2;i++)nor[off+i]&=p[i];save_io_failure_stage=3;save_io_failure_rc=-84;return -84;}for(i=0;i<n;i++)nor[off+i]&=p[i];programs++;if(silent_bad_program&&(off&4095u)==256u){nor[off+100]^=1;silent_bad_program=0;}return 0;}
#include "../firmware/src/storage.c"
static drumrec3_t drumrec3_buf;
#include "../firmware/src/persistence_diag.h"
static int save_wire_format(const void *p,uint32_t n){
 if(n==DRUMREC6_SIZE&&drumrec6_decode(&drumrec3_buf,p,n))return 6;
 if(n==DRUMREC5_SIZE&&drumrec5_decode(&drumrec3_buf,p,n))return 5;
 if(n==DRUMREC4_SIZE&&drumrec4_decode(&drumrec3_buf,p,n))return 4;
 if(n==DRUMREC3_SIZE&&drumrec3_decode(&drumrec3_buf,p,n))return 3;
 if(n==DRUMREC2_SIZE&&drumrec2_decode(&drumrec3_buf.base,p,n))return 2;
 if(n==DRUMREC_SIZE&&drumrec_decode(&drumrec3_buf.base.base,p,n))return 1;return 0;
}
static union{project_t v6;uint8_t raw[ST_PAYLOAD_MAX];}proj_tmp;
static project_t autosave_buf;
static struct{uint8_t force;char msg[64];}ui;
static uint8_t flash_ok=1,autosave_written_session,autosave_state;
static uint8_t autosave_recovery_hold;
static uint32_t autosave_ms,ui_input_ms,save_flash_jedec=0x856014;
static uint8_t lights_sync,lights_min,lights_mout,settings_later;
static int audio_quiet(void){uint32_t i;for(i=0;i<NDRUM;i++)if(drums.v[i].active)return 0;return 1;}
static uint32_t autosave_status(void){return autosave_state;}
static void autosave_write(int force){(void)force; /* explicit service double: ACK is not durable */ autosave_state=0;}
static void project_recovery_committed(uint32_t obj,const void *wire,uint32_t n){(void)wire;if(obj==7u&&n==DRUMREC6_SIZE){autosave_state=1;autosave_recovery_hold=0;}}
static void project_recovery_invalidated(uint32_t obj){if(obj==7u){autosave_state=0;autosave_recovery_hold=1;}}
static int project_sample_slot_referenced(uint32_t slot){(void)slot;return persistent_dependency;}
static int project_used(uint32_t slot){return st_load(OBJ_PROJECT0+slot,&proj_tmp,sizeof proj_tmp)>0;}
static int project_save(uint32_t slot){uint8_t b[DRUMREC6_SIZE];drumrec3_capture_live(&drumrec3_buf);if(!drumrec6_encode(b,sizeof b,&drumrec3_buf))return 0;return save_commit(OBJ_PROJECT0+slot,b,sizeof b)==0;}
static void project_load(uint32_t slot){int n;if(fail_project_load)return;n=st_load(OBJ_PROJECT0+slot,&proj_tmp,sizeof proj_tmp);if(n>0&&save_wire_format(&proj_tmp,n)){drumrec3_apply_live(&drumrec3_buf);str_cpy(ui.msg,"LOADED",sizeof ui.msg);}}
static int project_restore(uint32_t slot,const void *p,uint32_t n){if(!save_wire_format(p,n))return 2;if(slot==4){drumrec3_apply_live(&drumrec3_buf);return 0;}return save_commit(OBJ_PROJECT0+slot,p,n);}
/* Project-service double only. Actual occupied-slot restore is also exercised
 * by release_sample_emulator.py against the target image and real project.c. */
static int project_editor_restore(uint32_t slot,const void *p,uint32_t n){return project_restore(slot,p,n);}
static const uint8_t *ed_bk_obj(uint32_t id,uint32_t *len){int n;if(id<2||id>5){*len=0;return 0;}n=st_load(OBJ_PROJECT0+id-2,&proj_tmp,sizeof proj_tmp);*len=n>0?(uint32_t)n:0;return (const uint8_t *)&proj_tmp;}
static void fm1_wdt_feed(void){}
static int fl_write(uint32_t off,const void *src,uint32_t n){const uint8_t *p=src;while(n){uint32_t count=256u-(off&255u);int rc;if(count>n)count=n;rc=st_prog(off,p,count);if(rc)return rc;off+=count;p+=count;n-=count;}return 0;}
static int fl_erase4k_quiet(uint32_t off,uint32_t *took){*took=0;return st_erase(off);}
static void ed_smp_inval(uint32_t slot){(void)slot;}
static int ed_smp_erase(uint32_t slot,uint32_t all){uint32_t i;usr_nz[slot]=0;for(i=0;i<16;i++)usr_zone[slot][i].n=0;for(i=0;i<(all?20u:1u);i++){int rc=st_erase(SMP_USER_OFF(slot)+4096*i);if(rc)return rc;}return 0;}
static void rec_toggle(void){rec_calls++;song.rec^=1u;}
static uint8_t ed_out[600],wire_out[600];static uint32_t ed_n,wire_n;
static void ota_wire_send(const uint8_t *p,uint32_t n){assert(n<=sizeof wire_out);memcpy(wire_out,p,n);wire_n=n;wire_sends++;}
static void ed_begin(uint32_t cmd){memcpy(ed_out,(uint8_t[]){0xf0,0x7d,0x46,0x4c,(uint8_t)cmd},5);ed_n=5;}
static void ed_b(uint32_t b){assert(ed_n<sizeof ed_out-1);ed_out[ed_n++]=(uint8_t)(b&127u);}
static void ed_pack7(const uint8_t *p,uint32_t n){while(n){uint32_t count=n>7?7:n,i,mask=0;for(i=0;i<count;i++)mask|=(uint32_t)(p[i]>>7)<<i;ed_b(mask);for(i=0;i<count;i++)ed_b(p[i]);p+=count;n-=count;}}
static void ed_send(void){ed_out[ed_n++]=0xf7;ota_wire_send(ed_out,ed_n);}
#include "../firmware/src/editor_drum.c"
static uint8_t req[600],reply[DW_MAX];static uint32_t req_n,reply_n,case_count;static uint8_t next_id;
static uint32_t independent_crc(uint8_t version,uint8_t id,uint8_t op,uint8_t status,const uint8_t *payload,uint32_t n){uint8_t bytes[DW_MAX+4];bytes[0]=version;bytes[1]=id;bytes[2]=op;bytes[3]=status;if(n)memcpy(bytes+4,payload,n);return drum_sample_crc(bytes,n+4);}
static void build(uint8_t id,uint8_t op,const uint8_t *p,uint32_t n){uint8_t raw[DW_MAX+8];uint32_t i,j;assert(n<=DW_MAX);req[0]=1;req[1]=id;req[2]=op;req[3]=0;if(n)memcpy(raw,p,n);dw_w32(raw+n,independent_crc(1,id,op,0,p,n));n+=4;req_n=4;for(i=0;i<n;i+=7){uint32_t count=n-i>7?7:n-i,mask=0;for(j=0;j<count;j++)mask|=(uint32_t)(raw[i+j]>>7)<<j;req[req_n++]=(uint8_t)mask;for(j=0;j<count;j++)req[req_n++]=raw[i+j]&127u;}}
static uint32_t receive(void){uint32_t at=9,o=0,i;assert(wire_n>=15&&wire_out[0]==0xf0&&wire_out[1]==0x7d&&wire_out[2]==0x46&&wire_out[3]==0x4c&&wire_out[4]==80&&wire_out[5]==1&&wire_out[wire_n-1]==0xf7);for(i=1;i<wire_n-1;i++)assert(wire_out[i]<128);while(at<wire_n-1){uint32_t mask=wire_out[at++],count=wire_n-1-at;if(count>7)count=7;assert(count&&(mask>>count)==0);for(i=0;i<count;i++)reply[o++]=(uint8_t)(wire_out[at++]|((mask>>i)&1u)<<7);}assert(o>=4);reply_n=o-4;assert(dw_u32(reply+reply_n)==independent_crc(1,wire_out[6],wire_out[7],wire_out[8],reply,reply_n));return wire_out[8];}
static uint32_t call_id(uint8_t id,uint8_t op,const uint8_t *p,uint32_t n){build(id,op,p,n);fm1_ms++;ed_drum_handle(req,req_n);return receive();}
static uint32_t call(uint8_t op,const uint8_t *p,uint32_t n){return call_id(next_id++&127u,op,p,n);}
static void pass(const char *s){case_count++;printf("PASS %02u %s\n",case_count,s);}
static void quiet(void){song.playing=song.rec=rec_wait=ft_on=transport_req=0;memset(&drums,0,sizeof drums);}
static void upload_meta(uint8_t *p,uint32_t slot,uint32_t crc){memset(p,0,29);p[0]=(uint8_t)slot;dw_w32(p+2,512);dw_w32(p+6,22050);dw_w32(p+10,256);dw_w32(p+14,crc);memcpy(p+21,"TESTDRUM",8);}
static void backup_sample(uint32_t slot){uint8_t p[8];uint32_t off;for(off=0;off<drum_user_backup_size(slot);off+=256){p[0]=2;p[1]=(uint8_t)slot;dw_w32(p+2,off);dw_w16(p+6,256);assert(call(19,p,8)==0&&reply_n==268&&dw_u32(reply+2)==drum_user_backup_size(slot));assert(!memcmp(reply+12,nor+drum_user_slot_off(slot)+off,256));}assert(dw_backup_complete==(int32_t)slot);}
#ifndef PROTOCOL_TEST_ENTRY
#define PROTOCOL_TEST_ENTRY main
#endif
int PROTOCOL_TEST_ENTRY(void){uint8_t p[DW_MAX],before_pad[64],meta[29],sample[256],chunk[261],id;uint32_t i,j,k,e,pr,fp;drum_fx_config_t fx;
 setvbuf(stdout,0,_IONBF,0);memset(nor,255,sizeof nor);host_tracks_init();drum_preset_select(0);quiet();
 assert(drum_sample_crc("123456789",9)==0xcbf43926u);
 p[0]=0;p[1]=0;assert(call(25,p,2)==2);assert(call(1,0,0)==0&&reply_n==65u+sizeof(FM1_DRUM_ID)-1u&&reply[1]==2&&reply[2]==6&&reply[8]==55&&reply[9]==16);pass("capability handshake guards mutations; actual FL80 reply framing and header+payload CRC");
 assert(call(3,(uint8_t[]){7},1)==0&&reply_n==13);memcpy(p,reply,13);memcpy(before_pad,(const void *)drum_pad_edit,sizeof before_pad);
 p[1]=54;p[2]=81;p[3]=17;p[4]=1;p[5]=39;p[6]=43;p[7]=79;p[8]=45;dw_w32(p+9,0);assert(call(4,p,13)==0);assert(drum_custom_sample_ids[7]==54&&drum_pad_mix_get(7,1)==-47&&drum_pad_edit_get(7,DRUM_EDIT_START)==43);assert(!memcmp(before_pad,(const void *)drum_pad_edit,56));
 p[6]=p[7];assert(call(4,p,13)==1);p[6]=43;build(83,4,p,13);req[5]^=1;ed_drum_handle(req,req_n);assert(receive()==5&&drum_pad_edit_get(7,DRUM_EDIT_START)==43);p[1]=71;assert(call(4,p,13)==1);pass("PAD all fields, lane isolation, bounds, corrupt-request CRC rejects without mutation");
 build(84,3,(uint8_t[]){0},1);req[0]=2;ed_drum_handle(req,req_n);assert(receive()==2);build(85,3,(uint8_t[]){0},1);req[4]|=128;ed_drum_handle(req,req_n);assert(receive()==1);assert(call(3,(uint8_t[]){8},1)==1);assert(call(5,(uint8_t[]){9},1)==1);pass("version, malformed pack7, lane/effect bounds NACK with valid response CRC");
 for(i=0;i<9;i++){p[0]=(uint8_t)i;p[1]=(uint8_t)(i==8?6:8);for(j=2;j<6;j++)p[j]=(uint8_t)(j*17);assert(call(6,p,6)==0);assert(call(5,p,1)==0&&reply_n==6&&!memcmp(p,reply,6));}fx=drum_fx_get(8);p[0]=8;p[1]=7;assert(call(6,p,6)==1&&!memcmp(&fx,&drum_fx_config[8],5));pass("eight lane/master FX live roundtrip and strict type/four-parameter bounds");
 memset(p,0,143);p[0]=2;p[1]=1;for(i=0;i<8;i++){uint8_t *b=p+2+8*i;b[0]=(uint8_t)i;b[1]=90;b[2]=(uint8_t)-64;b[4]=(uint8_t)-24;b[5]=83;b[6]=99;b[7]=33;}assert(call(8,p,143)==0);assert(call(7,(uint8_t[]){2},1)==0&&reply_n==143&&!memcmp(p,reply,143));p[2+7]=83;assert(call(8,p,143)==1);pass("USER kit signed bytes/high-bit pack7, parameter/START bounds and exact snapshot readback");
 memset(p,0,43);p[0]=5;p[1]=2;p[2]=64;p[3]=35;p[4]=78;dw_w16(p+5,137);p[7]=8;p[7+10]=(uint8_t)-32;p[7+18]=128;p[7+18+17]=31;assert(call(10,p,43)==0);assert(call(9,(uint8_t[]){5,2},2)==0&&reply_n==43&&!memcmp(p,reply,43));p[7+1]=1;assert(call(10,p,43)==1);p[7+1]=0;p[7+10]=(uint8_t)-33;assert(call(10,p,43)==1);pass("actual pattern/raw signed offsets, Swing/QTZ/length; reserved and raw bounds reject");
 p[7+10]=(uint8_t)-32;for(i=0;i<2;i++){dw_w16(p+5,i?300:20);assert(call(10,p,43)==1&&song.g[G_BPM]==137);}for(i=0;i<2;i++){dw_w16(p+5,i?240:40);assert(call(10,p,43)==0);assert(call(2,0,0)==0);}pass("protocol BPM matches GP/DRM contract:20/300 reject,40/240 survive snapshot encoding");
 quiet();p[0]=0;p[1]=0;assert(call(25,p,2)==0);assert(call(17,0,0)==0&&reply_n==16);assert(call(16,(uint8_t[]){255},1)==0&&reply[0]==0);assert(call(18,0,0)==0&&reply_n==20&&dw_u32(reply+16)==save_flash_jedec);pass("REQUEST_SAVE ACK retains WAIT; diagnostic status and raw JEDEC/error fields are distinct");
 quiet();for(i=0;i<256;i++)sample[i]=(uint8_t)(i^0x96u);upload_meta(meta,0,st_crc32(sample,256));pr=programs;e=erases;meta[18]=1;assert(call(12,meta,29)==1&&programs==pr&&erases==e);meta[18]=0;assert(call(12,meta,29)==0&&dw_upload_slot==0&&!drum_usr_ready[0]);assert(!memcmp(nor+SMP_USER_OFF(0)+480,"FUP1",4));
 chunk[0]=0;dw_w32(chunk+1,0);memcpy(chunk+5,sample,256);id=next_id++&127u;assert(call_id(id,13,chunk,261)==0);pr=programs;assert(call_id(id,13,chunk,261)==0&&programs==pr);assert(call(13,chunk,261)==0&&programs==pr);chunk[6]^=1;assert(call(13,chunk,261)==8&&programs==pr);chunk[6]^=1;assert(call(14,(uint8_t[]){0},1)==0&&drum_usr_ready[0]);fp=dw_u32(reply+1);assert(fp==drum_sample_fingerprint(55));pass("sample BEGIN/intent/chunk CRC/header-last commit; cached and offset duplicate chunks do not reprogram");
 memset(p,0,13);p[0]=0;p[1]=55;p[2]=127;p[3]=64;p[5]=24;p[7]=100;dw_w32(p+9,fp);assert(call(4,p,13)==0);assert(call(23,(uint8_t[]){0,100,1},3)==0);assert(drums.v[0].active&&drums.v[0].s[4]==55);quiet();assert(call(15,(uint8_t[]){0,0x5a},2)==9);drum_preset_select(0);assert(call(15,(uint8_t[]){0,0x5a},2)==4);persistent_dependency=-1;assert(call(15,(uint8_t[]){0,0x5a},2)==4);persistent_dependency=1;assert(call(15,(uint8_t[]){0,0x5a},2)==9);persistent_dependency=0;pass("uploaded sample audition/assignment into real voice; live/persisted/unknown dependency guards");
 backup_sample(0);assert(dw_backup_unchanged(0));nor[SMP_USER_OFF(0)+5000]^=1;assert(call(15,(uint8_t[]){0,0x5a},2)==4);nor[SMP_USER_OFF(0)+5000]^=1;backup_sample(0);memcpy(nor_before,nor,sizeof nor);assert(call(15,(uint8_t[]){0,0x5a},2)==0);for(i=0;i<sizeof nor;i++)if(nor[i]!=nor_before[i])assert(i>=SMP_USER_OFF(0)&&i<SMP_USER_OFF(0)+SMP_USER_SIZE);for(i=0;i<SMP_USER_SIZE;i++)assert(nor[SMP_USER_OFF(0)+i]==255);pass("whole80KiB backup required beforedelete; changed-since-backup rejects; onlyauditedslot erased");
 upload_meta(meta,1,st_crc32(sample,256));assert(call(12,meta,29)==0);chunk[0]=1;assert(call(13,chunk,261)==0);dw_upload_slot=-1;assert(call(12,meta,29)==0);assert(!drum_usr_ready[1]);meta[14]^=1;assert(call(12,meta,29)==4);meta[14]^=1;assert(call(12,meta,29)==0);assert(call(13,chunk,261)==0);assert(call(14,(uint8_t[]){1},1)==0);pass("interrupted upload known FUP1 intent resumes only matching metadata; no premature sample exposure");
 upload_meta(meta,2,st_crc32(sample,256)^1);assert(call(12,meta,29)==0);chunk[0]=2;assert(call(13,chunk,261)==0);assert(call(14,(uint8_t[]){2},1)==5&&!drum_usr_ready[2]);nor[SMP_USER_OFF(3)]=0x42;upload_meta(meta,3,st_crc32(sample,256));e=erases;assert(call(12,meta,29)==4&&erases==e);pass("whole-upload CRC mismatch stays uncommitted; unknown legacy data is never erased");
 quiet();fm1_ms+=30001;ed_drum_tick();assert(call(20,(uint8_t[]){4,(uint8_t)DRUMREC6_SIZE,(uint8_t)(DRUMREC6_SIZE>>8),0,0,0,0,0x5a},8)==0);assert(call(2,0,0)==8);assert(call(19,(uint8_t[]){0,0,0,0,0,0,32,0},8)==8);assert(call(20,(uint8_t[]){255},1)==0);assert(call(2,0,0)==0);p[0]=0;p[1]=0;dw_w32(p+2,0);dw_w16(p+6,256);assert(call(19,p,8)==0);assert(call(2,0,0)==8);fm1_ms+=120001;ed_drum_tick();assert(!dw_work_backup&&dw_restore_slot<0);assert(call(2,0,0)==0);pass("working-backup/restore staging protected from state reads; abandon timeout releases staging");
 quiet();id=77;assert(call_id(id,24,(uint8_t[]){2},1)==0&&rec_calls==1);assert(call_id(id,24,(uint8_t[]){2},1)==0&&rec_calls==1);pass("cached valid mutating request retry sends prior ACK without repeatingREC toggle");
 build(78,3,(uint8_t[]){0},1);req[5]^=1;ed_drum_handle(req,req_n);assert(receive()==5);
 assert(call_id(id,24,(uint8_t[]){2},1)==0&&rec_calls==1);
 pass("CRC-invalid noise cannot evict verified retry cache and repeat REC toggle");
 quiet();drum_preset_select(0);p[0]=0;p[1]=1;p[2]=12;p[3]=45;p[4]=65;p[5]=70;assert(call(6,p,6)==0);assert(drum_kit_source_get()==255);
 {drum_fx_state_t history;int32_t block[CTL];memset(block,0,sizeof block);block[0]=16000;for(i=0;i<5;i++){drum_fx_lane_process(0,block,CTL,120);memset(block,0,sizeof block);}assert(dfx_state[0].fill);history=dfx_state[0];p[3]=68;assert(call(6,p,6)==0);assert(!memcmp(&history,&dfx_state[0],sizeof history));}
 pass("live same-type FX update preserves actual delay history and marks factory edit CUSTOM");
 {uint8_t image[DRUMREC6_SIZE],after[DRUMREC6_SIZE],begin[8],part[258],crcarg[4];uint32_t off,count,crc;
  for(off=0;off<sizeof image;off+=count){count=sizeof image-off;if(count>256)count=256;p[0]=0;p[1]=0;dw_w32(p+2,off);dw_w16(p+6,count);assert(call(19,p,8)==0&&reply_n==count+12);memcpy(image+off,reply+12,count);}
  assert(save_wire_format(image,sizeof image)==6);crc=st_crc32(image,sizeof image);
  assert(call(3,(uint8_t[]){0},1)==0);memcpy(p,reply,13);p[2]=7;assert(call(4,p,13)==0);
  begin[0]=4;dw_w16(begin+1,sizeof image);dw_w32(begin+3,crc);begin[7]=0x5a;assert(call(20,begin,8)==0);dw_w32(crcarg,crc);assert(call(22,crcarg,4)==8);
  for(off=0;off<sizeof image;off+=count){count=sizeof image-off;if(count>256)count=256;dw_w16(part,off);memcpy(part+2,image+off,count);assert(call(21,part,count+2)==0);assert(call(21,part,count+2)==0);}
  dw_w32(crcarg,crc^1u);assert(call(22,crcarg,4)==5);dw_w32(crcarg,crc);pr=programs;e=erases;assert(call(22,crcarg,4)==0&&programs==pr&&erases==e);
  drumrec3_capture_live(&drumrec3_buf);assert(drumrec6_encode(after,sizeof after,&drumrec3_buf)==sizeof after&&!memcmp(image,after,sizeof image));
 }
 pass("full chunked project backup/restore, duplicate chunks, partial/CRC rejects, exact runtime image; no false NOR commit");
 quiet();flash_ok=0;pr=programs;assert(call(16,(uint8_t[]){0},1)==6&&save_diag.code==SAVE_ERR_FLASH_NOT_READY&&programs==pr);flash_ok=1;assert(call(16,(uint8_t[]){0},1)==0);assert(st_load(OBJ_PROJECT0,&proj_tmp,sizeof proj_tmp)==DRUMREC6_SIZE);
 fail_project_load=1;assert(call(29,(uint8_t[]){0},1)==6);fail_project_load=0;assert(call(29,(uint8_t[]){0},1)==0&&!memcmp(ui.msg,"LOADED",6));
 pass("manual SAVE flash-not-ready refuses RAM success; confirmed LOAD must report actual successful backend result");

 printf("PROTOCOL HOST PASS %u cases; actualdispatcher/codec/voices/storagepolicy; USB/projectservices areexplicitdoubles; erases=%u programs=%u\n",case_count,erases,programs);
 return 0;
}
