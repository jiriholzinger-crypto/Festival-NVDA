import sys, time, os
sys.path[:0] = ["/home/claude/testenv/guitest/fake", "/home/claude/testenv/guitest/addon/globalPlugins"]
import wx, gui
import festivalVoices as fv
from festivalVoices import catalog
base = "http://127.0.0.1:8765/packed/festival/2.5/"
catalog.VOICES_URL = base + "voices/"; catalog.LEXICONS["oald"] = base + "festlex_OALD.tar.gz"
def pumpUntil(cond, t=20):
    t0 = time.time()
    while time.time() - t0 < t:
        wx.pump(0.02)
        if cond(): return True
    return False
def show(title):
    print("--", title); [print("   ", e) for e in wx.LOG]; wx.LOG.clear()
plugin = fv.GlobalPlugin()
plugin.onMenu(None)
d = fv.VoicesDialog._instance
pumpUntil(lambda: not d._busy)
show("open dialog")
print("   rows:", d.list.rows)
print("   install enabled on first row (kal, included)?", d.installButton.enabled)
d.list.Select(1)
print("   install enabled on test_diphone?", d.installButton.enabled, " remove?", d.removeButton.enabled)
gui.ANSWERS[:] = [wx.YES]   # accept the OALD question
d.installButton.click()
pumpUntil(lambda: not d._busy and not any(isinstance(e, tuple) and e[0] == "progress" and "OALD" in e[1] for e in wx.LOG), 5)
pumpUntil(lambda: False, 3)
show("install test voice (+ OALD)")
print("   rows:", d.list.rows)
print("   store:", sorted(os.listdir("/home/claude/testenv/guitest/cfg/festivalVoices")), os.listdir("/home/claude/testenv/guitest/cfg/festivalVoices/voices/english"), os.listdir("/home/claude/testenv/guitest/cfg/festivalVoices/dicts"))
print("   install/remove enabled:", d.installButton.enabled, d.removeButton.enabled)
gui.ANSWERS[:] = [wx.YES]
d.removeButton.click(); pumpUntil(lambda: False, 1)
show("remove")
print("   rows:", d.list.rows, "store voices:", os.listdir("/home/claude/testenv/guitest/cfg/festivalVoices/voices"))
# cancel a download
orig = wx.ProgressDialog.Update
def abortingUpdate(self, v, msg=""):
    self.abort = True; return orig(self, v, msg)
wx.ProgressDialog.Update = abortingUpdate
d.list.Select(1); d.installButton.click(); pumpUntil(lambda: not d._busy, 20)
show("cancelled install")
wx.ProgressDialog.Update = orig
# catalog error
catalog.VOICES_URL = "http://127.0.0.1:1/nothing/"
d.refreshButton.click(); pumpUntil(lambda: not d._busy, 20)
show("refresh with server down")
d.Close(); plugin.terminate(); show("close")
print("instance cleared:", fv.VoicesDialog._instance is None)
