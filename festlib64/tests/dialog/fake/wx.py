import queue, itertools
_calls = queue.Queue()
def CallAfter(f, *a, **k): _calls.put((f, a, k))
def pump(timeout=0.05):
    n = 0
    while True:
        try: f, a, k = _calls.get(timeout=timeout)
        except queue.Empty: return n
        f(*a, **k); n += 1
ID_ANY, ID_CLOSE, ID_OK = -1, 5104, 5100
OK = 4; YES = 2; NO = 8; YES_NO = 10; ICON_ERROR = 512; ICON_QUESTION = 1024
VERTICAL, HORIZONTAL, ALL, EXPAND = 8, 4, 240, 8192
LC_REPORT = 32; LC_SINGLE_SEL = 64
PD_APP_MODAL = PD_CAN_ABORT = PD_AUTO_HIDE = PD_ELAPSED_TIME = 1
EVT_LIST_ITEM_SELECTED = "sel"; EVT_LIST_ITEM_DESELECTED = "desel"; EVT_BUTTON = "button"; EVT_CLOSE = "close"; EVT_MENU = "menu"
LOG = []
class Window:
    def __init__(self, parent=None, *a, **k): self.handlers = {}; self.label = k.get("label", ""); self.enabled = True; self.id = k.get("id", -1)
    def Bind(self, evt, h, src=None): self.handlers[evt] = h
    def Enable(self, v=True): self.enabled = bool(v)
    def SetLabel(self, t): self.label = t
    def GetLabel(self): return self.label
    def SetFocus(self): pass
    def Layout(self): pass
    def SetSizer(self, s): pass
    def CentreOnScreen(self): pass
    def Raise(self): pass
    def Show(self): pass
    def Destroy(self): self._destroyed = True
    def __bool__(self): return not getattr(self, "_destroyed", False)
    def click(self): self.handlers["button"](None)
class Dialog(Window):
    def Close(self):
        e = type("E", (), {"CanVeto": lambda s: True, "Veto": lambda s: LOG.append("veto")})()
        self.handlers["close"](e)
class Button(Window): pass
class StaticText(Window): pass
class ListCtrl(Window):
    def __init__(self, *a, **k): super().__init__(*a, **k); self.rows = []; self.sel = -1
    def InsertColumn(self, i, t, width=0): pass
    def DeleteAllItems(self): self.rows = []; self.sel = -1
    def Append(self, row): self.rows.append(row)
    def Select(self, i): self.sel = i; self.handlers.get("sel", lambda e: None)(None)
    def Focus(self, i): pass
    def GetFirstSelected(self): return self.sel
class BoxSizer:
    def __init__(self, *a): pass
    def Add(self, *a, **k): pass
    def Fit(self, w): pass
class ProgressDialog(Window):
    def __init__(self, title, msg, maximum=100, parent=None, style=0): super().__init__(); LOG.append(("progress", title)); self.updates = []
    def Update(self, v, msg=""): self.updates.append((v, msg)); return (not getattr(self, "abort", False), False)
class Menu:
    def Append(self, *a): LOG.append(("menu", a[1])); return "item"
    def Remove(self, item): LOG.append("menu removed")
