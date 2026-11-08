import threading, time
EVENTS = []
_evlock = threading.Lock()
class _Notifier:
    def __init__(self, name): self.name = name
    def notify(self, **kw):
        with _evlock:
            EVENTS.append((time.monotonic(), self.name, kw.get("index")))
synthIndexReached = _Notifier("index")
synthDoneSpeaking = _Notifier("done")
class VoiceInfo:
    def __init__(self, id, displayName, language=None): self.id = id; self.displayName = displayName; self.language = language
class SynthDriver:
    @staticmethod
    def VoiceSetting(): return "voice"
    @staticmethod
    def RateSetting(): return "rate"
    @staticmethod
    def PitchSetting(): return "pitch"
    @staticmethod
    def VolumeSetting(): return "volume"
    def __init__(self): pass
    def terminate(self): pass
    @property
    def voice(self): return self._get_voice()
    @voice.setter
    def voice(self, v): self._set_voice(v)
    @property
    def pitch(self): return self._get_pitch()
    @pitch.setter
    def pitch(self, v): self._set_pitch(v)
    @property
    def rate(self): return self._get_rate()
    @rate.setter
    def rate(self, v): self._set_rate(v)
