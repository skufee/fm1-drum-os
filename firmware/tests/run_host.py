#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused portable host checks. gcc is an optional external dependency."""
from pathlib import Path
import json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/host-tests';out.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('gcc')
if not gcc:raise SystemExit('gcc is required for host tests; target build uses the separate JieLi toolchain')
tests=['rc2_start_test','corrective_slots_test','release_user_samples_test','release_persistence_test','drum_sequencer_test','drum_timing_test','drum_qtz_test','rc0_midi_test','rc0_fx_test']
rows=[]
for name in tests:
 exe=out/(name+('.exe' if __import__('os').name=='nt' else ''))
 cmd=[gcc,'-O1','-w','-D_POSIX_C_SOURCE=200809L','-DPROJECT_TEST_NO_PROCESS=1','-DFELUCCA_DRUM_ONLY=1','-DFELUCCA_RUNTIME_TRACKS=1','-DFELUCCA_RUNTIME_DRUM_TRACK=0','-I'+str(root/'build/gen'),'-I'+str(root/'firmware/src'),'-I'+str(root/'firmware/hal'),str(root/'tests'/str(name+'.c')),'-lm','-o',str(exe)]
 with (out/(name+'.log')).open('wb') as log:
  subprocess.run(cmd,cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
  subprocess.run([str(exe)],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True,env={**os.environ,'FM1_TEST_OUTPUT':str(out)})
 rows.append({'test':name,'status':'PASS'})
 print('PASS',name,flush=True)
(out/'results.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
