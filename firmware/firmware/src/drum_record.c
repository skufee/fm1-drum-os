/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "drum_record.h"
#ifndef DRUMREC_HAVE_CORE
#include "core.h"
#endif
#ifndef DRUMREC_HAVE_CORE
#include <string.h>
#endif

#define DRUMREC_MAGIC 0x314D5244u /* "DRM1" little endian */
#define DRUMREC2_MAGIC 0x324D5244u /* "DRM2" little endian */
#define DRUMREC3_MAGIC 0x334D5244u /* "DRM3" little endian */
#define DRUMREC5_MAGIC 0x354D5244u /* "DRM5" little endian */
_Static_assert(DRUMREC5_SIZE == 1799u, "DRM5 remains below existing 3840-byte payload");
#define DRUMREC4_MAGIC 0x344D5244u /* "DRM4" little endian */
_Static_assert(DRUMREC4_SIZE == 1759u, "DRM4 remains below existing 3840-byte payload");
_Static_assert(DRUMREC_SIZE == 752u, "wire size: 16 header + 736 payload bytes");
_Static_assert(DRUMREC2_SIZE == 1264u && DRUMREC2_PAYLOAD_SIZE == 1248u,
               "DRM2 wire size: 16 header + 736 base + 512 raw timing bytes");
_Static_assert(DRUMREC3_SIZE == 1533u && DRUMREC3_PAYLOAD_SIZE == 1517u,
               "DRM3 wire size: DRM2 + current assignments + four compact user kits");
_Static_assert(NSTEP == DRUMREC_STEPS && sizeof(dstep_t) == DRUMREC_STEP_SIZE,
               "wire step payload must match the pinned SLOOP drum step array");

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        uint32_t k;
        c ^= *p++;
        for (k = 0; k < 8u; ++k) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}
static int valid(const drumrec_t *r)
{
    static const int16_t lo[DRUMREC_PROJECT_FIELDS] = {0,-64,0,0,0,1,0,0,0,0,0,0,-50,0,0,0};
    static const int16_t hi[DRUMREC_PROJECT_FIELDS] = {127,63,1,8,100,127,127,127,127,127,127,2,50,127,127,127};
    uint32_t i;
    if (!r || r->bpm < 40u || r->bpm > 240u || r->swing > 100u || r->selected_pad >= DRUMREC_PADS ||
        r->pattern_length == 0u || r->pattern_length > DRUMREC_STEPS || r->qtz_grid || r->qtz_strength) return 0;
    for (i=0;i<DRUMREC_PROJECT_FIELDS;i++)
        if (r->project_fields[i] < lo[i] || r->project_fields[i] > hi[i]) return 0;
    for (i=0;i<DRUMREC_STEPS;i++)
        if (r->steps[i][1] || r->steps[i][4] || r->steps[i][5] || r->steps[i][8] || r->steps[i][9]) return 0;
    for (i=0;i<DRUMREC_PADS;i++)
        if (r->pads[i].level > 127u || r->pads[i].pan < -64 || r->pads[i].pan > 63 || r->pads[i].mute > 1u ||
            r->pads[i].tune < -24 || r->pads[i].tune > 24 || r->pads[i].end < 1u || r->pads[i].end > 100u || r->pads[i].decay > 127u) return 0;
    return 1;
}

static void encode_payload(uint8_t *p, const drumrec_t *r)
{
    size_t o = 0; uint32_t i;
    put16(p+o, r->bpm); put16(p+o+2, r->swing); p[o+4]=r->selected_pad; p[o+5]=r->pattern_length;
    p[o+6]=r->qtz_grid; p[o+7]=r->qtz_strength; o+=8;
    for(i=0;i<DRUMREC_PROJECT_FIELDS;i++,o+=2) put16(p+o,(uint16_t)r->project_fields[i]);
    memcpy(p+o,r->steps,sizeof r->steps); o+=sizeof r->steps;
    for(i=0;i<DRUMREC_PADS;i++) {
        p[o++]=r->pads[i].level; p[o++]=(uint8_t)r->pads[i].pan; p[o++]=r->pads[i].mute;
        p[o++]=(uint8_t)r->pads[i].tune; p[o++]=r->pads[i].end; p[o++]=r->pads[i].decay; p[o++]=0;
    }
}

static int decode_payload(drumrec_t *r, const uint8_t *p)
{
    size_t o=0; uint32_t i;
    memset(r,0,sizeof *r); r->bpm=get16(p+o); r->swing=get16(p+o+2); r->selected_pad=p[o+4];
    r->pattern_length=p[o+5]; r->qtz_grid=p[o+6]; r->qtz_strength=p[o+7]; o+=8;
    for(i=0;i<DRUMREC_PROJECT_FIELDS;i++,o+=2) r->project_fields[i]=(int16_t)get16(p+o);
    memcpy(r->steps,p+o,sizeof r->steps); o+=sizeof r->steps;
    for(i=0;i<DRUMREC_PADS;i++) {
        r->pads[i].level=p[o++]; r->pads[i].pan=(int8_t)p[o++]; r->pads[i].mute=p[o++];
        r->pads[i].tune=(int8_t)p[o++]; r->pads[i].end=p[o++]; r->pads[i].decay=p[o++];
        if(p[o++] != 0u) return 0;
    }
    return valid(r);
}

size_t drumrec_encode(void *dst, size_t capacity, const drumrec_t *r)
{
    uint8_t *b = (uint8_t *)dst;
    if (!b || capacity < DRUMREC_SIZE || !valid(r)) return 0;
    memset(b, 0, DRUMREC_SIZE); put32(b, DRUMREC_MAGIC); put16(b+4, DRUMREC_VERSION);
    put16(b+6, DRUMREC_HEADER_SIZE); put32(b+8, DRUMREC_PAYLOAD_SIZE);
    encode_payload(b+DRUMREC_HEADER_SIZE,r);
    put32(b+12,crc32(b+DRUMREC_HEADER_SIZE,DRUMREC_PAYLOAD_SIZE));
    return DRUMREC_SIZE;
}
int drumrec_decode(drumrec_t *r, const void *src, size_t n)
{
    const uint8_t *b=(const uint8_t *)src;
    drumrec_t decoded;
    if (!r || !b || n != DRUMREC_SIZE || get32(b)!=DRUMREC_MAGIC || get16(b+4)!=DRUMREC_VERSION ||
        get16(b+6)!=DRUMREC_HEADER_SIZE || get32(b+8)!=DRUMREC_PAYLOAD_SIZE ||
        get32(b+12)!=crc32(b+DRUMREC_HEADER_SIZE,DRUMREC_PAYLOAD_SIZE)) return 0;
    if (!decode_payload(&decoded,b+DRUMREC_HEADER_SIZE)) return 0;
    *r=decoded;
    return 1;
}

size_t drumrec2_encode(void *dst, size_t capacity, const drumrec2_t *r)
{
    uint8_t *b=(uint8_t *)dst; uint32_t lane,step;
    if(!b || capacity<DRUMREC2_SIZE || !r || !valid(&r->base)) return 0;
    memset(b,0,DRUMREC2_SIZE); put32(b,DRUMREC2_MAGIC); put16(b+4,DRUMREC2_VERSION);
    put16(b+6,DRUMREC_HEADER_SIZE); put32(b+8,DRUMREC2_PAYLOAD_SIZE);
    encode_payload(b+DRUMREC_HEADER_SIZE,&r->base);
    for(lane=0;lane<DRUMREC_PADS;lane++) for(step=0;step<DRUMREC_STEPS;step++) {
        int32_t v=r->raw_offsets[lane][step];
        if(v < -32 || v > 31) return 0;
        b[DRUMREC_HEADER_SIZE+DRUMREC_PAYLOAD_SIZE+lane*DRUMREC_STEPS+step]=(uint8_t)(int8_t)v;
    }
    put32(b+12,crc32(b+DRUMREC_HEADER_SIZE,DRUMREC2_PAYLOAD_SIZE));
    return DRUMREC2_SIZE;
}

int drumrec2_decode(drumrec2_t *r, const void *src, size_t n)
{
    const uint8_t *b=(const uint8_t *)src; drumrec2_t decoded; uint32_t lane,step;
    const uint8_t *payload;
    if(!r || !b || n!=DRUMREC2_SIZE || get32(b)!=DRUMREC2_MAGIC || get16(b+4)!=DRUMREC2_VERSION ||
       get16(b+6)!=DRUMREC_HEADER_SIZE || get32(b+8)!=DRUMREC2_PAYLOAD_SIZE ||
       get32(b+12)!=crc32(b+DRUMREC_HEADER_SIZE,DRUMREC2_PAYLOAD_SIZE)) return 0;
    payload=b+DRUMREC_HEADER_SIZE;
    if(!decode_payload(&decoded.base,payload)) return 0;
    for(lane=0;lane<DRUMREC_PADS;lane++) for(step=0;step<DRUMREC_STEPS;step++) {
        int8_t v=(int8_t)payload[DRUMREC_PAYLOAD_SIZE+lane*DRUMREC_STEPS+step];
        if(v < -32 || v > 31) return 0;
        decoded.raw_offsets[lane][step]=v;
    }
    *r=decoded;
    return 1;
}

static int valid_sample_id(uint32_t id) { return id < 55u; }

static int valid3_limit(const drumrec3_t *r,uint32_t sample_limit)
{
    uint32_t i, k, step;
    if (!r || !valid(&r->base.base) ||
        (r->source_id != DRUMREC3_SOURCE_CUSTOM && r->source_id >= 16u)) return 0;
    for (i = 0; i < DRUMREC_PADS; i++) {
        if (r->sample_ids[i]>=sample_limit) return 0;
        for (step = 0; step < DRUMREC_STEPS; step++)
            if (r->base.raw_offsets[i][step] < -32 || r->base.raw_offsets[i][step] > 31) return 0;
    }
    for (k = 0; k < DRUMREC3_USER_KITS; k++) {
        if (r->user_valid[k] > 1u) return 0;
        for (i = 0; i < DRUMREC_PADS; i++) {
            const drumrec3_pad_t *p = &r->user_kits[k][i];
            if (!r->user_valid[k]) {
                if (p->sample_id || p->level || p->pan || p->mute || p->tune || p->end || p->decay || p->reserved)
                    return 0;
            } else if (p->sample_id>=sample_limit || p->level > 127u || p->pan < -64 || p->pan > 63 ||
                       p->mute > 1u || p->tune < -24 || p->tune > 24 || p->end < 1u || p->end > 100u ||
                       p->decay > 127u || p->reserved) {
                return 0;
            }
        }
    }
    return 1;
}

static int valid3(const drumrec3_t *r){return valid3_limit(r,55u);}
static size_t drumrec3_encode_limit(void *dst,size_t capacity,const drumrec3_t *r,uint32_t sample_limit)
{
    uint8_t *b = (uint8_t *)dst;
    size_t o;
    uint32_t lane, step, kit;
    if (!b || capacity < DRUMREC3_SIZE || !valid3_limit(r,sample_limit)) return 0;
    memset(b, 0, DRUMREC3_SIZE);
    put32(b, DRUMREC3_MAGIC); put16(b + 4, DRUMREC3_VERSION);
    put16(b + 6, DRUMREC_HEADER_SIZE); put32(b + 8, DRUMREC3_PAYLOAD_SIZE);
    encode_payload(b + DRUMREC_HEADER_SIZE, &r->base.base);
    o = DRUMREC_HEADER_SIZE + DRUMREC_PAYLOAD_SIZE;
    for (lane = 0; lane < DRUMREC_PADS; lane++)
        for (step = 0; step < DRUMREC_STEPS; step++)
            b[o + lane * DRUMREC_STEPS + step] = (uint8_t)r->base.raw_offsets[lane][step];
    o += DRUMREC2_RAW_SIZE;
    b[o++] = r->source_id;
    memcpy(b + o, r->sample_ids, DRUMREC_PADS); o += DRUMREC_PADS;
    for (kit = 0; kit < DRUMREC3_USER_KITS; kit++) {
        b[o++] = r->user_valid[kit];
        for (lane = 0; lane < DRUMREC_PADS; lane++) {
            const drumrec3_pad_t *p = &r->user_kits[kit][lane];
            b[o++] = p->sample_id; b[o++] = p->level; b[o++] = (uint8_t)p->pan; b[o++] = p->mute;
            b[o++] = (uint8_t)p->tune; b[o++] = p->end; b[o++] = p->decay; b[o++] = 0;
        }
    }
    put32(b + 12, crc32(b + DRUMREC_HEADER_SIZE, DRUMREC3_PAYLOAD_SIZE));
    return DRUMREC3_SIZE;
}

size_t drumrec3_encode(void *dst,size_t capacity,const drumrec3_t *r){return drumrec3_encode_limit(dst,capacity,r,55u);}
static int decode3_body_limit(drumrec3_t *decoded,const uint8_t *b,uint32_t sample_limit)
{
    size_t o; uint32_t lane, step, kit;
    memset(decoded, 0, sizeof *decoded);
    if (!decode_payload(&decoded->base.base, b + DRUMREC_HEADER_SIZE)) return 0;
    o = DRUMREC_HEADER_SIZE + DRUMREC_PAYLOAD_SIZE;
    for (lane = 0; lane < DRUMREC_PADS; lane++) for (step = 0; step < DRUMREC_STEPS; step++) {
        int8_t v = (int8_t)b[o + lane * DRUMREC_STEPS + step];
        if (v < -32 || v > 31) return 0;
        decoded->base.raw_offsets[lane][step] = v;
    }
    o += DRUMREC2_RAW_SIZE;
    decoded->source_id = b[o++];
    memcpy(decoded->sample_ids, b + o, DRUMREC_PADS); o += DRUMREC_PADS;
    for (kit = 0; kit < DRUMREC3_USER_KITS; kit++) {
        decoded->user_valid[kit] = b[o++];
        for (lane = 0; lane < DRUMREC_PADS; lane++) {
            drumrec3_pad_t *p = &decoded->user_kits[kit][lane];
            p->sample_id = b[o++]; p->level = b[o++]; p->pan = (int8_t)b[o++]; p->mute = b[o++];
            p->tune = (int8_t)b[o++]; p->end = b[o++]; p->decay = b[o++]; p->reserved = b[o++];
        }
    }
    return valid3_limit(decoded,sample_limit);
}

static int decode3_body(drumrec3_t *decoded,const uint8_t *b){return decode3_body_limit(decoded,b,55u);}
int drumrec3_decode(drumrec3_t *r, const void *src, size_t n)
{
    const uint8_t *b = (const uint8_t *)src;
    drumrec3_t decoded;
    if (!r || !b || n != DRUMREC3_SIZE || get32(b) != DRUMREC3_MAGIC || get16(b + 4) != DRUMREC3_VERSION ||
        get16(b + 6) != DRUMREC_HEADER_SIZE || get32(b + 8) != DRUMREC3_PAYLOAD_SIZE ||
        get32(b + 12) != crc32(b + DRUMREC_HEADER_SIZE, DRUMREC3_PAYLOAD_SIZE)) return 0;
    if (!decode3_body(&decoded, b)) return 0;
    *r = decoded;
    return 1;
}

static int valid4_fx(const drum_fx_config_t *f, uint32_t lane)
{
    uint32_t p;
    if (f->type > (lane == 8u ? 6u : 8u)) return 0;
    for (p = 0; p < 4u; p++) if (f->p[p] > 100u) return 0;
    return 1;
}
static int valid4_limit(const drumrec3_t *r,uint32_t sample_limit)
{
    uint32_t k, lane;
    if (!valid3_limit(r,sample_limit) || r->map_version != 1u ||
        (r->source_id != DRUMREC3_SOURCE_CUSTOM && r->source_id >= 6u)) return 0;
    for (lane = 0; lane < 9u; lane++) if (!valid4_fx(&r->fx[lane], lane)) return 0;
    for (k = 0; k < DRUMREC3_USER_KITS; k++) for (lane = 0; lane < 9u; lane++) {
        const drum_fx_config_t *f = &r->user_fx[k][lane];
        if (!valid4_fx(f, lane)) return 0;
        if (!r->user_valid[k] && (f->type || f->p[0] || f->p[1] || f->p[2] || f->p[3])) return 0;
    }
    return 1;
}
static int valid4(const drumrec3_t *r){return valid4_limit(r,55u);}
static size_t drumrec4_encode_limit(void *dst,size_t capacity,const drumrec3_t *r,uint32_t sample_limit)
{
    uint8_t *b = (uint8_t *)dst; size_t o = DRUMREC3_SIZE; uint32_t k, lane, p;
    if (!b || capacity < DRUMREC4_SIZE || !valid4_limit(r,sample_limit)) return 0;
    /* Reuse the exact DRM3 payload encoder; DRM4 has its own header and CRC. */
    if (drumrec3_encode_limit(dst, capacity, r,sample_limit) != DRUMREC3_SIZE) return 0;
    b[o++] = r->map_version;
    for (k = 0; k < 1u + DRUMREC3_USER_KITS; k++) for (lane = 0; lane < 9u; lane++) {
        const drum_fx_config_t *f = k ? &r->user_fx[k-1u][lane] : &r->fx[lane];
        b[o++] = f->type;
        for (p = 0; p < 4u; p++) b[o++] = f->p[p];
    }
    put32(b, DRUMREC4_MAGIC); put16(b + 4, DRUMREC4_VERSION); put32(b + 8, DRUMREC4_PAYLOAD_SIZE);
    put32(b + 12, crc32(b + DRUMREC_HEADER_SIZE, DRUMREC4_PAYLOAD_SIZE));
    return DRUMREC4_SIZE;
}
size_t drumrec4_encode(void *dst,size_t capacity,const drumrec3_t *r){return drumrec4_encode_limit(dst,capacity,r,55u);}
int drumrec4_decode(drumrec3_t *r, const void *src, size_t n)
{
    const uint8_t *b = (const uint8_t *)src; drumrec3_t decoded;
    size_t o = DRUMREC3_SIZE; uint32_t k, lane, p;
    if (!r || !b || n != DRUMREC4_SIZE || get32(b) != DRUMREC4_MAGIC || get16(b + 4) != DRUMREC4_VERSION ||
        get16(b + 6) != DRUMREC_HEADER_SIZE || get32(b + 8) != DRUMREC4_PAYLOAD_SIZE ||
        get32(b + 12) != crc32(b + DRUMREC_HEADER_SIZE, DRUMREC4_PAYLOAD_SIZE) || !decode3_body(&decoded, b)) return 0;
    decoded.map_version = b[o++];
    for (k = 0; k < 1u + DRUMREC3_USER_KITS; k++) for (lane = 0; lane < 9u; lane++) {
        drum_fx_config_t *f = k ? &decoded.user_fx[k-1u][lane] : &decoded.fx[lane];
        f->type = b[o++];
        for (p = 0; p < 4u; p++) f->p[p] = b[o++];
    }
    if (!valid4(&decoded)) return 0;
    *r = decoded;
    return 1;
}

/* START is a source position, not a compressed-byte offset. Runtime decoding
 * restores IMA predictor/index independently; persistence only stores percent. */
static int valid5_limit(const drumrec3_t *r,uint32_t sample_limit)
{
    uint32_t lane, k;
    if (!valid4_limit(r,sample_limit)) return 0;
    for (lane = 0; lane < DRUMREC_PADS; lane++) {
        if (r->starts[lane] >= r->base.base.pads[lane].end) return 0;
        for (k = 0; k < DRUMREC3_USER_KITS; k++) {
            uint8_t start = r->user_starts[k][lane];
            if (r->user_valid[k] ? start >= r->user_kits[k][lane].end : start != 0u) return 0;
        }
    }
    return 1;
}
static int valid5(const drumrec3_t *r){return valid5_limit(r,55u);}
static size_t drumrec5_encode_limit(void *dst,size_t capacity,const drumrec3_t *r,uint32_t sample_limit)
{
    uint8_t *b = (uint8_t *)dst;
    size_t o = DRUMREC4_SIZE;
    if (!b || capacity < DRUMREC5_SIZE || !valid5_limit(r,sample_limit) ||
        drumrec4_encode_limit(dst, capacity, r,sample_limit) != DRUMREC4_SIZE) return 0;
    memcpy(b + o, r->starts, DRUMREC_PADS); o += DRUMREC_PADS;
    memcpy(b + o, r->user_starts, sizeof r->user_starts);
    put32(b, DRUMREC5_MAGIC); put16(b + 4, DRUMREC5_VERSION); put32(b + 8, DRUMREC5_PAYLOAD_SIZE);
    put32(b + 12, crc32(b + DRUMREC_HEADER_SIZE, DRUMREC5_PAYLOAD_SIZE));
    return DRUMREC5_SIZE;
}
size_t drumrec5_encode(void *dst,size_t capacity,const drumrec3_t *r){return drumrec5_encode_limit(dst,capacity,r,55u);}
int drumrec5_decode(drumrec3_t *r, const void *src, size_t n)
{
    const uint8_t *b = (const uint8_t *)src;
    drumrec3_t decoded;
    size_t o = DRUMREC3_SIZE;
    uint32_t k, lane, p;
    if (!r || !b || n != DRUMREC5_SIZE || get32(b) != DRUMREC5_MAGIC ||
        get16(b + 4) != DRUMREC5_VERSION || get16(b + 6) != DRUMREC_HEADER_SIZE ||
        get32(b + 8) != DRUMREC5_PAYLOAD_SIZE ||
        get32(b + 12) != crc32(b + DRUMREC_HEADER_SIZE, DRUMREC5_PAYLOAD_SIZE) ||
        !decode3_body(&decoded, b)) return 0;
    decoded.map_version = b[o++];
    for (k = 0; k < 1u + DRUMREC3_USER_KITS; k++) for (lane = 0; lane < 9u; lane++) {
        drum_fx_config_t *f = k ? &decoded.user_fx[k-1u][lane] : &decoded.fx[lane];
        f->type = b[o++];
        for (p = 0; p < 4u; p++) f->p[p] = b[o++];
    }
    memcpy(decoded.starts, b + o, sizeof decoded.starts); o += sizeof decoded.starts;
    memcpy(decoded.user_starts, b + o, sizeof decoded.user_starts);
    if (!valid5(&decoded)) return 0;
    *r = decoded;
    return 1;
}

#define DRUMREC6_MAGIC 0x364d5244u
static int valid6(const drumrec3_t *r)
{
    uint32_t k,lane;if(!valid5_limit(r,71u))return 0;
    for(lane=0;lane<8;lane++){
        if(r->sample_ids[lane]<55u?r->sample_refs[lane]!=0u:r->sample_refs[lane]==0u)return 0;
        for(k=0;k<4;k++){
            uint32_t ref=r->user_sample_refs[k][lane];
            if(!r->user_valid[k]){if(ref)return 0;}
            else if(r->user_kits[k][lane].sample_id<55u?ref!=0u:ref==0u)return 0;
        }
    }
    return 1;
}
size_t drumrec6_encode(void *dst,size_t capacity,const drumrec3_t *r)
{
    uint8_t *b=(uint8_t *)dst;size_t o=DRUMREC5_SIZE;uint32_t k,lane;
    if(!b||capacity<DRUMREC6_SIZE||!valid6(r)||drumrec5_encode_limit(dst,capacity,r,71u)!=DRUMREC5_SIZE)return 0;
    for(lane=0;lane<8;lane++){put32(b+o,r->sample_refs[lane]);o+=4;}
    for(k=0;k<4;k++)for(lane=0;lane<8;lane++){put32(b+o,r->user_sample_refs[k][lane]);o+=4;}
    put32(b,DRUMREC6_MAGIC);put16(b+4,DRUMREC6_VERSION);put32(b+8,DRUMREC6_PAYLOAD_SIZE);
    put32(b+12,crc32(b+DRUMREC_HEADER_SIZE,DRUMREC6_PAYLOAD_SIZE));return DRUMREC6_SIZE;
}
int drumrec6_decode(drumrec3_t *r,const void *src,size_t n)
{
    const uint8_t *b=(const uint8_t *)src;drumrec3_t d;size_t o=DRUMREC3_SIZE;uint32_t k,lane,p;
    if(!r||!b||n!=DRUMREC6_SIZE||get32(b)!=DRUMREC6_MAGIC||get16(b+4)!=6u||get16(b+6)!=16u||
       get32(b+8)!=DRUMREC6_PAYLOAD_SIZE||get32(b+12)!=crc32(b+16,DRUMREC6_PAYLOAD_SIZE)||!decode3_body_limit(&d,b,71u))return 0;
    d.map_version=b[o++];
    for(k=0;k<5;k++)for(lane=0;lane<9;lane++){
        drum_fx_config_t *f=k?&d.user_fx[k-1][lane]:&d.fx[lane];f->type=b[o++];for(p=0;p<4;p++)f->p[p]=b[o++];
    }
    memcpy(d.starts,b+o,sizeof d.starts);o+=sizeof d.starts;memcpy(d.user_starts,b+o,sizeof d.user_starts);o+=sizeof d.user_starts;
    for(lane=0;lane<8;lane++){d.sample_refs[lane]=get32(b+o);o+=4;}
    for(k=0;k<4;k++)for(lane=0;lane<8;lane++){d.user_sample_refs[k][lane]=get32(b+o);o+=4;}
    if(!valid6(&d))return 0;*r=d;return 1;
}
