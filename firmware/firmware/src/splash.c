/* SPDX-License-Identifier: GPL-3.0-only */
/* Original FM1 DRUM OS 2x2 DR/OS mark. Existing canvas/startup entry only. */
static void sloop_splash(void)
{
    static const uint8_t glyph[4][7]={
        {30,17,17,17,17,17,30}, {30,17,17,30,20,18,17},
        {14,17,17,17,17,17,14}, {15,16,16,14,1,1,30}
    };
    uint32_t band,k,row,col;
    const uint16_t chassis=RGB(176,177,171),orange=RGB(248,121,5),ink=RGB(57,18,16);
    for(band=0;band<240u;band+=120u) {
        cv_begin(240,120,chassis);
        for(k=0;k<4u;k++) {
            int32_t x=44+(int32_t)(k&1u)*80,y=16+(int32_t)(k/2u)*80-(int32_t)band;
            cv_rect(x,y,72,72,ink);cv_rect(x+2,y+2,68,68,orange);
            for(row=0;row<7;row++)for(col=0;col<5;col++)
                if(glyph[k][row]&(16u>>col))cv_rect(x+21+(int32_t)col*6,y+15+(int32_t)row*6,6,6,ink);
        }
        cv_text((240-text_w(&FONT_S,"FM1 DRUM OS"))/2,184-(int32_t)band,&FONT_S,"FM1 DRUM OS",RGB(0,0,0));
        cv_rect(76,207-(int32_t)band,88,2,RGB(49,149,180));
        cv_text((240-text_w(&FONT_S,"DRUM SAMPLER"))/2,217-(int32_t)band,&FONT_S,"DRUM SAMPLER",ink);
        cv_blit(0,band);
    }
}
