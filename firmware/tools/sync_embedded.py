#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the 55-zone firmware bank, LCD waveform and browser preview identical.
No WAV is copied. Audio redistribution permission is recorded in notices.
"""
from pathlib import Path
import argparse,hashlib,json,re
import sampleio as sio

ROOT=Path(__file__).resolve().parents[1]
def bank(path):
    text=path.read_text(encoding='utf-8')
    data=bytes(map(int,re.search(r'SMP_DATA\[\d+\] = \{(.*?)\};',text,re.S)[1].replace('\n','').replace(' ','').strip(',').split(',')))
    zones=[list(map(int,x.split(','))) for x in re.findall(r'\{([^{}]+)\}',re.search(r'SMP_ZONES\[\] = \{(.*?)\};',text,re.S)[1])]
    names=re.findall(r'"([A-Z]+\d+)"',re.search(r'SMP_CUSTOM_SAMPLE_NAMES\[.*?\] = \{(.*?)\};',text,re.S)[1])
    expected=[f'{cat}{i}' for cat,n in [('HAT',14),('KICK',12),('OH',3),('PERC',10),('SNARE',16)] for i in range(1,n+1)]
    if len(zones)!=55 or names!=expected:raise ValueError('55-zone identity/order mismatch')
    for z in zones:
        if z[0]+(z[1]+1)//2>len(data) or z[4]!=32768:raise ValueError('Sample bounds/rate mismatch')
    return data,zones,names

def decode(data,frames):
    pred=index=0;out=[]
    for i in range(frames):
        c=(data[i//2]>>(4*(i&1)))&15;step=sio.IMA_STEP[index]
        delta=(step>>3)+(step if c&4 else 0)+(step>>1 if c&2 else 0)+(step>>2 if c&1 else 0)
        pred=max(-32768,min(32767,pred+(-delta if c&8 else delta)))
        index=max(0,min(88,index+sio.IMA_IDX[c&7]));out.append(pred)
    return out

def waveform(data,zones):
    rows=[]
    for z in zones:
        pcm=decode(data[z[0]:],z[1]);row=[]
        for i in range(96):
            a=i*len(pcm)//96;b=max(a+1,(i+1)*len(pcm)//96);v=pcm[a:b]
            row.extend([min(v)>>8,max(v)>>8])
        rows.append(row)
    return rows

def native_text(rows):
    return '/* SPDX-License-Identifier: GPL-3.0-only */\n/* Generated from the actual embedded ADPCM by tools/sync_embedded.py. Audio: see notices. */\n#pragma once\nstatic const int8_t DRUM_WAVEFORM[55][192] = {\n'+''.join(' {'+','.join(map(str,r))+'},\n' for r in rows)+'};\n'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--header',type=Path,default=ROOT/'build/gen/felucca_samples.h')
    ap.add_argument('--editor',type=Path);ap.add_argument('--update',action='store_true');ap.add_argument('--distributable',action='store_true')
    a=ap.parse_args();data,zones,names=bank(a.header);rows=waveform(data,zones)
    native=ROOT/'firmware/src/drum_waveforms.h';text=native_text(rows)
    if a.update:native.write_text(text,encoding='utf-8')
    elif native.read_text(encoding='utf-8')!=text:raise SystemExit('LCD waveform bank differs; run sync_embedded.py --update')
    metadata=[dict(id=i,name=n,category=re.match('[A-Z]+',n)[0],frames=z[1],sampleRate=22050,duration=z[1]/22050) for i,(n,z) in enumerate(zip(names,zones))]
    zm=[dict(offset=z[0],frames=z[1],rate=22050) for z in zones]
    info=dict(format='FM1-EMBEDDED-55',sample_count=55,bytes=len(data),adpcm_sha256=hashlib.sha256(data).hexdigest(),header_sha256=hashlib.sha256(a.header.read_bytes()).hexdigest(),samples=metadata)
    if a.editor:
        editor=a.editor.resolve();public=editor/'public';meta=editor/'src/metadata.ts'
        ztext='// SPDX-License-Identifier: GPL-3.0-only\n// Generated from the exact firmware bank; audio permission in PROVENANCE.md.\nexport const ZONES = '+json.dumps(zm)+' as const;\n'
        if a.update:
            (public/'embedded-adpcm.bin').write_bytes(data)
            (editor/'src/zones.ts').write_text(ztext,encoding='utf-8')
            old=meta.read_text(encoding='utf-8')
            if 'export const FACTORY =' not in old or 'export const EMBEDDED =' not in old:raise ValueError('Missing editor metadata markers')
            old=old.split('export const EMBEDDED =',1)[0]+'export const EMBEDDED = '+json.dumps(metadata,indent=2)+' as const;\n'+old[old.index('export const FACTORY ='):]
            meta.write_text(old,encoding='utf-8')
            (public/'embedded-audio.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
        else:
            if (public/'embedded-adpcm.bin').read_bytes()!=data:raise ValueError('Browser ADPCM differs')
            parsed=json.loads(re.search(r'export const EMBEDDED = (\[.*?\])(?: as const)?;',meta.read_text(encoding='utf-8'),re.S)[1])
            if parsed!=metadata:raise ValueError('Browser sample metadata differs')
            parsed=json.loads(re.search(r'export const ZONES = (\[.*?\]) as const;', (editor/'src/zones.ts').read_text(encoding='utf-8'),re.S)[1])
            if parsed!=zm:raise ValueError('Browser zone metadata differs')
    if a.distributable:
        if not a.update:raise ValueError('--distributable requires explicit --update after rights review')
        dst=ROOT/'assets/embedded';dst.mkdir(parents=True,exist_ok=True)
        (dst/'felucca_samples.h').write_bytes(a.header.read_bytes())
        (dst/'library.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf-8')
    print('PASS embedded identity:55 zones, LCD waveform, ADPCM '+hashlib.sha256(data).hexdigest())

if __name__=='__main__':main()
