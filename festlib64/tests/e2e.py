import os, sys, time, types, tempfile
sys.path.insert(0, r"Z:\home\claude\src\festlib64\addon\globalPlugins\festivalVoices")
import catalog as c
base = "http://127.0.0.1:8765/packed/festival/2.5/"
c.VOICES_URL = base + "voices/"
c.LEXICONS["oald"] = base + "festlex_OALD.tar.gz"
store = r"Z:\home\claude\testenv\e2e\cfg\festivalVoices"
os.makedirs(store, exist_ok=True)
pk = c.fetchCatalog(c.VOICES_URL)
print("catalog:", [(p.voice, p.size, p.description) for p in pk])
p = [x for x in pk if x.voice == "test_diphone"][0]
steps = []
arch = os.path.join(tempfile.gettempdir(), "nvda-" + p.fileName)
c.download(p.url, arch, lambda d, t: steps.append(d * 100 // t))
print("download progress steps:", len(steps), "last", steps[-1], "% ; temp file", arch)
inst = c.installArchive(arch, store, "voices"); os.remove(arch)
print("installed:", inst)
v = c.installedVoices(os.path.join(store, "voices"))
need = c.neededLexicons(v["test_diphone"]); print("needs:", need)
for lex in need:
    a = os.path.join(tempfile.gettempdir(), "nvda-lex.tar.gz")
    c.download(c.LEXICONS[lex], a); print("lexicon:", c.installArchive(a, store, "dicts")); os.remove(a)
# cancel during download
try:
    c.download(p.url, arch + "2", lambda d, t: None, cancelled=lambda: True); print("FAIL no cancel")
except c.Cancelled:
    print("cancel works, no partial file:", not os.path.exists(arch + "2") and not os.path.exists(arch + "2.part"))
# now the synthesizer
sys.path.insert(0, r"Z:\home\claude\testenv\drvtest\mock")
sys.path.insert(0, r"Z:\home\claude\testenv\pkgtest")
gv = types.ModuleType("globalVars"); gv.appArgs = types.SimpleNamespace(configPath=r"Z:\home\claude\testenv\e2e\cfg", secure=False); sys.modules["globalVars"] = gv
import synthDriverHandler as sdh, nvwave
from speech.commands import IndexCommand as I
import festdrv
d = festdrv.SynthDriver(); print("synth voices:", d._voiceNames)
d.voice = "test_diphone"; sdh.EVENTS.clear(); d.speak(["Downloaded and installed.", I(1)])
t0 = time.time()
while not any(e[1] == "done" for e in sdh.EVENTS) and time.time() - t0 < 30: time.sleep(0.02)
print("speaks with", d._host.request("C"), "audio", round(len(nvwave.PLAYERS[-1].played) / nvwave.PLAYERS[-1].bytesPerSec, 2), "s")
d.terminate()
# remove while Festival is stopped, then the voice is gone
c.removeVoice(v["test_diphone"], os.path.join(store, "voices"))
d = festdrv.SynthDriver(); print("after remove:", d._voiceNames); d.terminate()
