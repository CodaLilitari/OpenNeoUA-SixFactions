"""Original deterministic procedural audio pilots. No sampled/Suno material.

Standard Python only. The motifs are new design choices, not recovered game audio.
Files are review candidates; technical checks cannot establish listening quality.
"""
from array import array
from pathlib import Path
import argparse
import hashlib
import json
import math
import random
import re
import struct
import sys
import wave

RATE = 22050
PEAK = 10 ** (-10 / 20)
TAU = 2 * math.pi

# Each note: start seconds, duration, initial/final fundamental Hz, strength.
# Vowel colours are three broad formants. They make soft voiced/breathy sounds
# without borrowing human speech, samples or a performer's identity.
PRESETS = {
 'unit_ready': ('three rising open calls', (520,1080,1850), .64, [(0,.16,128,146,.8),(.19,.16,160,179,.9),(.39,.23,190,225,1)]),
 'unit_idle': ('quiet low questioning warble', (370,800,1450), .45, [(0,.42,119,109,.8)]),
 'order_ack_friendly': ('calm two-note assent', (490,1040,1690), .40, [(0,.15,135,143,.8),(.19,.18,153,161,1)]),
 'order_ack_enemy': ('firm closing three-note assent', (430,950,1650), .50, [(0,.12,175,184,.8),(.15,.12,185,177,1),(.31,.16,165,146,1)]),
 'unit_control_entered': ('close responsive upward vowel', (560,1180,1900), .49, [(0,.19,132,148,.8),(.22,.24,158,197,1)]),
 'enemy_sector_entered': ('two alert pulses with lifted ending', (420,980,1750), .55, [(0,.20,150,176,.8),(.25,.27,178,207,1)]),
 'unit_recovered': ('soft falling tension then rising relief', (550,1050,1770), .72, [(0,.23,165,137,.7),(.28,.40,142,199,1)]),
 'enemy_unit_spotted': ('three quick rising warning pulses', (400,940,1670), .52, [(0,.12,153,173,.8),(.16,.12,179,194,.9),(.32,.17,201,220,1)]),
 'enemy_host_found': ('low warning followed by long high call', (460,1120,1830), .83, [(0,.20,119,135,.8),(.26,.53,173,223,1)]),
 'request_support': ('three evenly spaced calling pulses', (570,1200,1950), .88, [(0,.20,155,182,.9),(.28,.20,155,182,.9),(.56,.28,179,210,1)]),
 'unit_retreating': ('two descending withdrawing calls', (440,900,1510), .65, [(0,.24,192,158,.9),(.30,.31,151,112,1)]),
 'unit_lost': ('long low falling hollow exhalation', (350,720,1330), .90, [(0,.85,168,73,1)]),
 'enemy_unit_engaged': ('four compact alternating combat pulses', (410,1000,1740), .67, [(0,.11,162,178,.9),(.15,.11,192,181,.9),(.30,.11,163,180,.9),(.45,.18,198,210,1)]),
 'enemy_unit_destroyed': ('brief resonant upward resolution', (540,1130,1800), .66, [(0,.20,151,171,.8),(.25,.36,187,225,1)]),
 'enemy_host_destroyed': ('broad three-step victory resonance', (580,1230,1940), 1.17, [(0,.27,123,146,.8),(.32,.29,163,191,.9),(.66,.46,210,249,1)]),
 'powerstation_captured': ('sustained rising energy resonance', (600,1300,2020), 1.03, [(0,.30,132,164,.8),(.34,.64,175,224,1)]),
}


def smooth(x):
    x = max(0.,min(1.,x))
    return x*x*(3-2*x)


def render(key, variant):
    label,formants,duration,notes = PRESETS[key]
    seed = int.from_bytes(hashlib.sha256(f'coda-sulgogar-pilot-v1:{key}:{variant}'.encode()).digest()[:8],'big')
    rng = random.Random(seed)
    samples = [0.] * round(duration * RATE)
    pitch_scale = 1.0 if variant==1 else .965
    formant_scale = 1.0 if variant==1 else 1.045
    for start,length,f0,f1,strength in notes:
        phase = rng.random()*TAU
        noise_low = noise_high = 0.
        # Independently seeded note phases and slow irregular breath modulation.
        wobble = rng.uniform(4.2,5.7)
        for n in range(round(length*RATE)):
            t=n/RATE
            u=t/length
            attack=smooth(t/.035)
            release=smooth((length-t)/.075)
            env=attack*release*(.94+.06*math.sin(TAU*2.3*t))
            pitch=pitch_scale*(f0+(f1-f0)*smooth(u))*(1+.013*math.sin(TAU*wobble*t)+.004*math.sin(TAU*1.7*t))
            phase += TAU*pitch/RATE
            voiced=0.
            weights=0.
            for harmonic in range(1,15):
                frequency=harmonic*pitch
                weight=.11/(harmonic**1.5)
                for formant,band in zip(formants,(180,260,330)):
                    weight += (.7/harmonic**.45)*math.exp(-.5*((frequency-formant*formant_scale)/band)**2)
                voiced+=weight*math.sin(harmonic*phase + .1*math.sin(TAU*3.1*t+harmonic))
                weights+=weight
            voiced/=weights
            white=rng.uniform(-1.,1.)
            noise_low += .39*(white-noise_low)
            noise_high += .045*(white-noise_high)
            breath=noise_low-noise_high
            resonance=.11*math.sin(phase*1.5+.5*math.sin(phase)) if key in ('powerstation_captured','enemy_host_destroyed','unit_recovered') else 0.
            value=(.89*voiced+.065*breath+resonance)*env*strength
            idx=round(start*RATE)+n
            if idx<len(samples):
                samples[idx]+=value

    # Gentle one-pole low-pass and global fades make ends click-free without a
    # long reverberation tail. Remove DC through the same envelope, preserving0.
    last=0.
    window=[]
    for i,value in enumerate(samples):
        last += .46*(value-last)
        env=smooth(i/(RATE*.012))*smooth((len(samples)-1-i)/(RATE*.018))
        window.append(env)
        samples[i]=last*env
    correction=sum(samples)/sum(window)
    samples=[value-correction*env for value,env in zip(samples,window)]
    gain=PEAK/max(abs(v) for v in samples)
    if key=='unit_lost':
        gain*=.90
    if key=='unit_idle':
        gain*=.82
    pcm=[round(v*gain*32767) for v in samples]
    # Eliminate the tiny quantization DC residue without touching either end.
    residual=sum(pcm)
    step=1 if residual>0 else -1
    for index in range(abs(residual)):
        pcm[64+index%(len(pcm)-128)]-=step
    return pcm,seed,label


def write_wave(path,pcm):
    raw=array('h',pcm)
    if sys.byteorder!='little':
        raw.byteswap()
    with wave.open(str(path),'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(raw.tobytes())


def inspect(path, expected_max_duration=1.3):
    with wave.open(str(path),'rb') as src:
        assert (src.getnchannels(),src.getsampwidth(),src.getframerate(),src.getcomptype())==(1,2,RATE,'NONE')
        raw=src.readframes(src.getnframes())
    pcm=struct.unpack('<'+'h'*(len(raw)//2),raw)
    peak=max(abs(v) for v in pcm)
    rms=math.sqrt(sum(v*v for v in pcm)/len(pcm))/32768
    duration=len(pcm)/RATE
    stats={'duration_seconds':round(duration,6),'sample_rate':RATE,'channels':1,'bits':16,
           'peak_dbfs':round(20*math.log10(peak/32768),4),'rms_dbfs':round(20*math.log10(rms),4),
           'dc_mean_samples':sum(pcm)/len(pcm),'first_sample':pcm[0],'last_sample':pcm[-1],
           'clipped_samples':sum(v in(-32768,32767) for v in pcm),
           'first_32_peak_samples':max(abs(v) for v in pcm[:32]),'last_32_peak_samples':max(abs(v) for v in pcm[-32:]),
           'max_sample_step':max(abs(a-b) for a,b in zip(pcm,pcm[1:])),
           'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size}
    assert .3<=duration<=expected_max_duration
    assert stats['peak_dbfs']<=-9 and stats['clipped_samples']==0
    assert stats['dc_mean_samples']==0 and pcm[0]==pcm[-1]==0
    assert stats['first_32_peak_samples']<100 and stats['last_32_peak_samples']<100
    assert -38 < stats['rms_dbfs'] < -12
    return stats


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--speech-source',type=Path,required=True)
    args=parser.parse_args()
    source=args.speech_source.read_bytes()
    keys=re.findall(r'\{\d+, "([a-z_]+)"\}',source.decode())
    assert keys==list(PRESETS), 'Pilot keys must exactly match source event order'
    output=args.output
    output.mkdir(parents=True,exist_ok=True)
    pack=output/'sulgogars-mp'
    pack.mkdir(exist_ok=True)
    assert not list(pack.glob('*.wav')), 'Refusing to overwrite audio pilot files'
    manifest={'title':'Original procedural Sulgogar pilot v1',
              'provenance':'New deterministic standard-Python DSP by Coda collaborating with Andrei. No samples, recovered game sounds, actor imitation, Suno output or API generation.',
              'status':'Pilot only; technical validation performed, listening and Lily adoption pending',
              'source_event_file':str(args.speech_source.resolve()),'source_event_sha256':hashlib.sha256(source).hexdigest(),
              'generator':str(Path(__file__).resolve()),'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'format':'Dry mono PCM16,22050Hz; peak<=-9dBFS', 'cues':[], 'sequence':[]}
    sequence=[]
    for key in keys:
        for variant in (1,2):
            pcm,seed,label=render(key,variant)
            path=pack/f'{key}_{variant}.wav'
            write_wave(path,pcm)
            item={'event':key,'variant':variant,'vocabulary':label,'seed':seed,'path':str(path.resolve()),**inspect(path)}
            manifest['cues'].append(item)
            if variant==1:
                manifest['sequence'].append({'event':key,'start_seconds':round(len(sequence)/RATE,6),'duration_seconds':item['duration_seconds']})
                sequence.extend(pcm)
                sequence.extend([0]*round(.15*RATE))
    preview=output/'sulgogar-16-cue-review-sequence.wav'
    write_wave(preview,sequence)
    manifest['review_sequence']={'path':str(preview.resolve()),**inspect(preview,30)}
    manifest['wav_bytes_total']=sum(p.stat().st_size for p in output.rglob('*.wav'))
    assert manifest['wav_bytes_total']<2_500_000, 'Keep space for a spoken pilot under3MB total'
    (output/'sulgogar-pilot-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (output/'README.md').write_text('''# Original audio pilot candidates

These sounds were synthesized locally with deterministic standard-Python DSP.
They contain no Suno output, samples of existing game audio, or actor imitation.
They are review candidates and have not been adopted as a final game voice pack.

`sulgogars-mp/` contains two variants for each of the16 exact engine event keys.
Calm acknowledgements use short soft vowels; readiness and recovery rise;
threat/support use distinct pulse patterns; retreat/loss fall; victories and
captured energy use fuller resonances. The generator and preset descriptions are
the exact procedural source. This is a new vocabulary, not a canonical Sulgogar
language or a recovered original voice pack.

The review sequence plays variant1 in source event order, separated by150ms of
silence. Its manifest records exact event timestamps for comparison. No audio was
played aloud during generation. Technical checks verify format, durations, peak,
RMS, DC and endpoints; they cannot establish intelligibility, pleasantness,
in-game audibility or suitability. Listening and Lily's review remain pending.

All cues are dry mono PCM16 at22050Hz,0.40–1.17seconds, with peaks below-9dBFS,
zero mean after quantization, zero-valued endpoints and no clipped samples.
''',encoding='utf-8')
    print(json.dumps({'cues':len(manifest['cues']),'preview_duration':manifest['review_sequence']['duration_seconds'],
                      'wav_bytes_total':manifest['wav_bytes_total'],'manifest':str((output/'sulgogar-pilot-manifest.json').resolve())},indent=2))


if __name__=='__main__':
    main()
