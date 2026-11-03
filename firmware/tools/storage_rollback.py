#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One fixed-region raw rollback, separate owner consent REQUIRED for --execute.

--prepare only downloads current bytes and stages RAM proof; it never erases.
--execute verifies that plan/source/current backup are unchanged, requires the
exact RESTORE phrase, then performs only that target. No automatic retry of a
write, no second target, firmware flash or runtime settings command.
Requires mido/python-rtmidi (optional --deps directory). Keep plans/backups private.
"""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time,zlib
from physical_nor_readonly import pack7,unpack7
BANKS=(0xa0000,0xb4000,0xc8000,0xe7000)
def sha(b):return hashlib.sha256(b).hexdigest()
def geometry(kind,target,copy):
    if kind==0 and target in (1,2,3,4,7) and copy in (0,1):return (0x9f000 if copy==0 else 0xfe000,4096) if target==7 else (0x97000+(target-1)*8192+copy*4096,4096)
    if kind==1 and target in range(4) and copy==255:return BANKS[target],81920
    raise ValueError('Not an approved fixed journal/sample-bank target')
def phrase(kind,target,copy,base,n,source_hash):
    name=f'BANK {target+1}' if kind else ('AUTOSAVE' if target==7 else f'PROJECT {target}')+(' A' if copy==0 else ' B')
    return f'RESTORE {name} 0x{base:X} {n} {source_hash}'
class Link:
    def __init__(self,write):self.write=write;self.id=0;self.log=[];self.last_status=None
    def __enter__(self):
        import mido
        self.mido=mido;match=lambda xs:[s for s in xs if any(k in s.lower() for k in ('fm1','fm-1','felucca','sloop'))]
        i,o=match(mido.get_input_names()),match(mido.get_output_names())
        if len(i)!=1 or len(o)!=1:raise RuntimeError('Missing/ambiguous FM1 MIDI pair')
        self.inp=mido.open_input(i[0]);self.out=mido.open_output(o[0],autoreset=False)
        try:
            caps=self.request(1);assert len(caps)==64 and caps[56:64]==b'FM-1_900' and caps[14]&4,'Firmware lacks bounded raw rollback'
            self.firmware=caps[24:56].split(b'\0')[0].decode('ascii');assert self.firmware.startswith('FM1 DRUM OS ')
            state=self.request(2);assert len(state)==24 and not any(state[8:11]),'STOP/disarm required; tool never sends STOP'
            return self
        except Exception:
            self.inp.close();self.out.close();raise
    def __exit__(self,exc,*args):
        if exc:
            try:self.last_status=self.request(30,bytes([5])).hex()
            except Exception:pass
        self.inp.close();self.out.close()
    def request(self,op,p=b''):
        read=(op in (1,2,18) and not p or op==19 and len(p)==8 and p[0]==2 and p[1]<4 or
              op==27 and (len(p)==2 and p[0]==0 and p[1] in (1,2,3,4,7) or len(p)==5 and p[0]==1 or p==b'\2') or
              op==30 and p and p[0] in (5,6,7))
        write=op==30 and p and p[0] in (8,9,10)
        assert read or self.write and write,'Forbidden physical request'
        self.id=(self.id+1)&127;h=bytes([1,self.id,op,0]);f=b'\x7dFL\x50'+h+pack7(p+struct.pack('<I',zlib.crc32(h+p)))
        self.log.append({'op':op,'sub':p[0] if p else None,'bytes':len(p),'write':bool(write)})
        self.out.send(self.mido.Message('sysex',data=f));deadline=time.monotonic()+(30 if write else 4)
        while time.monotonic()<deadline:
            msg=self.inp.poll()
            if msg is None:time.sleep(.002);continue
            if msg.type!='sysex':continue
            b=bytes(msg.data)
            if len(b)<12 or b[:5]!=b'\x7dFL\x50\1' or b[5:7]!=bytes([self.id,op]):continue
            raw=unpack7(b[8:]);assert len(raw)>=4 and zlib.crc32(b[4:8]+raw[:-4])==struct.unpack('<I',raw[-4:])[0],'Reply CRC mismatch'
            if b[7]:raise RuntimeError(f'NACK{b[7]} op{op}; target may be partial. Read STATUS/back up before any new attempt.')
            return raw[:-4]
        raise TimeoutError('No write is automatically retried. Operation may have completed/partially completed; read STATUS first.')
    def backup(self,kind,target,out):
        if kind==0:
            report=self.request(27,bytes([0,target]));assert len(report)==50;raw=bytearray()
            for off in range(0,8192,256):
                b=self.request(27,b'\1'+struct.pack('<HH',off,256));assert len(b)==258 and struct.unpack_from('<H',b)[0]==off;raw+=b[2:]
            assert zlib.crc32(raw)==struct.unpack_from('<I',report)[0]
        else:
            raw=bytearray()
            for off in range(0,81920,256):
                b=self.request(19,bytes([2,target])+struct.pack('<IH',off,256));assert len(b)==268 and b[:2]==bytes([2,target]) and struct.unpack_from('<I',b,2)[0]==81920 and struct.unpack_from('<I',b,6)[0]==off;raw+=b[12:]
        out.write_bytes(raw);assert out.read_bytes()==raw;return bytes(raw)
def main():
    a=argparse.ArgumentParser(description=__doc__);mode=a.add_mutually_exclusive_group(required=True);mode.add_argument('--prepare',action='store_true');mode.add_argument('--execute',type=Path)
    a.add_argument('--source',type=Path);a.add_argument('--kind',choices=['journal','bank']);a.add_argument('--target',type=int);a.add_argument('--copy',type=int,default=255);a.add_argument('--out',type=Path,required=True);a.add_argument('--deps',type=Path);a.add_argument('--confirm');x=a.parse_args()
    if x.deps:sys.path.insert(0,str(x.deps.resolve()))
    out=x.out.resolve();assert not out.exists() or not any(out.iterdir()),'Use a fresh output directory; never overwrite a backup';out.mkdir(parents=True,exist_ok=True);log={'status':'INCOMPLETE','physical_writes':False}
    if x.execute:
        plan=json.loads(x.execute.read_text(encoding='utf-8'));assert plan['format']=='FM1-RAW-ROLLBACK-PLAN' and plan['version']==1
        kind,target,copy=plan['kind'],plan['target'],plan['copy'];source_path=Path(plan['source_file']);original=source_path.read_bytes()
        assert sha(original)==plan['source_sha256'] and zlib.crc32(original)==plan['source_crc32'],'Archived source changed'
    else:
        assert x.source and x.kind and x.target is not None,'--source/--kind/--target required';kind=0 if x.kind=='journal' else 1;target=x.target;copy=x.copy;source_path=x.source.resolve();original=source_path.read_bytes();plan=None
    base,n=geometry(kind,target,copy);assert len(original)==n
    expected_phrase=phrase(kind,target,copy,base,n,sha(original))
    if plan:assert plan['base']==base and plan['length']==n and x.confirm==expected_phrase,'Exact reviewed RESTORE phrase required'
    link=None;started=False
    try:
        link=Link(bool(plan))
        with link:
            current=link.backup(kind,target,out/'before-current-full-backup.bin');current_crc=zlib.crc32(current)
            if plan:assert current_crc==plan['snapshot_crc32'] and sha(current)==plan['snapshot_sha256'],'Current bytes differ from approved plan; prepare/review a new plan'
            prep=bytes([7,kind,target,copy])+struct.pack('<I',zlib.crc32(original))+original[:256]
            p=link.request(30,prep);assert len(p)==48 and p[:4]==bytes([1,kind+2,target,copy])
            assert struct.unpack_from('<III',p,4)[:2]==(base,n) and struct.unpack_from('<I',p,12)[0]==current_crc
            token=struct.unpack_from('<I',p,16)[0];assert token and struct.unpack_from('<I',p,20)[0]==zlib.crc32(original)
            if not plan:
                plan={'format':'FM1-RAW-ROLLBACK-PLAN','version':1,'firmware':link.firmware,'kind':kind,'target':target,'copy':copy,'base':base,'length':n,'source_file':str(source_path),'source_sha256':sha(original),'source_crc32':zlib.crc32(original),'snapshot_crc32':current_crc,'snapshot_sha256':sha(current),'required_confirmation':expected_phrase,'active_state_consequence':'Original bytes restored; raw AUTOSAVE rollback suspends autosave until cold reboot; no semantic migration'}
                (out/'rollback-plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8');link.request(30,b'\6');print('READ-ONLY plan prepared. Separate explicit owner approval required:\n'+expected_phrase);log['status']='PLAN_ONLY_NO_WRITES';return
            started=True;log['physical_writes']=True;link.request(30,bytes([8])+struct.pack('<III',token,current_crc,zlib.crc32(original))+b'\x5a')
            for off in range(256,n,256):
                r=link.request(30,bytes([9])+struct.pack('<II',token,off)+original[off:off+256]);assert len(r)==4 and struct.unpack('<I',r)[0]==off+256
            r=link.request(30,bytes([10])+struct.pack('<II',token,zlib.crc32(original))+b'\x5a');assert len(r)==20 and r[:4]==bytes([1,kind+2,target,copy]) and struct.unpack_from('<I',r,8)[0]==zlib.crc32(original)
            actual=link.backup(kind,target,out/'after-restored-full-backup.bin');restored=actual[copy*4096:(copy+1)*4096] if kind==0 else actual;assert restored==original
            if kind==0:assert actual[(copy^1)*4096:((copy^1)+1)*4096]==current[(copy^1)*4096:((copy^1)+1)*4096];link.request(27,b'\2')
            log['status']='ONE_TARGET_RAW_RESTORE_VERIFIED';log['after_sha256']=sha(restored);print('One target restored byte-for-byte. No next target follows. For AUTOSAVE perform owner-controlled cold reboot; autosave remains held meanwhile.')
    except Exception as e:
        log['error']=repr(e)
        if link:log['firmware_status_hex']=link.last_status
        raise
    finally:
        log['started']=started;log['commands']=link.log if link else [];(out/'rollback-session.json').write_text(json.dumps(log,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
