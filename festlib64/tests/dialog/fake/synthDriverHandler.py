import wx
class _S: name = "festival"
_cur = _S()
def getSynth(): return _cur
def setSynth(name, isFallback=False):
    import threading
    wx.LOG.append(("setSynth", name, isFallback, threading.current_thread().name)); _cur.name = name; return True
