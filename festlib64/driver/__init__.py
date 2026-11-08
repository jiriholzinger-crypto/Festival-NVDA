# Festival Speech Synthesis System driver for NVDA.
# Copyright (C) 2008-2011 Olga Yakovleva <Yakovleva.O.V@gmail.com>
# Copyright (C) 2026 Jiří Holzinger (port to modern 64-bit NVDA)
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 2 of the License, or (at your option)
# any later version. See COPYING.txt.
#
# Festival (festlib.dll 2.1, 64-bit) runs in the helper process festhost64.exe,
# so a crash inside Festival cannot take NVDA down; the helper is restarted
# automatically. The helper sends the synthesized audio back through a pipe and
# the driver plays it with NVDA's nvwave.WavePlayer, so the NVDA output device,
# audio ducking, pause and leading silence trimming work like with the built-in
# synthesizers.

import collections
import os
import queue
import re
import struct
import subprocess
import threading
import time

import config
import nvwave
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from speech.commands import IndexCommand, PitchCommand
from logHandler import log

import addonHandler

try:
	addonHandler.initTranslation()
except Exception:
	def _(s):
		return s

_DIR = os.path.dirname(os.path.abspath(__file__))
_DLL = os.path.join(_DIR, "festlib.dll")
_HOST = os.path.join(_DIR, "festhost64.exe")
_CRASH_LOG = os.path.join(_DIR, "festhost-crash.log")
# give up restarting the helper after this many crashes within _CRASH_WINDOW s
_MAX_CRASHES = 3
_CRASH_WINDOW = 60.0

# festlib synthesises short texts in one piece; short pieces start speaking
# sooner and every piece carries the NVDA indexes that precede it.
_MAX_PIECE = 140
# audio is fed to the player in slices of this many milliseconds, so that
# cancel() never lets more than one slice through
_FEED_SLICE_MS = 100

# default voice per language when NVDA has no saved Festival voice yet
_PREFERRED_VOICES = ("czech_ph", "kal_diphone")

_RE_SPACE = re.compile(r"\s+")
_RE_SENTENCE = re.compile(r"(?<=[.!?;:])\s+")
_RE_HAS_WORD = re.compile(r"\w")


def _hardSplit(text: str):
	"""Split a too long sentence at commas, then spaces, then anywhere."""
	if len(text) <= _MAX_PIECE:
		return [text]
	for sep in (", ", " "):
		parts = text.split(sep)
		if len(parts) > 1:
			out, cur = [], ""
			for p in parts:
				cand = cur + sep + p if cur else p
				if len(cand) > _MAX_PIECE and cur:
					out.append(cur + (sep.strip() if sep != " " else ""))
					cur = p
				else:
					cur = cand
			if cur:
				out.append(cur)
			res = []
			for o in out:
				res.extend(_hardSplit(o) if len(o) > _MAX_PIECE else [o])
			return res
	return [text[i:i + _MAX_PIECE] for i in range(0, len(text), _MAX_PIECE)]


def splitText(text: str):
	text = _RE_SPACE.sub(" ", text).strip()
	if not text:
		return []
	if len(text) <= _MAX_PIECE:
		return [text]
	out, cur = [], ""
	for sentence in _RE_SENTENCE.split(text):
		for part in _hardSplit(sentence):
			if cur and len(cur) + 1 + len(part) > _MAX_PIECE:
				out.append(cur)
				cur = part
			else:
				cur = cur + " " + part if cur else part
	if cur:
		out.append(cur)
	return out


def _outputDevice():
	for section in ("audio", "speech"):
		try:
			value = config.conf[section]["outputDevice"]
		except KeyError:
			continue
		if value:
			return value
	return nvwave.DEFAULT_DEVICE_KEY


def storeDir():
	"""Folder for downloaded voices and dictionaries, in NVDA's user
	configuration so that they survive add-on updates."""
	try:
		import globalVars
		base = globalVars.appArgs.configPath
	except Exception:
		base = os.path.join(os.environ.get("APPDATA", _DIR), "nvda")
	return os.path.join(base, "festivalVoices")


def voiceStoreDir():
	"""Downloaded voices: <language>/<voice>/."""
	return os.path.join(storeDir(), "voices")


def dictStoreDir():
	"""Downloaded lexicons, e.g. oald/."""
	return os.path.join(storeDir(), "dicts")


class _HostDied(Exception):
	pass


class _Piece:
	__slots__ = ("indexes", "last", "tail", "gen", "started")

	def __init__(self, indexes, last, tail, gen):
		self.indexes = indexes  # NVDA indexes reached when this piece starts
		self.last = last  # last piece of a speech sequence
		self.tail = tail  # NVDA indexes after the last piece
		self.gen = gen
		self.started = False


class _Host:
	"""festhost64.exe and the thread reading its output."""

	def __init__(self, onAudio, onEnd, onExit):
		startupInfo = subprocess.STARTUPINFO()
		startupInfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
		startupInfo.wShowWindow = 0
		self.proc = subprocess.Popen(
			[_HOST],
			stdin=subprocess.PIPE,
			stdout=subprocess.PIPE,
			stderr=subprocess.DEVNULL,
			cwd=_DIR,
			startupinfo=startupInfo,
			creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000),
			bufsize=0,
			env=self._environment(),
		)
		self.alive = True
		self._writeLock = threading.Lock()
		self._requestLock = threading.Lock()
		self._replies = queue.Queue()
		self._onAudio, self._onEnd, self._onExit = onAudio, onEnd, onExit
		self._reader = threading.Thread(target=self._readLoop, name="festival-reader", daemon=True)
		self._reader.start()

	@staticmethod
	def _environment():
		env = dict(os.environ)
		for key, path in (("FESTLIB_VOICE_PATH", voiceStoreDir()), ("FESTLIB_DICT_PATH", dictStoreDir())):
			if os.path.isdir(path):
				env[key] = path
			else:
				env.pop(key, None)
		return env

	def send(self, line: str):
		line = line.replace("\r", " ").replace("\n", " ")
		with self._writeLock:
			try:
				self.proc.stdin.write((line + "\n").encode("utf-8", "replace"))
				self.proc.stdin.flush()
			except (OSError, ValueError):
				self.alive = False
				raise _HostDied()

	def request(self, line: str, timeout: float = 10) -> str:
		with self._requestLock:
			self.send(line)
			try:
				reply = self._replies.get(timeout=timeout)
			except queue.Empty:
				raise RuntimeError("festival host did not answer %r" % line[:1])
			if reply is None:
				raise _HostDied()
			return reply

	def _readExact(self, stream, n):
		data = bytearray()
		while len(data) < n:
			chunk = stream.read(n - len(data))
			if not chunk:
				return None
			data += chunk
		return bytes(data)

	def _readLoop(self):
		stream = self.proc.stdout
		try:
			while True:
				hdr = self._readExact(stream, 5)
				if hdr is None:
					break
				length, kind = struct.unpack("<IB", hdr)
				payload = self._readExact(stream, length) if length else b""
				if payload is None:
					break
				if kind == 0x61:  # 'a'
					bookmark, rate, channels = struct.unpack_from("<iii", payload)
					self._onAudio(bookmark, rate, channels, payload[12:])
				elif kind == 0x65:  # 'e'
					self._onEnd(struct.unpack_from("<i", payload)[0])
				elif kind == 0x72:  # 'r'
					self._replies.put(payload.decode("utf-8", "replace"))
		except Exception:
			log.error("festival: reading from festhost64", exc_info=True)
		self.alive = False
		self._replies.put(None)
		self._onExit(self)

	def close(self):
		self.alive = False
		try:
			self.send("X")
			self.send("Q")
		except _HostDied:
			pass
		try:
			self.proc.wait(timeout=3)
		except Exception:
			try:
				self.proc.kill()
			except Exception:
				pass
		for stream in (self.proc.stdin, self.proc.stdout):
			try:
				stream.close()
			except Exception:
				pass


class SynthDriver(BaseSynthDriver):
	name = "festival"
	# Translators: name of the synthesizer in the Select Synthesizer dialog.
	description = _("Festival Speech Synthesis System")

	supportedSettings = (
		BaseSynthDriver.VoiceSetting(),
		BaseSynthDriver.RateSetting(),
		BaseSynthDriver.PitchSetting(),
		BaseSynthDriver.VolumeSetting(),
	)
	supportedCommands = {IndexCommand, PitchCommand}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	@classmethod
	def check(cls):
		return os.name == "nt" and os.path.isfile(_HOST) and os.path.isfile(_DLL)

	def __init__(self):
		super().__init__()
		self._lock = threading.Lock()
		self._gen = 0  # incremented by cancel()
		self._pending = {}  # bookmark -> _Piece
		self._nextId = 1
		self._player = None
		self._playerFormat = None
		self._playerLock = threading.Lock()
		self._audio = queue.Queue()  # work for the player thread
		self._terminating = False
		self._crashes = collections.deque()
		self._rate = 50
		self._pitch = 50
		self._volume = 100
		self._voice = None
		self._sentPitch = 50
		self._host = None
		self._startHost()
		self._playerThread = threading.Thread(target=self._playLoop, name="festival-player", daemon=True)
		self._playerThread.start()

	# --- helper process ---

	def _startHost(self):
		host = _Host(self._onAudio, self._onEnd, self._onHostExit)
		try:
			res = host.request("I " + _DIR, timeout=120)
			if res != "0":
				raise RuntimeError("cannot initialize Festival (code %s), see festival.log" % res)
			count = int(host.request("N"))
			names = [host.request("n %d" % i) for i in range(count)]
			voices = [n for n in names if n]
			if not voices:
				raise RuntimeError("Festival has no voices")
			current = host.request("C")
		except Exception:
			host.close()
			raise
		self._voiceNames = voices
		if self._voice not in voices:
			# first start (NVDA has no saved voice yet) or the voice is gone
			self._voice = self._defaultVoice(voices) or current or voices[0]
		if self._voice != current:
			host.send("v " + self._voice)
		host.send("R %d" % self._rate)
		host.send("T %d" % self._pitch)
		host.send("L %d" % self._volume)
		self._sentPitch = self._pitch
		self._host = host

	def _onHostExit(self, host):
		if self._terminating or host is not self._host:
			return
		log.error("festival: festhost64.exe stopped unexpectedly%s" % self._crashInfo())
		self._crashes.append(time.monotonic())
		# Audio that already arrived still plays; the pieces whose synthesis
		# was lost are released after it, so NVDA is not left waiting.
		with self._lock:
			pieces = [p for p in self._pending.values() if p.gen == self._gen]
			self._pending.clear()
		self._audio.put(("dead", pieces))

	@staticmethod
	def _crashInfo():
		try:
			with open(_CRASH_LOG, "r", encoding="utf-8", errors="replace") as f:
				lines = f.read().splitlines()
			return ": " + lines[-1] if lines else ""
		except OSError:
			return ""

	def _ensureHost(self):
		if self._host is not None and self._host.alive:
			return True
		now = time.monotonic()
		while self._crashes and now - self._crashes[0] > _CRASH_WINDOW:
			self._crashes.popleft()
		if len(self._crashes) >= _MAX_CRASHES:
			return False
		old, self._host = self._host, None
		if old is not None:
			old.close()
		try:
			self._startHost()
			log.info("festival: festhost64.exe restarted")
			return True
		except Exception:
			log.error("festival: cannot restart festhost64.exe", exc_info=True)
			self._crashes.append(now)
			return False

	def _send(self, line):
		host = self._host
		if host is None or not host.alive:
			return
		try:
			host.send(line)
		except _HostDied:
			pass

	# --- audio from the helper (reader thread) ---

	def _onAudio(self, bookmark, sampleRate, channels, data):
		with self._lock:
			piece = self._pending.get(bookmark)
			if piece is None or piece.gen != self._gen:
				return
		self._audio.put(("a", piece, sampleRate, channels, data))

	def _onEnd(self, bookmark):
		with self._lock:
			piece = self._pending.pop(bookmark, None)
			if piece is None or piece.gen != self._gen:
				return
		self._audio.put(("e", piece))

	# --- player thread ---

	def _playLoop(self):
		while True:
			item = self._audio.get()
			if item is None:
				break
			try:
				if item[0] == "a":
					self._playAudio(*item[1:])
				elif item[0] == "e":
					self._playEnd(item[1])
				elif item[0] == "dead":
					self._releaseDead(item[1])
			except Exception:
				log.error("festival: player thread", exc_info=True)

	def _getPlayer(self, channels, sampleRate):
		fmt = (channels, sampleRate)
		with self._playerLock:
			if self._player is not None and self._playerFormat == fmt:
				return self._player
			old = self._player
		if old is not None:
			# another voice with another sample rate: let the old audio finish
			old.idle()
		player = nvwave.WavePlayer(
			channels=channels,
			samplesPerSec=sampleRate,
			bitsPerSample=16,
			outputDevice=_outputDevice(),
		)
		with self._playerLock:
			self._player = player
			self._playerFormat = fmt
			if old is not None:
				# keep the old player alive until here; it is idle now
				old.close()
		return player

	def _currentPlayer(self):
		with self._playerLock:
			return self._player

	def _playAudio(self, piece, sampleRate, channels, data):
		if piece.gen != self._gen:
			return
		player = self._getPlayer(channels, sampleRate)
		if not piece.started:
			piece.started = True
			self._feedIndexes(player, piece.indexes, piece.gen)
		blockAlign = 2 * channels
		step = max(blockAlign, sampleRate * _FEED_SLICE_MS // 1000 * blockAlign)
		view = memoryview(data)
		for pos in range(0, len(data), step):
			if piece.gen != self._gen:
				return
			player.feed(bytes(view[pos:pos + step]))

	def _playEnd(self, piece):
		if piece.gen != self._gen:
			return
		player = self._currentPlayer()
		if player is None:
			# nothing was ever played (e.g. the first text had no words)
			self._notify(piece.indexes + (piece.tail if piece.last else []), piece.gen, piece.last)
			return
		if not piece.started:
			self._feedIndexes(player, piece.indexes, piece.gen)
		if piece.last:
			tail, gen = piece.tail, piece.gen
			player.feed(b"", onDone=lambda: self._notify(tail, gen, True))

	def _releaseDead(self, pieces):
		for piece in pieces:
			if piece.gen != self._gen:
				continue
			indexes = list(piece.indexes) if not piece.started else []
			if piece.last:
				indexes.extend(piece.tail)
			player = self._currentPlayer()
			if player is None:
				self._notify(indexes, piece.gen, piece.last)
			elif indexes or piece.last:
				gen, last = piece.gen, piece.last
				player.feed(b"", onDone=lambda indexes=indexes, gen=gen, last=last: self._notify(indexes, gen, last))

	def _feedIndexes(self, player, indexes, gen):
		if indexes:
			player.feed(b"", onDone=lambda: self._notify(indexes, gen, False))

	def _notify(self, indexes, gen, done):
		if gen != self._gen:
			return
		for index in indexes:
			synthIndexReached.notify(synth=self, index=index)
		if done:
			synthDoneSpeaking.notify(synth=self)

	def _dropQueuedAudio(self):
		try:
			while True:
				item = self._audio.get_nowait()
				if item is not None and item[0] == "dead":
					self._audio.put(item)
					break
		except queue.Empty:
			pass

	# --- SynthDriver API ---

	def speak(self, speechSequence):
		pieces = []  # [text, indexes before, pitch]
		pendingIndexes = []
		texts = []
		pitch = self._pitch

		def flush():
			text = "".join(texts)
			texts.clear()
			for part in splitText(text):
				if _RE_HAS_WORD.search(part):
					pieces.append([part, pendingIndexes[:], pitch])
					pendingIndexes.clear()

		for item in speechSequence:
			if isinstance(item, str):
				texts.append(item)
			elif isinstance(item, IndexCommand):
				flush()
				pendingIndexes.append(item.index)
			elif isinstance(item, PitchCommand):
				flush()
				pitch = max(0, min(100, int(round(self._pitch + item.offset))))
			else:
				log.debugWarning("festival: unsupported speech command %r" % item)
		flush()
		if not pieces:
			# no words, but NVDA still expects the indexes and synthDoneSpeaking
			pieces.append(["", [], self._pitch])
		if not self._ensureHost():
			# Festival keeps crashing: stay silent but do not block NVDA
			allIndexes = [i for _t, idx, _p in pieces for i in idx] + pendingIndexes
			for index in allIndexes:
				synthIndexReached.notify(synth=self, index=index)
			synthDoneSpeaking.notify(synth=self)
			return
		with self._lock:
			gen = self._gen
			for i, (text, indexes, piecePitch) in enumerate(pieces):
				last = i == len(pieces) - 1
				bookmark = self._nextId
				self._nextId = 1 if self._nextId >= 0x7FFFFFF0 else self._nextId + 1
				self._pending[bookmark] = _Piece(indexes, last, pendingIndexes if last else [], gen)
				if piecePitch != self._sentPitch:
					self._send("T %d" % piecePitch)
					self._sentPitch = piecePitch
				self._send("S %d %s" % (bookmark, text))
			if self._sentPitch != self._pitch:
				self._send("T %d" % self._pitch)
				self._sentPitch = self._pitch

	def cancel(self):
		with self._lock:
			self._gen += 1
			self._pending.clear()
		self._send("X")
		self._dropQueuedAudio()
		player = self._currentPlayer()
		if player is not None:
			player.stop()

	def pause(self, switch: bool):
		player = self._currentPlayer()
		if player is not None:
			player.pause(switch)

	def terminate(self):
		self._terminating = True
		self.cancel()
		host, self._host = self._host, None
		if host is not None:
			host.close()
		self._audio.put(None)
		with self._playerLock:
			player, self._player = self._player, None
		if player is not None:
			try:
				player.close()
			except Exception:
				pass
		super().terminate()

	def _get_rate(self):
		return self._rate

	def _set_rate(self, value):
		self._rate = max(0, min(100, int(value)))
		self._send("R %d" % self._rate)

	def _get_pitch(self):
		return self._pitch

	def _set_pitch(self, value):
		self._pitch = max(0, min(100, int(value)))
		with self._lock:
			self._send("T %d" % self._pitch)
			self._sentPitch = self._pitch

	def _get_volume(self):
		return self._volume

	def _set_volume(self, value):
		self._volume = max(0, min(100, int(value)))
		self._send("L %d" % self._volume)

	def _getAvailableVoices(self):
		voices = collections.OrderedDict()
		for name in self._voiceNames:
			voices[name] = VoiceInfo(name, name, self._voiceLanguage(name))
		return voices

	@classmethod
	def _defaultVoice(cls, voices):
		"""Voice for NVDA's interface language; Czech when no voice matches."""
		try:
			import languageHandler
			lang = (languageHandler.getLanguage() or "").split("_")[0].lower()
		except Exception:
			lang = ""
		for wanted in (lang, "cs"):
			candidates = [v for v in voices if cls._voiceLanguage(v) == wanted]
			if candidates:
				for preferred in _PREFERRED_VOICES:
					if preferred in candidates:
						return preferred
				return candidates[0]
		return None

	@staticmethod
	def _voiceLanguage(name: str):
		lower = name.lower()
		if lower.startswith("czech"):
			return "cs"
		if lower.startswith(("msu_ru", "russian")):
			return "ru"
		if lower.endswith(("_diphone", "_arctic_hts", "_multisyn", "_clunits")) or lower.startswith(("kal", "ked", "us_", "cmu_")):
			return "en"
		return None

	def _get_voice(self):
		return self._voice

	def _set_voice(self, name):
		if name not in self._voiceNames:
			raise ValueError("unknown Festival voice %r" % name)
		self._voice = name
		# rate and pitch are kept by festlib.dll when the voice changes
		self._send("v " + name)
