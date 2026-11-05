/* SPDX-License-Identifier: GPL-3.0-only */
/* Drum application protocol v1 in legacy FL SysEx command80. No transport/HAL. */
#include "drum_version.h"
#include "drum_platform.h"
#define DW_MAX 384u
#define DW_CONFIRM 0x5au
static uint8_t dw_in[DW_MAX],dw_reply[DW_MAX],dw_cache[600],dw_transfer[DRUMREC6_SIZE];
static uint32_t dw_cache_n,dw_cache_crc,dw_cache_ms,dw_backup_next,dw_backup_last,dw_backup_last_n;
static uint32_t dw_upload_next,dw_upload_ms,dw_restore_next,dw_restore_n,dw_restore_crc;
static int32_t dw_backup_slot=-1,dw_backup_complete=-1,dw_upload_slot=-1,dw_restore_slot=-1;
static uint32_t dw_activity_ms;
static uint8_t dw_work_backup;
static uint32_t dw_backup_crc,dw_backup_running;
static uint8_t dw_id,dw_op,dw_ready,dw_cache_id,dw_cache_op,dw_valid_request;
static smp_user_hdr_t dw_upload_header;
static uint32_t dw_u16(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8;}
static uint32_t dw_u32(const uint8_t *p){return dw_u16(p)|dw_u16(p+2)<<16;}
static void dw_w16(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void dw_w32(uint8_t *p,uint32_t v){dw_w16(p,v);dw_w16(p+2,v>>16);}
static int dw_busy(void){return song.playing||song.rec||rec_wait||ft_on||transport_req;}
static int dw_write_busy(void){return dw_busy()||!audio_quiet();}
static void dw_changed(void){ui_input_ms=fm1_ms;ui.force=1;}
static int dw_unpack(const uint8_t *p,uint32_t n){uint32_t o=0,j,m,k;while(n){m=*p++;n--;k=n>7u?7u:n;if(!k||(m>>k)||o+k>DW_MAX)return -1;for(j=0;j<k;j++){if(*p>127u)return -1;dw_in[o++]=(uint8_t)(*p++|((m>>j)&1u)<<7);}n-=k;}return (int)o;}
static uint32_t dw_frame_crc(uint32_t status,const uint8_t *p,uint32_t n){uint8_t h[4]={1,dw_id,dw_op,(uint8_t)status};uint32_t c=save_crc_more(0xffffffffu,h,4);return ~save_crc_more(c,p,n);}
static void dw_send(uint32_t status,uint32_t n,uint32_t request_crc){
 ed_begin(80);ed_b(1);ed_b(dw_id);ed_b(dw_op);ed_b(status);
 if(n>DW_MAX-4u){status=1;n=0;ed_out[8]=1;}
 dw_w32(dw_reply+n,dw_frame_crc(status,dw_reply,n));ed_pack7(dw_reply,n+4u);ed_send();
 if(dw_valid_request){memcpy(dw_cache,ed_out,ed_n);dw_cache_n=ed_n;dw_cache_crc=request_crc;dw_cache_id=dw_id;dw_cache_op=dw_op;dw_cache_ms=fm1_ms;}
}
static uint32_t dw_status(void){uint32_t gap=autosave_written_session&&fm1_ms-autosave_ms<20000u?20000u-(fm1_ms-autosave_ms):0;
 memset(dw_reply,0,20);dw_reply[0]=(uint8_t)autosave_status();dw_reply[1]=save_diag.code;dw_reply[2]=save_diag.object;dw_reply[3]=save_diag.copy;dw_reply[4]=save_diag.stage;dw_w32(dw_reply+6,(uint32_t)save_diag.raw_rc);dw_w32(dw_reply+10,autosave_ms);dw_w16(dw_reply+14,gap);dw_w32(dw_reply+16,save_flash_jedec);return 16;}
static void dw_pad_out(uint32_t lane){dw_reply[0]=(uint8_t)lane;dw_reply[1]=drum_custom_sample_ids[lane];dw_reply[2]=(uint8_t)drum_pad_mix_get(lane,DRUM_MIX_LEVEL);dw_reply[3]=(uint8_t)(drum_pad_mix_get(lane,DRUM_MIX_PAN)+64);dw_reply[4]=(uint8_t)drum_pad_mix_get(lane,DRUM_MIX_MUTE);dw_reply[5]=(uint8_t)(drum_pad_edit_get(lane,DRUM_EDIT_TUNE)+24);dw_reply[6]=(uint8_t)drum_pad_edit_get(lane,DRUM_EDIT_START);dw_reply[7]=(uint8_t)drum_pad_edit_get(lane,DRUM_EDIT_END);dw_reply[8]=(uint8_t)drum_pad_edit_get(lane,DRUM_EDIT_DECAY);dw_w32(dw_reply+9,drum_sample_refs[lane]);}
static int dw_fx_valid(const uint8_t *p,uint32_t lane){uint32_t i;if(p[0]>(lane==8u?6u:8u))return 0;for(i=1;i<5;i++)if(p[i]>100u)return 0;return 1;}
static int dw_user_pad_valid(const uint8_t *p,uint32_t fp){return p[0]<DRUM_SAMPLE_COUNT&&p[1]<=127u&&(int8_t)p[2]>=-64&&(int8_t)p[2]<=63&&p[3]<=1&&(int8_t)p[4]>=-24&&(int8_t)p[4]<=24&&p[5]>0&&p[5]<=100&&p[6]<=127&&p[7]<p[5]&&(p[0]<55u?!fp:fp!=0);}
static int dw_slot_class(uint32_t slot){ /* 0 erased,1 validated onezone,2 own interrupted upload,3 unknown/protected */
 uint8_t b[64],intent[32];uint32_t o,i;int erased=1;
 if(!flash_ok||slot>=DRUM_USER_SAMPLE_COUNT||!drum_user_slot_access(slot))return 3;
 if(drum_user_sample_scan(slot))return 1;
 for(o=0;o<DRUM_USER_SLICE;o+=sizeof b){if(st_read(drum_user_slot_off(slot)+o,b,sizeof b))return 3;for(i=0;i<sizeof b;i++)if(b[i]!=255u)erased=0;fm1_wdt_feed();}
 if(erased)return 0;
 if(st_read(drum_user_slot_off(slot)+480u,intent,32u))return 3;
 if(memcmp(intent,"FUP1",4)||intent[4]!=1u||intent[5]!=slot||dw_u32(intent+28)!=st_crc32(intent,28))return 3;
 /* An interrupted upload is known only if no unrelated committed header remains. */
 if(st_read(drum_user_slot_off(slot),b,32u))return 3;
 for(i=0;i<32;i++)if(b[i]!=255u)return 3;
 return 2;
}
static int dw_sample_deps(uint32_t slot){return drum_sample_slot_referenced(slot)?1:project_sample_slot_referenced(slot);}
static int dw_backup_unchanged(uint32_t slot){uint8_t b[256];uint32_t off,crc=0xffffffffu;if(dw_backup_complete!=(int32_t)slot)return 0;for(off=0;off<drum_user_backup_size(slot);off+=256){if(st_read(drum_user_slot_off(slot)+off,b,256))return 0;crc=save_crc_more(crc,b,256);fm1_wdt_feed();}return ~crc==dw_backup_crc;}
/* Only the target20KiB slice, or the proven data extent of an old large
 * primary source. Adjacent independent children are never part of an erase. */
static uint32_t dw_sample_extent(uint32_t slot)
{
 const smp_user_hdr_t *h=(const smp_user_hdr_t *)drum_user_slot_xip(slot);
 uint32_t n=DRUM_USER_SLICE;
 if(slot<4u&&drum_usr_ready[slot]&&h->data_len+512u>n)n=(h->data_len+512u+4095u)&~4095u;
 return n;
}
static int dw_sample_erase(uint32_t slot,uint32_t n)
{
 uint32_t off,took,base=drum_user_slot_off(slot),i;
 if(slot>=DRUM_USER_SAMPLE_COUNT||n<DRUM_USER_SLICE||n>drum_user_backup_size(slot)||(n&4095u))return -1;
 for(i=slot%4u;i<DRUM_USER_SAMPLE_COUNT;i+=4u)
   if(drum_user_slot_off(i)>=base&&drum_user_slot_off(i)<base+n){drum_user_sample_invalidate(i);drum_usr_class[i]=3;}
 for(off=0;off<n;off+=4096u){int rc=fl_erase4k_quiet(base+off,&took);if(rc){ed_smp_inval(slot%4u);return rc;}}
 ed_smp_inval(slot%4u);return 0;
}
static void dw_sample_rescan_bank(uint32_t slot)
{uint32_t i;for(i=slot%4u;i<DRUM_USER_SAMPLE_COUNT;i+=4u)drum_user_sample_scan(i);}
#include "nor_diagnostics.h"
#include "storage_retirement.h"
static uint32_t dw_dispatch(uint32_t op,const uint8_t *p,uint32_t n,uint32_t *out){
 uint32_t i,j,slot,off,count,total,kind;int rc;*out=0;
 if(op!=1u&&op!=2u&&op!=3u&&op!=5u&&op!=7u&&op!=9u&&op!=11u&&op!=17u&&op!=18u&&op!=19u&&!dw_ready)return 2;
 /* Staged current-runtime seed/proofs cannot be overwritten by another edit,
  * upload, restore or backup. Physical panel edits are caught by seed equality. */
 if(sr.active&&op!=30u&&(op==4u||op==6u||op==8u||op==10u||(op>=12u&&op<=16u)||
    op==19u||(op>=20u&&op<=25u)||(op==26u&&n&&p[0])||op==28u||op==29u||
    (op==27u&&n&&p[0]<3u)))return 3;
 switch(op){
 case 1: if(n)return 1;memset(dw_reply,0,65u+sizeof(FM1_DRUM_ID)-1u);dw_reply[0]=1;dw_reply[1]=2;dw_reply[2]=6;dw_reply[3]=1;dw_reply[4]=8;dw_reply[5]=6;dw_reply[6]=6;dw_reply[7]=4;dw_reply[8]=55;dw_reply[9]=DRUM_USER_SAMPLE_COUNT;dw_reply[10]=64;dw_reply[11]=8;dw_reply[12]=6;dw_reply[13]=63;dw_reply[14]=7;dw_w16(dw_reply+16,256);dw_w16(dw_reply+18,DRUMREC6_SIZE);dw_w32(dw_reply+20,DRUM_USER_CAPACITY);memcpy(dw_reply+24,FM1_DRUM_FIRMWARE,sizeof(FM1_DRUM_FIRMWARE)-1u);memcpy(dw_reply+56,FM1_DRUM_ID,8);dw_reply[64]=(uint8_t)(sizeof(FM1_DRUM_ID)-1u);memcpy(dw_reply+65,FM1_DRUM_ID,sizeof(FM1_DRUM_ID)-1u);dw_ready=1;*out=65u+sizeof(FM1_DRUM_ID)-1u;return 0;
 case 2: if(n)return 1;fm1_irq_off();drumrec3_capture_live(&drumrec3_buf);fm1_irq_on();if(!drumrec6_encode(st_buf,sizeof st_buf,&drumrec3_buf))return 8;
 memset(dw_reply,0,24);dw_w16(dw_reply,DRUMREC6_SIZE);dw_reply[2]=(uint8_t)autosave_status();dw_reply[3]=save_diag.code;dw_w32(dw_reply+4,st_crc32(st_buf,DRUMREC6_SIZE)^(uint32_t)drum_qtz_strength_get());dw_reply[8]=song.playing;dw_reply[9]=song.rec;dw_reply[10]=rec_wait;dw_reply[11]=(uint8_t)clamp(song.g[G_SLOT]-1,0,3);dw_reply[12]=drum_pad_sel;dw_reply[13]=(uint8_t)drum_kit_source_get();dw_w16(dw_reply+14,song.g[G_BPM]);dw_reply[16]=(uint8_t)song.g[G_SWING];dw_reply[17]=drum_qtz_strength_get();dw_reply[18]=(uint8_t)trk_len(TDRUM);dw_reply[19]=rec_mode_get();dw_reply[20]=rec_grid_get();dw_reply[21]=rec_count_get();dw_reply[22]=(uint8_t)song.g[G_CLOCK];for(i=0;i<4;i++)if(project_used(i))dw_reply[23]|=(uint8_t)(1u<<i);*out=24;return 0;
 case 3: if(n!=1||p[0]>=8)return 1;dw_pad_out(p[0]);*out=13;return 0;
 case 4: if(n!=13||p[0]>=8||p[1]>=DRUM_SAMPLE_COUNT||p[2]>127||p[3]>127||p[4]>1||p[5]>48||p[6]>=p[7]||p[7]>100||p[8]>127)return 1;
 if(song.rec||rec_wait||ft_on)return 3;if(!drum_sample_available(p[1],dw_u32(p+9)))return 7;
 slot=p[0];drum_custom_sample_id_set(slot,p[1]);drum_pad_mix_set(slot,0,p[2]);drum_pad_mix_set(slot,1,(int)p[3]-64);drum_pad_mix_set(slot,2,p[4]);drum_pad_edit_set(slot,DRUM_EDIT_START,0);drum_pad_edit_set(slot,DRUM_EDIT_END,p[7]);drum_pad_edit_set(slot,DRUM_EDIT_START,p[6]);drum_pad_edit_set(slot,DRUM_EDIT_TUNE,(int)p[5]-24);drum_pad_edit_set(slot,DRUM_EDIT_DECAY,p[8]);dw_changed();dw_pad_out(slot);*out=13;return 0;
 case 5: if(n!=1||p[0]>8)return 1;dw_reply[0]=p[0];{drum_fx_config_t f=drum_fx_get(p[0]);memcpy(dw_reply+1,&f,5);}*out=6;return 0;
 case 6: if(n!=6||p[0]>8||!dw_fx_valid(p+1,p[0]))return 1;{drum_fx_config_t old=drum_fx_get(p[0]);if(old.type!=p[1])drum_fx_set(p[0],p[1]);for(i=0;i<4;i++)drum_fx_param_set(p[0],i,p[2+i]);}dw_changed();memcpy(dw_reply,p,6);*out=6;return 0;
 case 7: if(n!=1||p[0]>=4)return 1;slot=p[0];dw_reply[0]=(uint8_t)slot;dw_reply[1]=drum_user_valid[slot];memcpy(dw_reply+2,drum_user_pads[slot],64);memcpy(dw_reply+66,drum_user_fx[slot],45);for(i=0;i<8;i++)dw_w32(dw_reply+111+4*i,drum_user_sample_refs[slot][i]);*out=143;return 0;
 case 8: if(n!=143||p[0]>=4||p[1]>1)return 1;if(dw_busy())return 3;slot=p[0];if(p[1]){for(i=0;i<8;i++)if(!dw_user_pad_valid(p+2+8*i,dw_u32(p+111+4*i)))return 1;for(i=0;i<9;i++)if(!dw_fx_valid(p+66+5*i,i))return 1;}
 fm1_irq_off();drum_user_valid[slot]=p[1];if(p[1]){memcpy(drum_user_pads[slot],p+2,64);memcpy(drum_user_fx[slot],p+66,45);for(i=0;i<8;i++)drum_user_sample_refs[slot][i]=dw_u32(p+111+4*i);}else{memset(drum_user_pads[slot],0,64);memset(drum_user_fx[slot],0,45);memset(drum_user_sample_refs[slot],0,32);}fm1_irq_on();dw_changed();return 0;
 case 9: if(n!=2||p[0]>=64||!p[1]||p[1]>16||p[0]+p[1]>64)return 1;slot=p[0];count=p[1];dw_reply[0]=p[0];dw_reply[1]=p[1];dw_reply[2]=(uint8_t)trk_len(TDRUM);dw_reply[3]=(uint8_t)song.g[G_SWING];dw_reply[4]=drum_qtz_strength_get();dw_w16(dw_reply+5,song.g[G_BPM]);fm1_irq_off();for(i=0;i<count;i++){memcpy(dw_reply+7+18*i,&TDRUM->dstep[slot+i],10);for(j=0;j<8;j++)dw_reply[7+18*i+10+j]=(uint8_t)drum_raw_offset[j][slot+i];}fm1_irq_on();*out=7+18*count;return 0;
 case 10: if(n<7||p[0]>=64||!p[1]||p[1]>16||p[0]+p[1]>64||n!=7u+18u*p[1]||!p[2]||p[2]>64||p[3]>100||p[4]>100||dw_u16(p+5)<40||dw_u16(p+5)>240)return 1;if(dw_busy())return 3;
 for(i=0;i<p[1];i++){const uint8_t *s=p+7+18*i;if(s[1]||s[4]||s[5]||s[8]||s[9])return 1;for(j=0;j<8;j++)if((int8_t)s[10+j]<-32||(int8_t)s[10+j]>31)return 1;}
 undo_mark(TDRUM,(undo_sess+=4u)|3u);fm1_irq_off();for(i=0;i<p[1];i++){memcpy(&TDRUM->dstep[p[0]+i],p+7+18*i,10);for(j=0;j<8;j++)drum_raw_offset[j][p[0]+i]=(int8_t)p[7+18*i+10+j];}TDRUM->p[P_SLEN]=p[2];song.g[G_SWING]=p[3];song.g[G_BPM]=(int16_t)dw_u16(p+5);fm1_irq_on();drum_qtz_strength_set(p[4]);dw_changed();return 0;
 case 11: if(n!=2||p[0]>=DRUM_SAMPLE_COUNT||!p[1]||p[1]>8)return 1;count=p[1];if(p[0]+count>DRUM_SAMPLE_COUNT)count=DRUM_SAMPLE_COUNT-p[0];dw_reply[0]=DRUM_SAMPLE_COUNT;dw_reply[1]=p[0];dw_reply[2]=(uint8_t)count;
 for(i=0;i<count;i++){uint32_t id=p[0]+i;uint8_t *b=dw_reply+3+32*i;const smp_zone_t *z=0;const char *name;memset(b,0,32);b[0]=(uint8_t)id;if(id<55){z=&SMP_ZONES[SMP_SETS[0].z0+id];b[1]=3;b[2]=id<14?2:id<26?0:id<29?3:id<39?4:1;name=SMP_CUSTOM_SAMPLE_NAMES[id];}else{slot=id-55;int cls=drum_usr_class[slot];b[1]=drum_usr_ready[slot]?2:0;b[2]=5;name=drum_user_sample_name(id);if(drum_usr_ready[slot])z=drum_user_zone(slot);dw_w32(b+23,drum_sample_fingerprint(id));dw_w32(b+27,DRUM_USER_CAPACITY);b[31]=drum_usr_ready[slot]?1:0;if(cls==3)b[1]|=4;}
 for(j=0;j<12&&name&&name[j];j++)b[3+j]=(uint8_t)name[j];if(z){dw_w32(b+15,z->n);dw_w32(b+19,(uint32_t)(((uint64_t)z->rate*FS)>>16));}}*out=3+32*count;return 0;
 case 12: {uint8_t intent[32];int cls;
 if(n!=29||p[0]>=DRUM_USER_SAMPLE_COUNT||p[1]>1||dw_u32(p+2)<2||dw_u32(p+2)>DRUM_USER_CAPACITY*2u||dw_u32(p+6)<4000||dw_u32(p+6)>44100||dw_u32(p+10)!=(dw_u32(p+2)+1u)/2u||dw_u32(p+10)>DRUM_USER_CAPACITY||p[18]||p[19]||p[20])return 1;if(dw_write_busy())return 3;if(!flash_ok)return 6;
 slot=p[0];rc=dw_sample_deps(slot);if(rc)return rc<0?4:9;cls=dw_slot_class(slot);if(cls==3)return 4;
 if(cls==1&&(!(p[1]&1u)||!dw_backup_unchanged(slot)))return 4;
 for(i=21;i<29;i++)if(p[i]<32||p[i]>126)return 1;
 memset(&dw_upload_header,0,sizeof dw_upload_header);dw_upload_header.magic=SMP_USER_MAGIC;dw_upload_header.version=1;dw_upload_header.nz=1;memcpy(dw_upload_header.name,p+21,8);dw_upload_header.data_len=dw_u32(p+10);dw_upload_header.crc=dw_u32(p+14);
 if(slot>=4u){dw_upload_header.rsv2[0]=DRUM_USER_CHILD_MAGIC;dw_upload_header.rsv2[1]=slot;}
 {smp_zone_t *z=&dw_upload_header.zone[0];z->n=dw_u32(p+2);z->le=z->n-1u;z->rate=(uint32_t)(((uint64_t)dw_u32(p+6)<<16)/FS);z->root16=60*16;z->hi=127;}
 memset(intent,0,32);memcpy(intent,"FUP1",4);intent[4]=1;intent[5]=(uint8_t)slot;dw_w32(intent+8,dw_upload_header.data_len);dw_w32(intent+12,dw_upload_header.crc);dw_w32(intent+16,st_crc32(&dw_upload_header,sizeof dw_upload_header));dw_w32(intent+28,st_crc32(intent,28));
 if(cls==2){uint8_t old[32];if(st_read(drum_user_slot_off(slot)+480,old,32)||memcmp(old,intent,32))return 4;}
 total=dw_sample_extent(slot);if(dw_sample_erase(slot,total))return 6;if(fl_write(drum_user_slot_off(slot)+480,intent,32))return 6;ed_smp_inval(slot%4u);drum_usr_class[slot]=2;dw_upload_slot=(int32_t)slot;dw_upload_next=0;dw_upload_ms=fm1_ms;dw_backup_complete=-1;return 0;}
 case 13: if(n<6||n>261||p[0]>=DRUM_USER_SAMPLE_COUNT)return 1;slot=p[0];off=dw_u32(p+1);count=n-5;if(dw_write_busy())return 3;if(dw_upload_slot!=(int32_t)slot||fm1_ms-dw_upload_ms>30000u||off>dw_upload_header.data_len||count>dw_upload_header.data_len-off||count>256u||(off&255u))return 8;
 if(off<dw_upload_next){if(off+count>dw_upload_next||memcmp(drum_user_slot_xip(slot)+512+off,p+5,count))return 8;return 0;}
 if(off!=dw_upload_next||((count&255u)&&off+count!=dw_upload_header.data_len))return 8;off+=512;
 /* All target sectors were erased at BEGIN, before programming intent/data. */
 if(fl_write(drum_user_slot_off(slot)+off,p+5,count))return 6;ed_smp_inval(slot%4u);if(memcmp(drum_user_slot_xip(slot)+off,p+5,count))return 6;dw_upload_next+=count;dw_upload_ms=fm1_ms;return 0;
 case 14: if(n!=1||p[0]>=DRUM_USER_SAMPLE_COUNT)return 1;if(dw_write_busy())return 3;slot=p[0];if(dw_upload_slot!=(int32_t)slot||dw_upload_next!=dw_upload_header.data_len)return 8;if(st_crc32(drum_user_slot_xip(slot)+512,dw_upload_header.data_len)!=dw_upload_header.crc)return 5;
 if(fl_write(drum_user_slot_off(slot),(const uint8_t *)&dw_upload_header,sizeof dw_upload_header))return 6;ed_smp_inval(slot%4u);dw_upload_slot=-1;dw_sample_rescan_bank(slot);if(!drum_usr_ready[slot])return 6;dw_reply[0]=(uint8_t)slot;dw_w32(dw_reply+1,drum_sample_fingerprint(55+slot));*out=5;return 0;
 case 15: if(n!=2||p[0]>=DRUM_USER_SAMPLE_COUNT||p[1]!=DW_CONFIRM)return 1;if(dw_write_busy())return 3;slot=p[0];rc=dw_sample_deps(slot);if(rc)return rc<0?4:9;if(dw_slot_class(slot)!=1||!dw_backup_unchanged(slot))return 4;total=dw_sample_extent(slot);rc=dw_sample_erase(slot,total);if(!rc)dw_sample_rescan_bank(slot);dw_backup_complete=-1;return rc?6:0;
 case 16: if(n!=1||(p[0]>3&&p[0]!=255))return 1;if(p[0]==255){autosave_write(0);*out=dw_status();return 0;}if(dw_write_busy())return 3;if(!flash_ok){save_error(SAVE_ERR_FLASH_NOT_READY,-1,OBJ_PROJECT0+p[0],255,SAVE_STAGE_CLASSIFY);return 6;}if(!project_save(p[0]))return 6;*out=dw_status();return 0;
 case 17: case 18: if(n)return 1;*out=dw_status();if(op==18)*out=20;return 0;
 case 19: if(n!=8||p[0]>2||dw_u16(p+6)>256||!dw_u16(p+6))return 1;kind=p[0];slot=p[1];off=dw_u32(p+2);count=dw_u16(p+6);
 if(kind==0){if(slot||dw_restore_slot>=0)return 8;total=sizeof dw_transfer;if(!off){fm1_irq_off();drumrec3_capture_live(&drumrec3_buf);fm1_irq_on();if(!drumrec6_encode(dw_transfer,sizeof dw_transfer,&drumrec3_buf))return 8;dw_work_backup=1;}else if(!dw_work_backup)return 8;if(off>total||count>total-off)return 1;memcpy(dw_reply+12,dw_transfer+off,count);if(off+count==total)dw_work_backup=0;}
 else if(kind==1){const uint8_t *b;if(slot>=4)return 1;b=ed_bk_obj(2u+slot,&total);if(!b||!total)return 7;if(off>total||count>total-off)return 1;memcpy(dw_reply+12,b+off,count);}
 else{if(slot>=DRUM_USER_SAMPLE_COUNT||dw_write_busy()||dw_upload_slot>=0)return 3;total=drum_user_backup_size(slot);if(off>total||count>total-off)return 1;if(!off){dw_backup_slot=(int32_t)slot;dw_backup_next=0;dw_backup_complete=-1;dw_backup_running=0xffffffffu;}if(dw_backup_slot!=(int32_t)slot||(off!=dw_backup_next&&!(off==dw_backup_last&&count==dw_backup_last_n)))return 8;if(st_read(drum_user_slot_off(slot)+off,dw_reply+12,count))return 6;if(off==dw_backup_next){dw_backup_last=off;dw_backup_last_n=count;dw_backup_running=save_crc_more(dw_backup_running,dw_reply+12,count);dw_backup_next+=count;}if(dw_backup_next==total){dw_backup_complete=(int32_t)slot;dw_backup_crc=~dw_backup_running;}}
 dw_reply[0]=(uint8_t)kind;dw_reply[1]=(uint8_t)slot;dw_w32(dw_reply+2,total);dw_w32(dw_reply+6,off);dw_w16(dw_reply+10,count);*out=12+count;return 0;
 case 20: if(n==1&&p[0]==255){dw_restore_slot=-1;dw_work_backup=0;return 0;}if(n!=8||p[0]>4||p[7]!=DW_CONFIRM||dw_u16(p+1)>sizeof dw_transfer||dw_u16(p+1)<16)return 1;if(dw_write_busy()||dw_upload_slot>=0)return 3;dw_restore_slot=p[0];dw_restore_n=dw_u16(p+1);dw_restore_crc=dw_u32(p+3);dw_restore_next=0;return 0;
 case 21: if(n<3||n>258||dw_restore_slot<0)return 8;off=dw_u16(p);count=n-2;if(off>dw_restore_n||count>dw_restore_n-off)return 1;if(off<dw_restore_next){if(off+count>dw_restore_next||memcmp(dw_transfer+off,p+2,count))return 8;return 0;}if(off!=dw_restore_next)return 8;memcpy(dw_transfer+off,p+2,count);dw_restore_next+=count;return 0;
 case 22: if(n!=4||dw_restore_slot<0||dw_restore_next!=dw_restore_n)return 8;if(dw_write_busy())return 3;if(dw_u32(p)!=dw_restore_crc||st_crc32(dw_transfer,dw_restore_n)!=dw_restore_crc)return 5;if(!save_wire_format(dw_transfer,dw_restore_n))return 5;rc=(int)(dw_restore_slot==4?project_restore(4,dw_transfer,dw_restore_n):project_editor_restore((uint32_t)dw_restore_slot,dw_transfer,dw_restore_n));dw_restore_slot=-1;if(rc)return 4;dw_changed();return 0;
 case 23: if((n!=2&&n!=3)||p[0]>=DRUM_SAMPLE_COUNT||p[1]>127||(n==3&&(p[0]>=8||p[2]!=1)))return 1;if(song.rec||rec_wait||ft_on)return 3;if(n==3){fm1_irq_off();drum_on(LANE_NOTE[p[0]],p[1]);fm1_irq_on();return 0;}if(p[0]>=55&&!drum_usr_ready[p[0]-55])return 7;drum_preview(p[0],p[1]);return 0;
 case 24: if(n!=1||p[0]>2)return 1;if(p[0]<2){transport_req=p[0]?1:2;dw_changed();return 0;}rec_toggle();dw_changed();return 0;
 case 25: if(n!=2||p[0]>1||p[1]>=(p[0]?4u:6u))return 1;if(dw_busy())return 3;if(p[0]){if(!drum_user_kit_load(p[1]))return 7;}else drum_preset_select(p[1]);dw_changed();return 0;
 case 26: if(n!=1&&n!=14)return 1;if(p[0]>1||(!p[0]&&n!=1)||(p[0]&&n!=14))return 1;if(p[0]){const uint8_t *b=p+1;static const uint8_t ids[6]={G_DRCH,G_SYNC,G_ROUTE,G_MIDI,G_CHOTH,G_CLOCK};if(dw_busy())return 3;for(i=0;i<6;i++)if(b[i]<GP[ids[i]].min||b[i]>GP[ids[i]].max)return 1;if(b[6]>1||b[7]>4||b[8]>2||dw_u16(b+9)<40||dw_u16(b+9)>240||b[11]>100||b[12]>100)return 1;for(i=0;i<6;i++)song.g[ids[i]]=b[i];song.g[G_BPM]=(int16_t)dw_u16(b+9);song.g[G_SWING]=b[11];drum_qtz_strength_set(b[12]);rec_mode_set(b[6]);rec_grid_set(b[7]);rec_count_set(b[8]);lights_sync=(uint8_t)song.g[G_SYNC];lights_min=(uint8_t)song.g[G_ROUTE];lights_mout=(uint8_t)song.g[G_MIDI];settings_later=1;dw_changed();}
 dw_reply[0]=(uint8_t)song.g[G_DRCH];dw_reply[1]=(uint8_t)song.g[G_SYNC];dw_reply[2]=(uint8_t)song.g[G_ROUTE];dw_reply[3]=(uint8_t)song.g[G_MIDI];dw_reply[4]=(uint8_t)song.g[G_CHOTH];dw_reply[5]=(uint8_t)song.g[G_CLOCK];dw_reply[6]=rec_mode_get();dw_reply[7]=rec_grid_get();dw_reply[8]=rec_count_get();dw_w16(dw_reply+9,song.g[G_BPM]);dw_reply[11]=(uint8_t)song.g[G_SWING];dw_reply[12]=drum_qtz_strength_get();*out=13;return 0;
 case 27: if(!n||p[0]>5)return 1;if(dw_write_busy()||dw_upload_slot>=0||dw_restore_slot>=0)return 3;if(p[0]>=3)return nor_diag_dispatch(p,n,dw_reply,out);if(p[0]==0){save_copy_report_t report[2];uint32_t crc,token;if(n!=2||!save_backup_begin(p[1],&crc,&token)||!save_pair_report(p[1],report))return 4;dw_w32(dw_reply,crc);dw_w32(dw_reply+4,token);dw_w16(dw_reply+8,8192);for(i=0;i<2;i++){uint8_t *b=dw_reply+10+20*i;b[0]=report[i].state;b[1]=report[i].format;b[2]=report[i].owned;b[3]=0;dw_w32(b+4,report[i].sequence);dw_w32(b+8,report[i].length);dw_w32(b+12,report[i].payload_crc);dw_w32(b+16,report[i].raw_crc);}*out=50;return 0;}
 if(p[0]==2){if(n!=1)return 1;save_backup_cancel();return 0;}if(n!=5||!dw_u16(p+3)||dw_u16(p+3)>256)return 1;off=dw_u16(p+1);count=dw_u16(p+3);if(!save_backup_read(off,count,dw_reply+2))return 4;dw_w16(dw_reply,off);*out=count+2;return 0;
 case 28: if(n!=9||p[8]!=DW_CONFIRM)return 1;if(dw_write_busy())return 3;if(!save_recovery_authorize(dw_u32(p),dw_u32(p+4)))return 4;return 0;
 case 29: if(n!=1||p[0]>=4)return 1;if(dw_write_busy())return 3;if(!project_used(p[0]))return 7;{int length=st_load(OBJ_PROJECT0+p[0],&proj_tmp,sizeof proj_tmp);if(length<0||(!save_wire_format(&proj_tmp,(uint32_t)length)&&!proj_import(&autosave_buf,&proj_tmp,length)))return 4;}ui.msg[0]=0;project_load(p[0]);if(str_len(ui.msg)!=6u||memcmp(ui.msg,"LOADED",6))return 6;song.g[G_SLOT]=(int16_t)(p[0]+1u);dw_changed();return 0;
 case 30:return sr_dispatch(p,n,dw_reply,out);
 default:return 1;
 }
}
static void ed_drum_tick(void){
 if(fm1_ms-dw_activity_ms>120000u){if(sr.active)sr_invalidate(3);if(save_backup.active)save_backup_cancel();dw_work_backup=0;dw_restore_slot=-1;}
 if(dw_upload_slot>=0&&fm1_ms-dw_upload_ms>30000u)dw_upload_slot=-1;
}
static void ed_drum_handle(const uint8_t *a,uint32_t na){int decoded;uint32_t n,crc,reply_n=0,status;
 ed_drum_tick();dw_valid_request=0;dw_id=na>1?a[1]:0;dw_op=na>2?a[2]:0;
 if(na<4||a[0]!=1){dw_send(2,0,0);return;}
 if(a[1]>127||a[2]<1||a[2]>30||a[3]){dw_send(1,0,0);return;}
 decoded=dw_unpack(a+4,na-4);if(decoded<4){dw_send(1,0,0);return;}n=(uint32_t)decoded-4;crc=dw_u32(dw_in+n);
 if(dw_frame_crc(0,dw_in,n)!=crc){dw_send(5,0,0);return;}
 dw_valid_request=1;
 if(dw_cache_n&&dw_cache_id==dw_id&&dw_cache_op==dw_op&&dw_cache_crc==crc&&fm1_ms-dw_cache_ms<10000u){ota_wire_send(dw_cache,dw_cache_n);return;}
 /* A state read must never overwrite a staged restore. */
 if((dw_restore_slot>=0||dw_work_backup)&&(dw_op==2||(dw_restore_slot>=0&&dw_op==19))){dw_send(8,0,crc);return;}
 dw_activity_ms=fm1_ms;status=dw_dispatch(dw_op,dw_in,n,&reply_n);dw_send(status,status?0:reply_n,crc);
}
