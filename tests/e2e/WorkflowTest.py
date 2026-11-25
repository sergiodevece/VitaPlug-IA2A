"""Run the actual CLI against generated PCM captures, including its exported WAVs."""
import array
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

RATE = 24000
voice_prompt = "Ponla en primer plano y que quede integrada en la mezcla al mismo tiempo"
drums_prompt = "Los platos están muy sucios"

def write_wave(path, samples):
    channels = len(samples[0])
    with wave.open(str(path), "wb") as target:
        target.setparams((channels, 2, RATE, 0, "NONE", ""))
        pcm = array.array("h", (round(v * 32767) for frame in samples for v in frame))
        if sys.byteorder != "little":
            pcm.byteswap()
        target.writeframes(pcm.tobytes())

def run_test(binary, folder):
    def evaluate(name, frames, source, prompt, reference=None):
        track = folder / (name + ".wav")
        write_wave(track, frames)
        out = folder / name
        args = [binary, "--track", str(track), "--source", source, "--prompt", prompt, "--out", str(out)]
        if reference:
            ref = folder / (name + "-ref.wav")
            write_wave(ref, reference)
            args.extend(["--ref", str(ref)])
        env = {**os.environ, "IA2A_TURN_URL": ""}
        subprocess.run(args, check=True, env=env)
        for output in ("nivel-final.wav", "volumen-igualado.wav"):
            assert (out / output).stat().st_size >= 2 * len(frames)
        report=json.loads((out / "informe.json").read_text())
        assert report["applied"] is False and report["provider"] == "local-demo", report
        return report

    times = [i / RATE for i in range(2 * RATE)]
    def speech(soft):
        return [(math.sin(2*math.pi*227*t)*(.39 if int(t*2)%2 else soft),) for t in times]
    irregular=evaluate("voz",speech(.065),"voice",voice_prompt)
    smoother=evaluate("otra-voz",speech(.19),"voice",voice_prompt)
    assert irregular["decision"]=="candidate" and irregular["after"]["phraseSpreadDb"] < irregular["before"]["phraseSpreadDb"]
    assert irregular["settings"]!=smoother["settings"], (irregular,smoother)
    def kit(wide):
        result=[]
        for t in times:
            beat=t%.37
            centre=.64*math.exp(-beat*18)*math.sin(2*math.pi*97*t)
            cymbal=.10*math.exp(-beat*4)*math.sin(2*math.pi*7400*t)
            result.append((centre+cymbal,centre+(-cymbal if wide else cymbal)))
        return result
    open_kit=evaluate("lateral",kit(True),"drums",drums_prompt)
    centre=evaluate("centrado",kit(False),"drums",drums_prompt)
    assert open_kit["settings"]["midSide"] and not centre["settings"]["midSide"]
    active = [f if t<1 else (0.,) for t,f in zip(times,speech(.11))]
    base = [(math.sin(2*math.pi*122*t)*.18 if t<1 else 0.,) for t in times]
    together=evaluate("coincide",active,"voice",voice_prompt,base)
    apart=evaluate("desplazada",active,"voice",voice_prompt,base[RATE:]+base[:RATE])
    assert together["input"]["referenceOverlap"]>.95 and apart["input"]["referenceOverlap"]<.05
    assert together["settings"]!=apart["settings"], (together,apart)
    question=evaluate("aclaracion",speech(.11),"voice","Quiero otro color")
    assert question["decision"]=="question"

with tempfile.TemporaryDirectory(prefix="ia2a-workflow-") as directory:
    run_test(sys.argv[1],Path(directory))
