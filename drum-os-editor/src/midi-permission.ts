// SPDX-License-Identifier: GPL-3.0-only
/** A permission query never opens a browser prompt. Only the explicit click
 * may request permission in the prompt/unknown state. */
export async function previouslyGrantedMIDI(nav:Pick<Navigator,'permissions'>):Promise<boolean> {
 try{return !!nav.permissions&&(await nav.permissions.query({name:'midi',sysex:true} as unknown as PermissionDescriptor)).state==='granted';}catch{return false;}
}
export function chooseMIDIPorts(inputs:{id:string;name?:string|null}[],outputs:{id:string;name?:string|null}[],remembered?:{input:string;output:string},automatic=false) {
 const fm=(ports:{id:string;name?:string|null}[])=>ports.filter(p=>/FM.?1|FELUCCA|SLOOP/i.test(p.name??''));
 const ins=fm(inputs),outs=fm(outputs),unique=ins.length===1&&outs.length===1;
 const known=!!remembered&&inputs.some(p=>p.id===remembered.input)&&outputs.some(p=>p.id===remembered.output);
 return {input:known?remembered!.input:(unique?ins[0]:inputs[0])?.id??'',output:known?remembered!.output:(unique?outs[0]:outputs[0])?.id??'',automatic:known||unique||(!automatic&&inputs.length===1&&outputs.length===1)};
}
