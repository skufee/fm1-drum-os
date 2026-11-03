/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual new dispatcher + scanner + voices + unchanged storage.c. Peripheral
 * and project-service doubles are inherited and explicitly not hardware. */
#define PROTOCOL_TEST_ENTRY corrective_baseline_protocol
#include "release_protocol_test.c"
int main(void)
{
 uint8_t meta[29],sample[256],chunk[261];uint32_t i,k,p,fp;drumrec3_t a,b;uint8_t wire[DRUMREC6_SIZE];
 assert(corrective_baseline_protocol()==0);
 memset(nor,255,sizeof nor);memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);
 memset(drum_user_valid,0,sizeof drum_user_valid);memset(drum_user_pads,0,sizeof drum_user_pads);
 memset(drum_user_fx,0,sizeof drum_user_fx);memset(drum_user_sample_refs,0,sizeof drum_user_sample_refs);
 quiet();drum_preset_select(0);persistent_dependency=0;
 for(i=0;i<256;i++)sample[i]=(uint8_t)(i^0x69u);
 dw_w32(chunk+1,0);memcpy(chunk+5,sample,256);
 for(k=0;k<16;k++) {
  upload_meta(meta,k,st_crc32(sample,256));memcpy(nor_before,nor,sizeof nor);
  assert(call(12,meta,29)==0);chunk[0]=(uint8_t)k;
  assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){(uint8_t)k},1)==0);
  assert(drum_usr_ready[k]&&dw_u32(reply+1)==drum_sample_fingerprint(55+k));
  for(i=0;i<sizeof nor;i++)if(i<drum_user_slot_off(k)||i>=drum_user_slot_off(k)+DRUM_USER_SLICE)assert(nor[i]==nor_before[i]);
  for(p=0;p<100;p++) {
   const smp_zone_t *z=drum_user_zone(k);voice_t ref={0},seek={0};uint32_t pos=z->n*p/100u;
   while(ref.ph[0]<pos)sample_next(z,&ref,0);
   assert(drum_usr_pred[k][p]==ref.s[0]&&drum_usr_idx[k][p]==ref.s[1]);
   assert(drum_start_prime(z,55+k,&seek,p)==pos&&seek.s[3]==sample_next(z,&ref,0));
  }
 }
 assert(call(11,(uint8_t[]){63,8},2)==0&&reply[0]==71&&reply[2]==8&&dw_u32(reply+3+27)==19968);
 pass("16uploads/1600exactSTARTstates/ID70/list71/cap19968; allneighborNOR preserved");
 {int16_t sentinel=(int16_t)(drum_usr_pred[15][37]^1);drum_usr_pred[15][37]=sentinel;
  song.playing=1;song.rec=1;assert(call(11,(uint8_t[]){63,8},2)==0);
  assert(drum_usr_pred[15][37]==sentinel&&drum_usr_ready[15]);
  drum_usr_pred[15][37]^=1;quiet();}
 pass("LIST duringPLAY/REC is read-only: readiness/checkpoints unchanged,no decode scan");
 quiet();for(i=0;i<6;i++){assert(drum_custom_sample_id_set(i,65+i));drum_pad_edit_set(i,DRUM_EDIT_START,37);drum_on(LANE_NOTE[i],100);}
 for(i=0;i<6;i++)assert(drums.v[i].active);quiet();
 drumrec3_capture_live(&a);
 assert(drumrec6_encode(wire,sizeof wire,&a)==sizeof wire&&drumrec6_decode(&b,wire,sizeof wire));
 assert(!memcmp(&a,&b,sizeof a)&&b.sample_ids[5]==70);
 assert(call(16,(uint8_t[]){0},1)==0);drum_preset_select(0);
 assert(call(29,(uint8_t[]){0},1)==0&&drum_custom_sample_ids[5]==70&&drum_pad_edit_get(5,DRUM_EDIT_START)==37);
 quiet();drum_preset_select(0);backup_sample(15);memcpy(nor_before,nor,sizeof nor);
 assert(call(15,(uint8_t[]){15,0x5a},2)==0);
 for(i=0;i<sizeof nor;i++)if(i<drum_user_slot_off(15)||i>=drum_user_slot_off(15)+DRUM_USER_SLICE)assert(nor[i]==nor_before[i]);
 for(k=0;k<15;k++)assert(drum_user_sample_scan(k));
 pass("sixvoices/highID70/DRM6roundtrip/SAVE-LOAD;20KiBbackup-delete preserves15neighbors");
 backup_sample(0);memcpy(nor_before,nor,sizeof nor);upload_meta(meta,0,st_crc32(sample,256));meta[1]=1;
 assert(call(12,meta,29)==0);chunk[0]=0;assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){0},1)==0);
 for(i=DRUM_USER_SLICE;i<SMP_USER_SIZE;i++)assert(nor[SMP_USER_OFF(0)+i]==nor_before[SMP_USER_OFF(0)+i]);
 pass("confirmedprimaryreplacement erasesonlyown20KiB,childdata retained");
 memset(nor,255,sizeof nor);memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);
 {smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+SMP_USER_OFF(0));memset(h,0,sizeof *h);
  h->magic=SMP_USER_MAGIC;h->version=1;h->nz=1;h->data_len=50000;memset((uint8_t *)h+512,0x22,50000);
  h->zone[0].n=100000;h->zone[0].le=99999;h->zone[0].rate=32768;h->zone[0].hi=127;h->crc=st_crc32((uint8_t *)h+512,50000);}
 assert(drum_user_sample_scan(0)&&drum_user_zone(0)->n==100000);
 memcpy(nor_before,nor,sizeof nor);upload_meta(meta,4,st_crc32(sample,256));assert(call(12,meta,29)==4);
 assert(!memcmp(nor,nor_before,sizeof nor)&&!drum_user_sample_scan(4)&&!drum_user_sample_scan(8));
 upload_meta(meta,12,st_crc32(sample,256));assert(call(12,meta,29)==0);chunk[0]=12;
 assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){12},1)==0);
 assert(!memcmp(nor+SMP_USER_OFF(0),nor_before+SMP_USER_OFF(0),61440));
 backup_sample(0);upload_meta(meta,0,st_crc32(sample,256));meta[1]=1;fp=drum_sample_fingerprint(67);
 assert(call(12,meta,29)==0);chunk[0]=0;assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){0},1)==0);
 assert(drum_user_sample_scan(12)&&drum_sample_fingerprint(67)==fp&&dw_slot_class(4)==0);
 pass("legacy100000frames preserved/overlaplocked; backed-upshrink freesextras,preserves nonoverlapchild");
 memset(nor+SMP_USER_OFF(1),255,SMP_USER_SIZE);memcpy(nor+SMP_USER_OFF(1),"UNKNOWN!",8);drum_user_sample_invalidate(1);
 upload_meta(meta,5,st_crc32(sample,256));memcpy(nor_before,nor,sizeof nor);
 assert(call(12,meta,29)==4&&!memcmp(nor,nor_before,sizeof nor));
 upload_meta(meta,16,st_crc32(sample,256));assert(call(12,meta,29)==1);
 upload_meta(meta,4,0);dw_w32(meta+2,39938);dw_w32(meta+10,19969);assert(call(12,meta,29)==1);
 pass("unknownparent/slot16/capacityoverrun failclosed beforeNORwrites");
 memset(nor,255,sizeof nor);memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);
 for(k=4;k<16;k+=4){upload_meta(meta,k,st_crc32(sample,256));assert(call(12,meta,29)==0);chunk[0]=(uint8_t)k;
  assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){(uint8_t)k},1)==0);
  for(i=4;i<=k;i+=4)assert(drum_usr_ready[i]);}
 memset((void *)drum_usr_ready,0,sizeof drum_usr_ready);
 for(i=0;i<16;i++)drum_user_sample_scan(i);
 assert(!drum_usr_ready[0]&&drum_usr_ready[4]&&drum_usr_ready[8]&&drum_usr_ready[12]);
 fp=drum_sample_fingerprint(59);assert(dw_slot_class(0)==0&&drum_sample_fingerprint(59)==fp);
 upload_meta(meta,0,st_crc32(sample,256));assert(call(12,meta,29)==0);chunk[0]=0;
 assert(call(13,chunk,261)==0&&call(14,(uint8_t[]){0},1)==0);
 assert(drum_sample_fingerprint(59)==fp&&drum_usr_ready[8]&&drum_usr_ready[12]);
 backup_sample(0);assert(call(15,(uint8_t[]){0,0x5a},2)==0);
 assert(drum_sample_fingerprint(59)==fp&&drum_usr_ready[8]&&drum_usr_ready[12]);
 pass("blankprimary with3children: successiveuploads/coldscan/primaryclass/reupload/delete retainallchildren");
 {uint32_t off=drum_user_slot_off(6);smp_user_hdr_t *h=(smp_user_hdr_t *)(nor+off);
  memset(nor+SMP_USER_OFF(2),255,SMP_USER_SIZE);memset(h,0,sizeof *h);
  h->magic=SMP_USER_MAGIC;h->version=1;h->nz=1;h->data_len=256;
  h->zone[0].n=512;h->zone[0].le=511;h->zone[0].rate=32768;h->zone[0].hi=127;
  memcpy((uint8_t *)h+512,sample,256);h->crc=st_crc32(sample,256);
  assert(!drum_user_sample_scan(6)&&dw_slot_class(6)==3);
  h->rsv2[0]=DRUM_USER_CHILD_MAGIC;h->rsv2[1]=7;assert(!drum_user_sample_scan(6));
  h->rsv2[1]=6;assert(drum_user_sample_scan(6));}
 pass("childFSMP requiresDRS1+exactlogicalslot;legacy-lookingdata cannotalias extras");
 {static uint8_t full[19968];uint32_t off;memset(full,0x22,sizeof full);
  upload_meta(meta,15,st_crc32(full,sizeof full));dw_w32(meta+2,sizeof full*2);dw_w32(meta+10,sizeof full);
  assert(call(12,meta,29)==0);chunk[0]=15;
  for(off=0;off<sizeof full;off+=256){dw_w32(chunk+1,off);memcpy(chunk+5,full+off,256);assert(call(13,chunk,261)==0);}
  assert(call(14,(uint8_t[]){15},1)==0&&drum_user_zone(15)->n==39936);
  {voice_t v={0};assert(drum_start_prime(drum_user_zone(15),70,&v,99)<39936);}}
 pass("exact19968-bytecapacity/78chunks/headerCRC andSTART99 acceptedwithoutboundaryoverrun");
 printf("CORRECTIVE TARGETED PASS %u cases;16logicalslots/DRM6; peripheral/project doubles,notphysicalUSB\n",case_count);
 return 0;
}
