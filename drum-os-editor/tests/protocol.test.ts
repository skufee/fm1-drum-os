import {Device} from '../src/device.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import {capsIdentity,crc32,pack7,unpack7,frame,parseFrame,concat,le32,verifyProjectWire,MidiTransport,OP,ProtocolError,type InputPort} from '../src/protocol.ts';
const delay=(ms:number)=>new Promise(r=>setTimeout(r,ms));
test('CRC32 matches zlib golden vector and protects header + payload',()=>{assert.equal(crc32(new TextEncoder().encode('123456789')),0xcbf43926);const payload=Uint8Array.of(0,128,255,17);const b=frame(37,OP.SET_PAD,payload);assert.deepEqual(parseFrame(b),{id:37,op:OP.SET_PAD,status:0,payload});const raw=unpack7(b.slice(9,-1));assert.equal(new DataView(raw.buffer).getUint32(raw.length-4,true),crc32(concat(Uint8Array.of(1,37,OP.SET_PAD,0),payload)));for(const at of [6,7,8,11]){const bad=b.slice();bad[at]^=1;assert.throws(()=>parseFrame(bad));}});
test('7-bit packing all byte values/partial groups; canonical bounds',()=>{for(let n=0;n<=384;n++){const bytes=Uint8Array.from({length:n},(_,i)=>(i*53+129)&255),packed=pack7(bytes);assert(packed.every(x=>x<128));assert.deepEqual(unpack7(packed),bytes);}assert.throws(()=>unpack7(Uint8Array.of(128,0)));assert.throws(()=>unpack7(Uint8Array.of(1)));assert.throws(()=>unpack7(Uint8Array.of(2,1)));assert.throws(()=>frame(128,1));assert.throws(()=>frame(1,31));assert.throws(()=>frame(1,1,new Uint8Array(381)));});
test('version/framing/CRC and truncated project records reject',()=>{const b=frame(1,1);b[5]=2;assert.throws(()=>parseFrame(b),/version/);for(let v=1;v<=6;v++){const sizes=[0,752,1264,1533,1759,1799,1959],out=new Uint8Array(sizes[v]),view=new DataView(out.buffer);out.set(new TextEncoder().encode('DRM'+v));view.setUint16(4,v,true);view.setUint16(6,16,true);view.setUint32(8,out.length-16,true);view.setUint32(12,crc32(out.slice(16)),true);assert.equal(verifyProjectWire(out),v);assert.throws(()=>verifyProjectWire(out.slice(0,-1)));out[100]^=1;assert.throws(()=>verifyProjectWire(out),/CRC/);}});
test('MIDI queue correlates replies, NACK and retries exact same transaction',async()=>{const input:InputPort={onmidimessage:null},sent:Uint8Array[]=[];let drop=true;const output={send(b:Uint8Array){sent.push(b.slice());const p=parseFrame(b);if(drop){drop=false;return;}queueMicrotask(()=>input.onmidimessage?.({data:frame(p.id,p.op,Uint8Array.of(9))}));}};const io=new MidiTransport(input,output,10);assert.deepEqual(await io.request(OP.SET_FX,Uint8Array.of(2),{mutating:true,retries:1}),Uint8Array.of(9));assert.equal(sent.length,2);assert.deepEqual(sent[0],sent[1]);io.close();});
test('MIDI synchronous ACK does not leave retry timer or duplicate mutation',async()=>{const input:InputPort={onmidimessage:null};let sends=0;const io=new MidiTransport(input,{send(b){sends++;const p=parseFrame(b);input.onmidimessage?.({data:frame(p.id,p.op)});}},10);await io.request(OP.SET_PAD,Uint8Array.of(1),{mutating:true});await delay(35);assert.equal(sends,1);io.close();});
test('MIDI timeout, device NACK and disconnection remain failures',async()=>{const input:InputPort={onmidimessage:null};const io=new MidiTransport(input,{send(b){const p=parseFrame(b);queueMicrotask(()=>input.onmidimessage?.({data:frame(p.id,p.op,new Uint8Array(),4)}));}},10);await assert.rejects(io.request(OP.SAVE),e=>e instanceof ProtocolError&&e.code===4);io.close();await assert.rejects(io.request(OP.CAPS),/disconnected/);const silent=new MidiTransport({onmidimessage:null},{send(){}},5);await assert.rejects(silent.request(OP.CAPS,new Uint8Array(),{retries:0}),/timeout/);silent.close();});


test('CAPS revision2 nine-character identity and legacy replies survive MIDI framing without field shifts',async()=>{
 for(const id of ['FM-1_900','FM-1_947','FM-1_1337']){
  const extended=id.length>8,b=new Uint8Array(extended?65+id.length:64),text=new TextEncoder();
  b.set([1,extended?2:0,6,1,8,6,6,4,55,16,64,8,6,63,7]);b.set([0,1,167,7],16);b.set(le32(19968),20);b.set(text.encode('FM1 DRUM OS 1.0.0-rc.2'),24);b.set(text.encode(id.slice(0,8)),56);
  if(extended){b[64]=id.length;b.set(text.encode(id),65);}
  assert.equal(capsIdentity(b),id);
  const input:InputPort={onmidimessage:null};let sends=0;
  const io=new MidiTransport(input,{send(raw){sends++;const p=parseFrame(raw);assert.equal(p.op,OP.CAPS);assert.equal(p.payload.length,0);queueMicrotask(()=>input.onmidimessage?.({data:frame(p.id,p.op,b)}));}});
  const d=new Device(io),c=await d.handshake();assert.equal(c.identity,id);assert.equal(c.wire,6);assert.equal(c.size,1959);assert.equal(c.userSlots,16);assert.equal(c.capacity,19968);assert.equal(sends,1);io.close();
 }
});
test('CAPS revision2 truncation, version, prefix and identity length mismatches fail closed',()=>{
 const b=new Uint8Array(74);b.set([1,2]);b.set(new TextEncoder().encode('FM-1_133'),56);b[64]=9;b.set(new TextEncoder().encode('FM-1_1337'),65);
 for(const x of [b.slice(0,64),b.slice(0,73),concat(b,Uint8Array.of(0))])assert.throws(()=>capsIdentity(x),/CAPS/);
 for(const [offset,value] of [[1,3],[64,8],[64,21],[56,65],[73,0],[73,255]]){const x=b.slice();x[offset]=value;assert.throws(()=>capsIdentity(x),/CAPS/);}
});
