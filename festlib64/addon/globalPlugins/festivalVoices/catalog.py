# Festival voice catalog: list, download, install and remove Festival voices
# from festvox.org for the NVDA Festival synthesizer.
# Copyright (C) 2026 Jiří Holzinger. GNU GPL v2 or later.
#
# This module has no wx or NVDA GUI code, so it can be tested on its own.

import html
import json
import os
import re
import shutil
import tarfile
import tempfile
import urllib.parse
import urllib.request

VOICES_URL = "http://festvox.org/packed/festival/2.5/voices/"
# lexicons that some voices need, outside the voices folder
LEXICONS = {
	# British English voices (rab_diphone) use the OALD lexicon,
	# which is free for non-commercial use only.
	"oald": "http://festvox.org/packed/festival/2.5/festlex_OALD.tar.gz",
}
USER_AGENT = "NVDA-Festival-addon/2026"
TIMEOUT = 30

# what is known about the voices on festvox.org (name -> description)
KNOWN_VOICES = {
	"kal_diphone": "American English, male (diphone)",
	"ked_diphone": "American English, male (diphone)",
	"rab_diphone": "British English, male (diphone, needs the OALD lexicon)",
	"cmu_us_awb_cg": "Scottish English, male (ClusterGen)",
	"cmu_us_bdl_cg": "American English, male (ClusterGen)",
	"cmu_us_clb_cg": "American English, female (ClusterGen)",
	"cmu_us_jmk_cg": "Canadian English, male (ClusterGen)",
	"cmu_us_ksp_cg": "Indian English, male (ClusterGen)",
	"cmu_us_rms_cg": "American English, male (ClusterGen)",
	"cmu_us_slt_cg": "American English, female (ClusterGen)",
}

_RE_LINK = re.compile(r'href="([^"?/]+\.tar\.gz)"', re.IGNORECASE)
_RE_SIZE = re.compile(r"(?<![\w.:-])(\d+(?:\.\d+)?)\s*([KMG])B?(?![\w])", re.IGNORECASE)
_RE_DIPHONE = re.compile(r"^festvox_([a-z]+?)lpc(?:8|16)k$")
_PREFIX = "festival/lib/"


class Cancelled(Exception):
	pass


class Package:
	def __init__(self, fileName, size=None):
		self.fileName = fileName
		self.url = urllib.parse.urljoin(VOICES_URL, fileName)
		self.size = size  # display text, e.g. "69M", or None
		self.voice = guessVoiceName(fileName)

	@property
	def description(self):
		if self.voice in KNOWN_VOICES:
			return KNOWN_VOICES[self.voice]
		if self.voice.startswith("cmu_us_"):
			return "English (CMU voice)"
		return ""

	def __repr__(self):
		return "Package(%r, %r, voice=%r)" % (self.fileName, self.size, self.voice)


def guessVoiceName(fileName):
	"""festvox_kallpc16k.tar.gz -> kal_diphone, festvox_cmu_us_slt_cg.tar.gz -> cmu_us_slt_cg"""
	base = fileName[:-len(".tar.gz")] if fileName.endswith(".tar.gz") else fileName
	m = _RE_DIPHONE.match(base)
	if m:
		return m.group(1) + "_diphone"
	if base.startswith("festvox_"):
		base = base[len("festvox_"):]
	return base


def parseListing(text):
	"""Packages from a web server directory listing (Apache style or plain links)."""
	packages = []
	seen = set()
	for m in _RE_LINK.finditer(text):
		name = html.unescape(urllib.parse.unquote(m.group(1)))
		if not name.startswith("festvox_") or name in seen:
			continue
		seen.add(name)
		# the size follows the link on the same row
		rest = text[m.end():]
		cut = min([i for i in (rest.find("<a "), rest.find("<A "), rest.find("\n"), rest.find("</tr")) if i >= 0] or [len(rest)])
		row = re.sub(r"<[^>]+>", " ", rest[:cut])
		size = None
		sizes = _RE_SIZE.findall(row)
		if sizes:
			number, unit = sizes[-1]
			size = number + unit.upper()
		packages.append(Package(name, size))
	packages.sort(key=lambda p: p.voice)
	return packages


def _open(url):
	request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
	return urllib.request.urlopen(request, timeout=TIMEOUT)


def fetchCatalog(url=None):
	with _open(url or VOICES_URL) as response:
		data = response.read()
		charset = response.headers.get_content_charset() or "utf-8"
	return parseListing(data.decode(charset, "replace"))


def download(url, target, progress=None, cancelled=None):
	"""Download url to the file target. progress(done, total or None)."""
	tmp = target + ".part"
	try:
		with _open(url) as response, open(tmp, "wb") as out:
			total = response.headers.get("Content-Length")
			total = int(total) if total and total.isdigit() else None
			done = 0
			while True:
				if cancelled and cancelled():
					raise Cancelled()
				chunk = response.read(64 * 1024)
				if not chunk:
					break
				out.write(chunk)
				done += len(chunk)
				if progress:
					progress(done, total)
		os.replace(tmp, target)
	finally:
		if os.path.exists(tmp):
			os.remove(tmp)


def _safeParts(name):
	"""Path parts below festival/lib/, or None for anything unsafe or unrelated."""
	name = name.replace("\\", "/")
	while name.startswith("./"):
		name = name[2:]
	if not name.startswith(_PREFIX):
		return None
	parts = [p for p in name[len(_PREFIX):].split("/") if p not in ("", ".")]
	if not parts or any(p == ".." or ":" in p for p in parts):
		return None
	return parts


def installArchive(archive, store, kind="voices", cancelled=None):
	"""Unpack festival/lib/<kind>/... from a festvox archive into store/<kind>/.

	Returns the installed top folders relative to store/<kind>, for voices
	"<language>/<voice>", for lexicons "<name>". Existing ones are replaced."""
	depth = 2 if kind == "voices" else 1
	target = os.path.join(store, kind)
	os.makedirs(target, exist_ok=True)
	staging = tempfile.mkdtemp(prefix=".staging-", dir=store)
	installed = set()
	try:
		with tarfile.open(archive, "r:*") as tar:
			for member in tar:
				if cancelled and cancelled():
					raise Cancelled()
				parts = _safeParts(member.name)
				if not parts or parts[0] != kind or len(parts) < 1 + depth:
					continue
				rel = parts[1:]
				dest = os.path.join(staging, *rel)
				if member.isdir():
					os.makedirs(dest, exist_ok=True)
				elif member.isfile():
					os.makedirs(os.path.dirname(dest), exist_ok=True)
					src = tar.extractfile(member)
					with src, open(dest, "wb") as out:
						shutil.copyfileobj(src, out)
				else:
					continue  # no links or devices
				if len(rel) > depth or member.isfile():
					installed.add("/".join(rel[:depth]))
		if not installed:
			raise ValueError("the archive contains no Festival %s" % kind)
		for top in sorted(installed):
			src = os.path.join(staging, *top.split("/"))
			dst = os.path.join(target, *top.split("/"))
			if os.path.isdir(dst):
				shutil.rmtree(dst)
			elif os.path.exists(dst):
				os.remove(dst)
			os.makedirs(os.path.dirname(dst), exist_ok=True)
			shutil.move(src, dst)
	finally:
		shutil.rmtree(staging, ignore_errors=True)
	return sorted(installed)


def installedVoices(*roots):
	"""{voice name: folder} for voice folders <root>/<language>/<voice>/festvox/<voice>.scm."""
	voices = {}
	for root in roots:
		if not root or not os.path.isdir(root):
			continue
		for language in sorted(os.listdir(root)):
			langDir = os.path.join(root, language)
			if not os.path.isdir(langDir):
				continue
			for voice in sorted(os.listdir(langDir)):
				folder = os.path.join(langDir, voice)
				if os.path.isfile(os.path.join(folder, "festvox", voice + ".scm")):
					voices.setdefault(voice, folder)
	return voices


def neededLexicons(voiceFolder):
	"""Lexicons from LEXICONS that the voice's festvox/*.scm files set up."""
	needed = set()
	festvox = os.path.join(voiceFolder, "festvox")
	try:
		names = os.listdir(festvox)
	except OSError:
		return needed
	for name in names:
		if not name.endswith(".scm"):
			continue
		try:
			with open(os.path.join(festvox, name), "r", encoding="latin-1") as f:
				text = f.read()
		except OSError:
			continue
		for lex in LEXICONS:
			if "setup_%s_lex" % lex in text:
				needed.add(lex)
	return needed


def lexiconInstalled(lex, *dictRoots):
	return any(
		root and os.path.isfile(os.path.join(root, lex, "%slex.scm" % lex))
		for root in dictRoots
	)


def removeVoice(folder, store):
	"""Delete a downloaded voice folder (only inside store)."""
	folder = os.path.abspath(folder)
	store = os.path.abspath(store)
	if os.path.commonpath([folder, store]) != store or folder == store:
		raise ValueError("not a downloaded voice: %s" % folder)
	shutil.rmtree(folder)
	parent = os.path.dirname(folder)
	try:
		if parent != store and not os.listdir(parent):
			os.rmdir(parent)
	except OSError:
		pass


def saveInfo(store, info):
	path = os.path.join(store, "installed.json")
	with open(path + ".tmp", "w", encoding="utf-8") as f:
		json.dump(info, f, indent=1, ensure_ascii=False)
	os.replace(path + ".tmp", path)


def loadInfo(store):
	try:
		with open(os.path.join(store, "installed.json"), "r", encoding="utf-8") as f:
			return json.load(f)
	except (OSError, ValueError):
		return {}
