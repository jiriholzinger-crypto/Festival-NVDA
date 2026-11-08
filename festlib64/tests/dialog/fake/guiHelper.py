import wx
BORDER_FOR_DIALOGS = 10
class BoxSizerHelper:
    def __init__(self, parent, orientation=None): self.parent = parent; self.sizer = wx.BoxSizer()
    def addLabeledControl(self, label, cls, **k): return cls(self.parent, **k)
    def addItem(self, item): return item
class ButtonHelper:
    def __init__(self, o): pass
    def addButton(self, *a, **k): return wx.Button(*a, **k)
