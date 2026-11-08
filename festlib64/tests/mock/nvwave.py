# Stand-in for NVDA's WASAPI WavePlayer: plays in simulated real time (SPEED x faster),
# feed() blocks while more than 0.4 s (simulated) is buffered, onDone fires in order.
import threading, time, ctypes, collections
DEFAULT_DEVICE_KEY = "default"
SPEED = 4.0
PLAYERS = []
class WavePlayer:
    def __init__(self, channels, samplesPerSec, bitsPerSample, outputDevice=DEFAULT_DEVICE_KEY, wantDucking=True, purpose=None):
        self.channels, self.samplesPerSec, self.bits = channels, samplesPerSec, bitsPerSample
        self.creator = threading.current_thread().name
        self.bytesPerSec = channels * samplesPerSec * bitsPerSample // 8
        self.q = collections.deque(); self.cv = threading.Condition(); self.buffered = 0.0
        self.paused = False; self.played = bytearray(); self.fedAfterStop = 0; self.stops = 0
        self.t = threading.Thread(target=self._run, daemon=True); self.t.start()
        PLAYERS.append(self)
    def feed(self, data, size=None, onDone=None):
        if isinstance(data, (bytes, bytearray)):
            buf = bytes(data)
        else:
            buf = ctypes.string_at(data.value, size) if size else b""
        dur = len(buf) / self.bytesPerSec
        with self.cv:
            while self.buffered > 0.4:
                self.cv.wait(0.01)
            self.q.append((buf, dur, onDone)); self.buffered += dur; self.cv.notify_all()
    def _run(self):
        while True:
            with self.cv:
                while not self.q or self.paused:
                    self.cv.wait()
                buf, dur, onDone = self.q[0]
            time.sleep(dur / SPEED)
            with self.cv:
                if not self.q or self.q[0][0] is not buf:
                    continue  # stopped meanwhile
                self.q.popleft(); self.buffered -= dur; self.played += buf; self.cv.notify_all()
            if onDone: onDone()
    def idle(self): self.sync()
    def sync(self):
        with self.cv:
            while self.q: self.cv.wait(0.01)
    def stop(self):
        with self.cv:
            self.q.clear(); self.buffered = 0.0; self.paused = False; self.stops += 1; self.cv.notify_all()
    def pause(self, switch):
        with self.cv:
            self.paused = switch; self.cv.notify_all()
    def close(self): self.stop()
