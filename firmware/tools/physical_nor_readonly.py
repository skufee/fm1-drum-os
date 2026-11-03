#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owner-authorized read-only USB diagnostics using EXISTING Drum protocol.

Requires mido/python-rtmidi. Explicitly no MIDI reset/autoreset, runtime setters,
SAVE, authorization, recovery/erase/upload/restore/OTA. Op27 sub0/1 only creates
an in-RAM backup lock and reads8KiB; sub2 cancels that RAM lock. No op28/token use.
Use only after explicit owner permission for physical read-only diagnostics.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time,zlib

def pack7(raw):
    out=bytearray()
    for off in range(0,len(raw),7):
        b=raw[off:off+7];out.append(sum((v>>7)<<i for i,v in enumerate(b)));out.extend(v&127 for v in b)
    return bytes(out)

def unpack7(raw):
    out=bytearray()
    for off in range(0,len(raw),8):
        part=raw[off:off+8]
        if len(part)<2 or part[0]>>(len(part)-1):raise ValueError('Malformed7bit reply')
        out.extend(v|((part[0]>>i)&1)<<7 for i,v in enumerate(part[1:]))
    return bytes(out)

def allowed(op,p):
    return (op in (1,2,18) and not p or
            op==19 and len(p)==8 and (p[0]==2 and p[1]<4 or p[0]==0 and p[1]==0) or
            op==27 and (len(p)==2 and p[0]==0 and p[1] in (1,2,3,4,7) or p==b'\x02' or len(p)==5 and p[0]==1))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--deps',type=Path);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--projects',action='store_true');ap.add_argument('--working-snapshot',action='store_true');ap.add_argument('--journal-only',action='store_true');ap.add_argument('--owner-authorized-read-only',action='store_true',required=True);a=ap.parse_args()
    if a.deps:sys.path.insert(0,str(a.deps.resolve()))
    import mido
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);ops=[];request_id=0;lock=False
    inputs=mido.get_input_names();outputs=mido.get_output_names()
    # Select only an unambiguous named FM1/SLOOP/Felucca endpoint, then verify CAPS.
    match=lambda names:[n for n in names if any(s in n.lower() for s in ('fm1','fm-1','felucca','sloop'))]
    ii,oo=match(inputs),match(outputs)
    if len(ii)!=1 or len(oo)!=1:raise RuntimeError('Ambiguous/missing physical ports: '+repr((inputs,outputs)))
    summary={'physical_USB':True,'ports':{'input':ii[0],'output':oo[0]},'writes_requested':False,'commands':ops,'status':'INCOMPLETE'}
    def save(): (out/'read-session.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    save()
    with mido.open_input(ii[0]) as inp,mido.open_output(oo[0],autoreset=False) as output:
        def request(op,p=b''):
            nonlocal request_id
            assert allowed(op,p),'FORBIDDEN PHYSICAL REQUEST'
            request_id=(request_id+1)&127;h=bytes([1,request_id,op,0]);packet=b'\x7dFL\x50'+h+pack7(p+struct.pack('<I',zlib.crc32(h+p)))
            item={'op':op,'request_hex':p.hex(),'response_bytes':None,'status':None};ops.append(item)
            for retry in range(2):
                output.send(mido.Message('sysex',data=packet));deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    m=inp.poll()
                    if m is None:time.sleep(.002);continue
                    if m.type!='sysex':continue
                    f=bytes(m.data)
                    if len(f)<12 or f[:5]!=b'\x7dFL\x50\x01' or f[5]!=request_id or f[6]!=op:continue
                    if any(v>127 for v in f):raise ValueError('Non7bit response')
                    r=unpack7(f[8:]);assert len(r)>=4 and zlib.crc32(f[4:8]+r[:-4])==struct.unpack('<I',r[-4:])[0],'Reply CRC'
                    item.update({'status':f[7],'response_bytes':len(r)-4,'response_crc32':hex(zlib.crc32(r[:-4]))})
                    if f[7]:save();raise RuntimeError(f'Physical NACK op{op}:code{f[7]}')
                    return r[:-4]
            save();raise TimeoutError(f'No physical response op{op}; no write attempted')
        try:
            caps=request(1);assert len(caps)==64 and caps[8]==55 and caps[9]==16 and caps[56:64]==b'FM-1_900'
            summary['firmware']=caps[24:56].split(b'\0')[0].decode('ascii');summary['caps_hex']=caps.hex();print('PHYSICAL CAPS',summary['firmware'],flush=True)
            assert summary['firmware'].startswith('FM1 DRUM OS '),'Unexpected device; stop'
            state=request(2);summary['state_hex']=state.hex();assert len(state)==24 and not any(state[8:11]),'Transport/REC/ARM active: stop diagnostics, never send STOP'
            diag=request(18);summary['save_diagnostics_hex']=diag.hex()
            # Existing full A/B backup. No recovery authorization is sent.
            meta=request(27,b'\0\7');lock=True;assert len(meta)==50 and struct.unpack_from('<H',meta,8)[0]==8192
            summary['journal_report_hex']=meta.hex();pair=bytearray()
            for off in range(0,8192,256):
                chunk=request(27,b'\1'+struct.pack('<HH',off,256));assert len(chunk)==258 and struct.unpack_from('<H',chunk)[0]==off;pair+=chunk[2:]
            assert zlib.crc32(pair)==struct.unpack_from('<I',meta)[0],'Pair CRC mismatch'
            for name,raw in [('autosave-AB.bin',pair),('autosave-A.bin',pair[:4096]),('autosave-B.bin',pair[4096:])]:
                (out/name).write_bytes(raw)
            summary['autosave_pair']={'bytes':len(pair),'crc32':hex(zlib.crc32(pair)),'sha256':hashlib.sha256(pair).hexdigest()}
            request(27,b'\2');lock=False;print('PHYSICAL AUTOSAVE raw8192 CRC PASS',flush=True);save()
            banks=[]
            for bank in range(0 if a.journal_only else 4):
                data=bytearray()
                for off in range(0,81920,256):
                    b=request(19,bytes([2,bank])+struct.pack('<IH',off,256));assert len(b)==268 and b[:2]==bytes([2,bank]) and struct.unpack_from('<I',b,2)[0]==81920 and struct.unpack_from('<I',b,6)[0]==off and struct.unpack_from('<H',b,10)[0]==256
                    data+=b[12:]
                (out/f'sample-bank-{bank}.bin').write_bytes(data)
                banks.append({'bank':bank,'bytes':len(data),'crc32':hex(zlib.crc32(data)),'sha256':hashlib.sha256(data).hexdigest()});summary['sample_banks']=banks
                print('PHYSICAL BANK',bank,'read81920',flush=True);save()
            if a.projects:
                projects=[]
                for obj in range(1,5):
                    meta=request(27,bytes([0,obj]));lock=True;assert len(meta)==50
                    data=bytearray()
                    for off in range(0,8192,256):
                        part=request(27,b'\1'+struct.pack('<HH',off,256));assert len(part)==258 and struct.unpack_from('<H',part)[0]==off;data+=part[2:]
                    assert zlib.crc32(data)==struct.unpack_from('<I',meta)[0]
                    (out/f'project-{obj}-AB.bin').write_bytes(data)
                    projects.append({'object':obj,'bytes':8192,'crc32':hex(zlib.crc32(data)),'sha256':hashlib.sha256(data).hexdigest(),'report_hex':meta.hex()})
                    request(27,b'\2');lock=False;print('PHYSICAL PROJECT',obj,'full8192 CRC PASS',flush=True)
                summary['projects']=projects;save()
            if a.working_snapshot:
                data=bytearray()
                for off in range(0,1959,256):
                    n=min(256,1959-off);part=request(19,bytes([0,0])+struct.pack('<IH',off,n))
                    assert len(part)==12+n and part[:2]==bytes([0,0]) and struct.unpack_from('<I',part,2)[0]==1959 and struct.unpack_from('<I',part,6)[0]==off and struct.unpack_from('<H',part,10)[0]==n
                    data+=part[12:]
                assert data[:4]==b'DRM6' and struct.unpack_from('<I',data,12)[0]==zlib.crc32(data[16:])
                (out/'current-runtime.drm').write_bytes(data)
                summary['runtime_snapshot']={'bytes':len(data),'crc32':hex(zlib.crc32(data)),'sha256':hashlib.sha256(data).hexdigest()}
                print('PHYSICAL current runtime DRM6 captured without applying/writing',flush=True);save()
            # Read-only repeat of complete banks is optional to avoid overstating
            # filesystem hashes as a firmware CRC. Each chunk has firmware CRC;
            # compare all bytes with owner supplied CRC-checked archive afterwards.
            summary['save_after_hex']=request(18).hex();summary['status']='PHYSICAL_READ_ONLY_BACKUP_COMPLETE';save()
        except Exception as e:
            summary['error']=repr(e);save();raise
        finally:
            if lock:
                try:request(27,b'\2');summary['RAM_backup_lock_cancelled']=True
                except Exception as e:summary['RAM_backup_cancel_error']=repr(e)
                save()
    print('Physical read-only backup complete; no NOR/runtime write/flash/authorization command sent',flush=True)

if __name__=='__main__':main()
