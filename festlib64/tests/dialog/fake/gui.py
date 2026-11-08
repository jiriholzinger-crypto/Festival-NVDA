import wx
ANSWERS = []
def messageBox(msg, title, style, parent=None):
    wx.LOG.append(("messageBox", msg)); return ANSWERS.pop(0) if ANSWERS else wx.OK
class _Tray:
    toolsMenu = wx.Menu()
    def Bind(self, *a): pass
class _Main:
    sysTrayIcon = _Tray()
    def prePopup(self): pass
    def postPopup(self): pass
mainFrame = _Main()
import guiHelper
