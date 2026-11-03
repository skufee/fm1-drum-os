#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline rollback-tool contract tests; never imports MIDI or opens USB."""
from pathlib import Path
import importlib.util,sys,tempfile,struct,zlib,json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
spec=importlib.util.spec_from_file_location('rollback_tool',Path(__file__).resolve().parents[1]/'tools/storage_rollback.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
assert m.geometry(0,7,0)==(0x9f000,4096) and m.geometry(0,7,1)==(0xfe000,4096)
for obj in range(1,5):
 for copy in (0,1):assert m.geometry(0,obj,copy)==(0x97000+(obj-1)*8192+copy*4096,4096)
for bank,base in enumerate(m.BANKS):assert m.geometry(1,bank,255)==(base,81920)
for a in [(0,0,0),(0,5,0),(0,7,2),(1,4,255),(1,1,0),(2,0,0)]:
 try:m.geometry(*a);raise RuntimeError('unexpected allowed range')
 except ValueError:pass
for op,p in [(30,bytes([8])),(30,bytes([9])),(30,bytes([10])),(16,bytes([255])),(26,bytes([1])),(24,bytes([0]))]:
 try:m.Link(False).request(op,p);raise RuntimeError('write bypass')
 except AssertionError:pass
class Fake:
 original=b'';current=b'';kind=0;target=7;copy=0;commands=[];opens=0
 def __init__(self,write):self.write=write;self.log=[];self.last_status=None;self.firmware='FM1 DRUM OS TEST';self.staged=None
 def __enter__(self):Fake.opens+=1;return self
 def __exit__(self,*a):pass
 def backup(self,k,t,out):assert (k,t)==(self.kind,self.target);out.write_bytes(self.current);return self.current
 def request(self,op,p):
  self.log.append([op,p[0]]);Fake.commands.append([self.write,p[0]])
  if op==27 and p==bytes([2]):return b''
  assert op==30
  if p[0]==6:return b''
  if p[0]==7:
   assert p[1:4]==bytes([self.kind,self.target,self.copy]) and p[8:]==self.original[:256]
   b=bytearray(48);b[:4]=bytes([1,self.kind+2,self.target,self.copy]);base,n=m.geometry(self.kind,self.target,self.copy);struct.pack_into('<IIIII',b,4,base,n,zlib.crc32(self.current),17,zlib.crc32(self.original));return b
  assert self.write
  if p[0]==8:self.staged=bytearray(self.original[:256]);return b''
  if p[0]==9:
   off=struct.unpack_from('<I',p,5)[0];assert off==len(self.staged) and len(p)==265;self.staged+=p[9:];return struct.pack('<I',len(self.staged))
  if p[0]==10:
   assert bytes(self.staged)==self.original
   if self.kind:self.current=self.original
   else:self.current=self.current[:self.copy*4096]+self.original+self.current[(self.copy+1)*4096:]
   b=bytearray(20);b[:4]=bytes([1,self.kind+2,self.target,self.copy]);struct.pack_into('<I',b,8,zlib.crc32(self.original));return b
  raise AssertionError('unexpected operation')
m.Link=Fake
with tempfile.TemporaryDirectory() as tmp:
 root=Path(tmp)
 for kind,target,copy in [(0,7,0),(1,3,255)]:
  Fake.kind,Fake.target,Fake.copy=kind,target,copy;n=81920 if kind else 4096;Fake.original=bytes((i*11)&255 for i in range(n));Fake.current=b'\xff'*(n if kind else 8192);source=root/f'source-{kind}.bin';source.write_bytes(Fake.original)
  plan_dir=root/f'prepare-{kind}';sys.argv=['tool','--prepare','--kind','bank' if kind else 'journal','--target',str(target),'--copy',str(copy),'--source',str(source),'--out',str(plan_dir)];m.main();assert all(not w for w,_ in Fake.commands)
  planfile=plan_dir/'rollback-plan.json';plan=json.loads(planfile.read_text());assert json.loads((plan_dir/'rollback-session.json').read_text())['physical_writes'] is False
  opens=Fake.opens;sys.argv=['tool','--execute',str(planfile),'--out',str(root/f'refuse-{kind}'),'--confirm','WRONG']
  try:m.main();raise RuntimeError('confirmation bypass')
  except AssertionError:assert Fake.opens==opens
  result_dir=root/f'execute-{kind}';sys.argv=['tool','--execute',str(planfile),'--out',str(result_dir),'--confirm',plan['required_confirmation']];m.main();r=json.loads((result_dir/'rollback-session.json').read_text());assert r['physical_writes'] and r['status']=='ONE_TARGET_RAW_RESTORE_VERIFIED';Fake.commands=[]
print('PASS offline rollback tool: fixed ranges, no read-only write bypass, prepare/cancel, exact confirmation before USB, sequential journal/bank CRC restore and preserved survivor; MOCK ONLY')
