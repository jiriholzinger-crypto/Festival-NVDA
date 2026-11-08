class IndexCommand:
    def __init__(self, index): self.index = index
    def __repr__(self): return "IndexCommand(%d)" % self.index
class PitchCommand:
    def __init__(self, offset=0, multiplier=1): self._offset = offset; self._multiplier = multiplier
    @property
    def offset(self):
        import config
        if self._offset: return self._offset
        d = config.conf["speech"]["festival"]["pitch"]
        return int(d * self._multiplier) - d
class BreakCommand:
    def __init__(self, time=0): self.time = time
