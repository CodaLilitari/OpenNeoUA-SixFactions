"""File-only speech synthesis for the approved Black Sect voice direction."""
from pathlib import Path
import json
import argparse
import comtypes.client

HERE = Path(__file__).resolve().parent
BASE = HERE.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--dialogue', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
dialogue = json.loads(args.dialogue.read_text())
dialogue = dialogue.get('events', dialogue)
out = args.output
out.mkdir(exist_ok=False)
voice = comtypes.client.CreateObject('SAPI.SpVoice', dynamic=True)
tokens = voice.GetVoices()
token = next(tokens.Item(i) for i in range(tokens.Count)
             if tokens.Item(i).GetDescription() == 'Microsoft David Desktop - English (United States)')
voice.Voice = token
voice.Volume = 100
frequent = {'unit_idle', 'order_ack_friendly', 'order_ack_enemy', 'unit_control_entered',
            'enemy_unit_engaged', 'enemy_unit_spotted', 'enemy_unit_destroyed'}
records = []
for event, lines in dialogue.items():
    for variant, text in enumerate(lines, 1):
        path = out / f'{event}_{variant}.wav'
        stream = comtypes.client.CreateObject('SAPI.SpFileStream', dynamic=True)
        stream.Format.Type = 22
        stream.Open(str(path), 3, False)
        voice.AllowAudioOutputFormatChangesOnNextSet = False
        voice.AudioOutputStream = stream
        voice.Rate = 1 if event in frequent else 0
        voice.Speak(text, 0)
        stream.Close()
        records.append({'event': event, 'variant': variant, 'phrase': text,
                        'rate': voice.Rate, 'file': path.name})
        print(path.name, flush=True)
(out / 'source.json').write_text(json.dumps({'engine': 'Windows SAPI.SpVoice',
    'voice': token.GetDescription(), 'token_id': token.Id,
    'method': 'SpFileStream assigned before Speak; no audio-device playback',
    'actor_imitation': False, 'suno_output': False, 'cues': records}, indent=2))
