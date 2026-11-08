import sys, time, threading
sys.path.insert(0, r"Z:\home\claude\testenv\drvtest\mock")
sys.path.insert(0, r"Z:\home\claude\testenv\drvtest\addon")
import synthDriverHandler as sdh, nvwave
from speech.commands import IndexCommand as I, PitchCommand
import festdrv

def events(): return [(n, i) for _, n, i in sdh.EVENTS]
def waitDone(timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if any(n == "done" for n, _ in events()): return True
        time.sleep(0.02)
    return False
fails = []
def check(cond, what):
    print(("PASS " if cond else "FAIL ") + what, flush=True)
    if not cond: fails.append(what)

print("check():", festdrv.SynthDriver.check())
t0 = time.time(); d = festdrv.SynthDriver(); print("init %.2fs voices=%s" % (time.time() - t0, d._voiceNames))
d.rate = 50; d.pitch = 50

# 1. indexes in order + done
for voice, text in (("kal_diphone", ["Hello there.", "This is the second sentence."]), ("czech_dita", ["Dobrý den.", "Toto je druhá věta."])):
    sdh.EVENTS.clear(); d.voice = voice
    d.speak([I(1), text[0], I(2), text[1], I(3)])
    ok = waitDone(); time.sleep(0.1)
    print(voice, "voice now", d.voice, events(), "player", nvwave.PLAYERS[-1].samplesPerSec, "Hz, created on", nvwave.PLAYERS[-1].creator)
    check(ok and events() == [("index", 1), ("index", 2), ("index", 3), ("done", None)], "indexes+done " + voice)

# 2. sequence without words
sdh.EVENTS.clear(); d.speak([I(10), "...", I(11)]); ok = waitDone(); time.sleep(0.05)
check(ok and events() == [("index", 10), ("index", 11), ("done", None)], "no-word sequence " + str(events()))

# 3. cancel during long speech, then speak again
d.voice = "kal_diphone"
long = []
for k in range(8):
    long += [I(100 + k), "This is sentence number %d of a long paragraph that is read with say all." % k]
sdh.EVENTS.clear(); d.speak(long); time.sleep(1.0)
before = events(); d.cancel(); time.sleep(1.5); after = events()
check(len(before) >= 1 and before == after and ("done", None) not in after, "cancel stops indexes (got %s)" % before)
sdh.EVENTS.clear(); d.speak(["After cancel.", I(200)]); ok = waitDone()
check(ok and events() == [("index", 200), ("done", None)], "speak after cancel " + str(events()))

# 4. pause / resume
sdh.EVENTS.clear(); d.speak(["One two three four five six seven eight.", I(300)]); time.sleep(0.2)
d.pause(True); time.sleep(1.5); paused = events()
d.pause(False); ok = waitDone()
check(paused == [] and ok and events()[-1] == ("done", None), "pause holds, resume finishes")

# 5. capital pitch: PitchCommand changes only the marked piece
import struct
def f0(buf, sr):
    import array
    a = array.array("h"); a.frombytes(buf); x = [float(v) for v in a]
    L = int(0.1 * sr); f = []
    for i in range(0, len(x) - L, L):
        fr = x[i:i + L]; m = sum(fr) / L; fr = [v - m for v in fr]
        e = sum(v * v for v in fr)
        if e / L < 500 ** 2: continue
        best, bk = -1, 0
        for k in range(sr // 300, sr // 60):
            c = sum(fr[j] * fr[j + k] for j in range(0, L - k, 2))
            if c > best: best, bk = c, k
        f.append(sr / bk)
    f.sort(); return f[len(f) // 2] if f else 0
res = {}
for label, seq in (("normal", ["Pitch test sentence."]), ("capital", [PitchCommand(offset=30), "Pitch test sentence.", PitchCommand()]), ("after", ["Pitch test sentence."])):
    p = nvwave.PLAYERS[-1]; start = len(p.played)
    sdh.EVENTS.clear(); d.speak(seq + [I(1)]); waitDone(); time.sleep(0.1)
    res[label] = f0(bytes(p.played[start:]), p.samplesPerSec)
print("F0", res)
check(res["capital"] > res["normal"] * 1.3 and abs(res["after"] - res["normal"]) < res["normal"] * 0.1, "PitchCommand only for its piece")
d.pitch = 80; sdh.EVENTS.clear(); p = nvwave.PLAYERS[-1]; start = len(p.played); d.speak(["Pitch test sentence.", I(1)]); waitDone(); time.sleep(0.1)
hi = f0(bytes(p.played[start:]), p.samplesPerSec); print("F0 at pitch 80:", hi)
check(hi > res["normal"] * 1.3, "pitch setting")
d.pitch = 50

# 6. terminate and start again (NVDA switching synths)
d.terminate()
d = festdrv.SynthDriver(); d.voice = "czech_machac"
sdh.EVENTS.clear(); d.speak(["Znovu spuštěno.", I(5)]); ok = waitDone()
check(ok and events() == [("index", 5), ("done", None)], "re-created driver works, voice " + str(d.voice))
d.terminate()
print("RESULT:", "ALL PASSED" if not fails else "FAILED: %s" % fails, flush=True)
