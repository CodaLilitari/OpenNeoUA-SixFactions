"""Develop Lily's event grammar and dialogue into versioned, measurable voicepacks."""
from pathlib import Path
import hashlib
import json
import math
import random
import re
import struct
import subprocess
import sys
import wave

sys.dont_write_bytecode = True
import generate_sulgogar_pilot as dsp

import argparse
import shutil
HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--work-dir', type=Path, required=True)
parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg'))
parser.add_argument('--sapi-python', default=sys.executable)
args = parser.parse_args()
OUT = args.output.resolve()
TEMP = args.work_dir.resolve() / 'processing-v3'
RAW = args.work_dir.resolve() / 'black-sect-raw-v3'
repo_root = HERE.parents[1]
if OUT.is_relative_to(repo_root) or args.work_dir.resolve().is_relative_to(repo_root):
    parser.error('Generated audio and processing files must stay outside the checkout.')
if not args.ffmpeg:
    parser.error('Install FFmpeg or supply --ffmpeg.')
FF = Path(args.ffmpeg)
PY = Path(args.sapi_python)
DIALOGUE = HERE / 'black-sect-dialogue.json'
RATE = dsp.RATE

def run(args, timeout=30):
    r = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=timeout,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    if r.returncode:
        raise RuntimeError(r.stderr)
    return r.stderr

def measure(path):
    log = run([FF, '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', 'NUL'])
    tail = log.split('Summary:')[-1]
    return {'integrated_lufs': float(re.search(r'I:\s*([-\d.]+) LUFS', tail)[1]),
            'true_peak_dbfs': float(re.search(r'Peak:\s*([-\d.]+) dBFS', tail)[1])}

def read(path):
    with wave.open(str(path), 'rb') as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1,2,RATE)
        raw = w.readframes(w.getnframes())
    return list(struct.unpack('<'+'h'*(len(raw)//2), raw))

def zero_dc(samples):
    window = [dsp.smooth(i/(RATE*.012))*dsp.smooth((len(samples)-1-i)/(RATE*.018)) for i in range(len(samples))]
    samples = [v*e for v,e in zip(samples,window)]
    dc = sum(samples)/sum(window)
    pcm = [round(v-dc*e) for v,e in zip(samples,window)]
    residue = sum(pcm)
    for i in range(abs(residue)):
        pcm[64+i%(len(pcm)-128)] -= 1 if residue>0 else -1
    return pcm

def finish(samples, destination, target, compress=False):
    temp = TEMP / destination.name
    dsp.write_wave(temp, zero_dc(samples))
    if compress:
        filtered = TEMP / ('filtered-'+destination.name)
        run([FF,'-hide_banner','-nostats','-i',temp,'-af',
             'highpass=f=170,lowpass=f=4800,acompressor=threshold=0.0316:ratio=3:attack=4:release=65:knee=2.828:makeup=1',
             '-ar',RATE,'-ac',1,'-c:a','pcm_s16le',filtered])
        temp = filtered
    before = measure(temp)
    # Static gain after modest compression, constrained by measured true peak.
    gain_db = min(target-before['integrated_lufs'], -3.1-before['true_peak_dbfs'])
    gain = 10**(gain_db/20)
    pcm = zero_dc([v*gain for v in read(temp)])
    assert max(abs(v) for v in pcm)<32767
    dsp.write_wave(destination, pcm)
    after = measure(destination)
    assert after['true_peak_dbfs'] <= -3.0
    assert sum(pcm)==0 and pcm[0]==pcm[-1]==0
    stats = {'duration_seconds': round(len(pcm)/RATE,6), 'bytes': destination.stat().st_size,
             'sha256':hashlib.sha256(destination.read_bytes()).hexdigest(), **after,
             'target_lufs':target, 'gain_db':round(gain_db,3), 'pre_level':before,
             'dc_mean_samples':0, 'clipped_samples':0, 'first_sample':0, 'last_sample':0}
    return pcm, stats

def clicks(samples, key, variant):
    starts = {'unit_control_entered':[.015], 'enemy_sector_entered':[.015,.105], 'order_ack_enemy':[.015]}.get(key,[])
    rng = random.Random(f'coda-lily-sulg-click-v3:{key}:{variant}')
    for start in starts:
        length = .065 if key=='order_ack_enemy' else .040
        frequency = 90 if key=='order_ack_enemy' else 420
        low=0.
        for n in range(round(length*RATE)):
            t=n/RATE
            low += .28*(rng.uniform(-1,1)-low)
            envelope = dsp.smooth(t/.005)*math.exp(-t/(length/4))*dsp.smooth((length-t)/.01)
            value = (math.sin(2*math.pi*frequency*t)*.7 + low*.3)*envelope*10500
            i=round(start*RATE)+n
            if i<len(samples): samples[i]+=value
    return samples

assert not OUT.exists(), 'Use a new version; do not overwrite accepted or reviewable files.'
OUT.mkdir(parents=True)
TEMP.mkdir(parents=True)
run([PY,HERE/'sapi_pack_child.py','--dialogue',DIALOGUE,'--output',RAW],180)
black = json.loads((RAW/'source.json').read_text())
manifest = {'version':3,'collaboration':'Coda implementation with Lily dialogue and event-grammar review; Andrei approved developing the pilots.',
    'source_review':'Lily technical and design review, 2026-11-01',
    'listening_status':'Technical analysis only by Coda and Lily; in-game listening still pending.',
    'format':'PCM16 mono 22050 Hz', 'level_policy':'Provisional -18 LUFS routine / -17 LUFS strategic, true peak <= -3 dBFS. Stock references measured -14.4 to -11 LUFS; live speech-bus comparison pending.',
    'black_voice':black['voice'], 'sulg_provenance':'Original procedural synthesis; no existing samples or actor imitation.',
    'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), 'cues':[]}

for faction in ('black-sect-mp','sulgogars-mp'):
    (OUT/faction).mkdir()
sequence=[]
for item in black['cues']:
    samples = read(RAW/item['file'])
    active=[i for i,v in enumerate(samples) if abs(v)>8]
    samples=samples[max(0,active[0]-round(RATE*.025)):min(len(samples),active[-1]+round(RATE*.025))]
    dest=OUT/'black-sect-mp'/item['file']
    pcm,stats=finish(samples,dest,-18,True)
    manifest['cues'].append({'faction':'black-sect-mp',**item,**stats})
    sequence.extend(pcm+[0]*round(.20*RATE))
dsp.write_wave(OUT/'black-sect-all-32-review.wav',sequence)

# Lily's specific revisions preserve a shared organic timbre but split event shapes.
dsp.PRESETS.update({
 'unit_idle':('two quiet questioning warbles',(370,800,1450),.60,[(0,.20,122,109,.7),(.31,.24,109,127,.7)]),
 'order_ack_enemy':('low membrane impact and descending closure',(430,950,1650),.51,[(.16,.31,186,126,1)]),
 'unit_control_entered':('one membrane click and sustained rising vowel',(560,1180,1900),.62,[(.12,.46,132,205,1)]),
 'enemy_sector_entered':('two membrane clicks and short warning rise',(420,980,1750),.55,[(.23,.27,183,221,1)]),
 'unit_recovered':('smooth fall then rise',(550,1050,1770),.76,[(0,.28,181,130,.8),(.28,.44,130,202,1)]),
 'enemy_unit_destroyed':('decisive pulse with falling tail',(540,1130,1800),.52,[(0,.47,222,105,1)]),
 'powerstation_captured':('one continuous rising energy bloom',(600,1300,2020),1.05,[(0,1.01,126,244,1)]),
 'request_support':('low call followed by two answer chirps',(570,1200,1950),.96,[(0,.33,107,132,.9),(.44,.16,190,215,.8),(.69,.22,220,248,1)])
})
sequence=[]
for key in dsp.PRESETS:
    for variant in (1,2):
        samples,seed,label=dsp.render(key,variant)
        samples=clicks(samples,key,variant)
        dest=OUT/'sulgogars-mp'/f'{key}_{variant}.wav'
        target=-17 if key in ('enemy_host_found','enemy_host_destroyed','powerstation_captured') else -18
        if key=='unit_idle': target=-19
        pcm,stats=finish(samples,dest,target)
        manifest['cues'].append({'faction':'sulgogars-mp','event':key,'variant':variant,
                               'file':dest.name,'grammar':label,'seed':seed,**stats})
        sequence.extend(pcm+[0]*round(.20*RATE))
dsp.write_wave(OUT/'sulgogars-all-32-review.wav',sequence)
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps({'cues':len(manifest['cues']),'output':str(OUT),
    'range_lufs':[min(x['integrated_lufs'] for x in manifest['cues']),max(x['integrated_lufs'] for x in manifest['cues'])],
    'max_true_peak':max(x['true_peak_dbfs'] for x in manifest['cues'])}))
