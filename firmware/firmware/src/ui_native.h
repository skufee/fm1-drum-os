/* SPDX-License-Identifier: GPL-3.0-only */
/* RC1 native LCD typography and waveform drawing. No framebuffer/capture storage.
 * Compact glyphs are original 5x7 bitmap artwork, expanded vertically to 10 px;
 * no stroke is discarded, unlike the old downsampled 8x16 font. */
#ifndef FM1_UI_NATIVE_H
#define FM1_UI_NATIVE_H
static const uint8_t RC1_FONT[64][7] = {
 ['!'-32]={4,4,4,4,4,0,4}, ['"'-32]={10,10,10,0,0,0,0},
 ['#'-32]={10,31,10,10,31,10,0}, ['$'-32]={4,15,20,14,5,30,4},
 ['%'-32]={25,25,2,4,8,19,19}, ['&'-32]={12,18,20,8,21,18,13},
 ['\''-32]={4,4,8,0,0,0,0}, ['('-32]={2,4,8,8,8,4,2},
 [')'-32]={8,4,2,2,2,4,8}, ['*'-32]={0,21,14,31,14,21,0},
 ['+'-32]={0,4,4,31,4,4,0}, [','-32]={0,0,0,0,0,4,8},
 ['-'-32]={0,0,0,31,0,0,0}, ['.'-32]={0,0,0,0,0,0,4},
 ['/'-32]={1,2,2,4,8,8,16}, ['0'-32]={14,17,19,21,25,17,14},
 ['1'-32]={4,12,4,4,4,4,14}, ['2'-32]={14,17,1,2,4,8,31},
 ['3'-32]={30,1,1,14,1,1,30}, ['4'-32]={2,6,10,18,31,2,2},
 ['5'-32]={31,16,16,30,1,1,30}, ['6'-32]={14,16,16,30,17,17,14},
 ['7'-32]={31,1,2,4,8,8,8}, ['8'-32]={14,17,17,14,17,17,14},
 ['9'-32]={14,17,17,15,1,1,14}, [':'-32]={0,4,0,0,4,0,0},
 [';'-32]={0,4,0,0,4,4,8}, ['<'-32]={1,2,4,8,4,2,1},
 ['='-32]={0,0,31,0,31,0,0}, ['>'-32]={16,8,4,2,4,8,16},
 ['?'-32]={14,17,1,2,4,0,4}, ['@'-32]={14,17,23,21,23,16,14},
 ['A'-32]={14,17,17,31,17,17,17}, ['B'-32]={30,17,17,30,17,17,30},
 ['C'-32]={14,17,16,16,16,17,14}, ['D'-32]={30,17,17,17,17,17,30},
 ['E'-32]={31,16,16,30,16,16,31}, ['F'-32]={31,16,16,30,16,16,16},
 ['G'-32]={14,17,16,23,17,17,15}, ['H'-32]={17,17,17,31,17,17,17},
 ['I'-32]={14,4,4,4,4,4,14}, ['J'-32]={7,2,2,2,2,18,12},
 ['K'-32]={17,18,20,24,20,18,17}, ['L'-32]={16,16,16,16,16,16,31},
 ['M'-32]={17,27,21,21,17,17,17}, ['N'-32]={17,25,25,21,19,19,17},
 ['O'-32]={14,17,17,17,17,17,14}, ['P'-32]={30,17,17,30,16,16,16},
 ['Q'-32]={14,17,17,17,21,18,13}, ['R'-32]={30,17,17,30,20,18,17},
 ['S'-32]={15,16,16,14,1,1,30}, ['T'-32]={31,4,4,4,4,4,4},
 ['U'-32]={17,17,17,17,17,17,14}, ['V'-32]={17,17,17,17,17,10,4},
 ['W'-32]={17,17,17,21,21,21,10}, ['X'-32]={17,17,10,4,10,17,17},
 ['Y'-32]={17,17,10,4,4,4,4}, ['Z'-32]={31,1,2,4,8,16,31},
 ['['-32]={14,8,8,8,8,8,14}, ['\\'-32]={16,8,8,4,2,2,1},
 [']'-32]={14,2,2,2,2,2,14}, ['^'-32]={4,10,17,0,0,0,0},
 ['_'-32]={0,0,0,0,0,0,31}
};
static uint32_t rc1_char(uint32_t c)
{ if(c>='a'&&c<='z')c-=32u; return c>=32u&&c<=95u?c:'?'; }
/* RC2 sizes: compact6x10 only for hints/dense labels; normal8x14;
 * primary12x14 uses exact2x2 bitmap pixels for headers and numeric values. */
static uint32_t rc2_gw(int size) { return size==1?5u:size==2?10u:7u; }
static uint32_t rc2_gh(int size) { return size==1?10u:14u; }
static uint32_t rc2_adv(int size) { return size==1?6u:size==2?12u:8u; }
static int rc1_bit(uint32_t c,uint32_t x,uint32_t y,int size)
{
    uint32_t sx=x*5u/rc2_gw(size),sy=y*7u/rc2_gh(size);
    return (RC1_FONT[rc1_char(c)-32u][sy]>>(4u-sx))&1u;
}
/* Actual lit-pixel extents, including the native glyph side bearings. */
static int32_t rc1_ink(const char *text,int small,int32_t *left)
{
    int32_t pen=0,lo=0x7fffffff,hi=-0x7fffffff; uint32_t x,y;
    const char *s=text?text:"--";
    for(;*s;s++) {
        uint32_t w=rc2_gw(small),h=rc2_gh(small);
        for(y=0;y<h;y++)for(x=0;x<w;x++)if(rc1_bit((uint8_t)*s,x,y,small)) {
            int32_t at=pen+(int32_t)x;
            if(at<lo)lo=at;if(at>hi)hi=at;
        }
        pen+=(int32_t)rc2_adv(small);
    }
    if(hi<lo){if(left)*left=0;return 0;}
    if(left)*left=lo;return hi-lo+1;
}
static void rc1_fit(char *out,uint32_t cap,const char *source,int32_t width,int small)
{
    uint32_t n,truncated=0; const char *s=source?source:"--";
    if(!cap)return;
    str_cpy(out,s,cap);n=str_len(out);truncated=str_len(s)>=cap;
    while(n&&rc1_ink(out,small,0)>width){out[--n]=0;truncated=1;}
    if(truncated) {
        while(n&&n+4u>cap)out[--n]=0;
        if(cap<4u){out[0]=0;return;}
        for(;;){out[n]='.';out[n+1]='.';out[n+2]='.';out[n+3]=0;
            if(rc1_ink(out,small,0)<=width||!n)break;out[--n]=0;}
        if(rc1_ink(out,small,0)>width)out[0]=0;
    }
}
/* align: 0 left, 1 centre, 2 right. Ink, rather than the advance cell, fits box. */
static void rc1_text_box(int32_t x,int32_t y,int32_t width,const char *text,uint16_t color,int small,int align)
{
    char fitted[64];int32_t left,w;uint32_t gx,gy;const char *s;
    rc1_fit(fitted,sizeof fitted,text,width,small);w=rc1_ink(fitted,small,&left);
    if(align==1)x+=(width-w)/2;else if(align==2)x+=width-w;
    x-=left;
    for(s=fitted;*s;s++,x+=(int32_t)rc2_adv(small))
        for(gy=0;gy<rc2_gh(small);gy++)for(gx=0;gx<rc2_gw(small);gx++)
            if(rc1_bit((uint8_t)*s,gx,gy,small))cv_pset(x+(int32_t)gx,y+(int32_t)gy,color);
}
/* Existing zero-crossing/auto-scaled oscilloscope line, for any UI rectangle.
 * Caller supplies the existing vis_l snapshot; this primitive captures nothing. */
static void rc1_wave_plot(const int16_t *samples,uint32_t count,int32_t x,int32_t y,int32_t w,int32_t h,int32_t scale)
{
    uint32_t start=0,i;int32_t oldy=0,cx,cy,dx=w/4,mid=y+h/2;
    if(!samples||w<2||h<3||(uint32_t)w>count)return;
    if(scale<1600)scale=1600;if(dx<1)dx=1;
    for(cx=x+dx;cx<x+w;cx+=dx)for(cy=y;cy<y+h;cy+=4)cv_pset(cx,cy,RC_GRID);
    cv_rect(x,mid,w,1,RC_GRID);
    for(i=1;i+(uint32_t)w<count;i++)if(samples[i-1u]<0&&samples[i]>=0){start=i;break;}
    for(i=0;i<(uint32_t)w;i++) {
        cy=clamp(mid-(int32_t)samples[start+i]*(h/2-2)/scale,y+1,y+h-2);
        if(i){cv_line(x+(int32_t)i-1,oldy,x+(int32_t)i,cy,RC_INK);cv_line(x+(int32_t)i-1,oldy+1,x+(int32_t)i,cy+1,RC_INK);}
        oldy=cy;
    }
}
static void rc1_fx_wave_draw(int32_t x,int32_t y,int32_t w,int32_t h);
#endif
