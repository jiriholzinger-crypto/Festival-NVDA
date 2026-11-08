import sys, time, types
sys.path.insert(0, r"Z:\home\claude\testenv\pkgtest\mockstore")
sys.path.insert(0, r"Z:\home\claude\testenv\drvtest\mock")
sys.path.insert(0, r"Z:\home\claude\testenv\pkgtest")
lh = types.ModuleType("languageHandler"); lh.getLanguage = lambda: "cs"; sys.modules["languageHandler"] = lh
import synthDriverHandler as sdh, nvwave
from speech.commands import IndexCommand as I
import festdrv
d = festdrv.SynthDriver()
print("voices:", d._voiceNames)
print("language of test_diphone:", d._voiceLanguage("test_diphone"))
for v in ("test_diphone", "kal_diphone", "czech_ph"):
    d.voice = v
    p0 = nvwave.PLAYERS[-1] if nvwave.PLAYERS else None; n0 = len(p0.played) if p0 else 0
    sdh.EVENTS.clear(); d.speak(["Hello from the downloaded voice.", I(1)])
    t0 = time.time()
    while not any(e[1] == "done" for e in sdh.EVENTS) and time.time() - t0 < 30: time.sleep(0.02)
    p = nvwave.PLAYERS[-1]
    played = len(p.played) - (n0 if p is p0 else 0)
    print("%-13s Festival uses %-13s audio %.2f s, events %s" % (v, d._host.request("C"), played / p.bytesPerSec, [(e[1], e[2]) for e in sdh.EVENTS]))
d.terminate()
