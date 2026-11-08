# Festival voice downloader for the NVDA Festival synthesizer add-on.
# NVDA menu > Tools > Festival voices...
# Copyright (C) 2026 Jiří Holzinger. GNU GPL v2 or later.

import os
import tempfile
import threading

import wx

import addonHandler
import globalPluginHandler
import globalVars
import gui
import synthDriverHandler
import ui
from gui import guiHelper
from logHandler import log

from . import catalog

addonHandler.initTranslation()

_ADDON_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
_BUILTIN_VOICES = os.path.join(_ADDON_DIR, "synthDrivers", "festival", "festival", "lib", "voices")
_BUILTIN_DICTS = os.path.join(_ADDON_DIR, "synthDrivers", "festival", "festival", "lib", "dicts")


def _store():
	# keep in step with storeDir() in synthDrivers/festival/__init__.py
	return os.path.join(globalVars.appArgs.configPath, "festivalVoices")


def _errorText(error):
	"""Readable text for download errors (urllib wraps the real reason)."""
	reason = getattr(error, "reason", None)
	if reason is not None:
		error = reason
	return getattr(error, "strerror", None) or str(error)


def _festivalActive():
	synth = synthDriverHandler.getSynth()
	return synth is not None and synth.name == "festival"


def _onMainThread(func, *args):
	"""Run func on the GUI thread and wait for it (from a background thread)."""
	done = threading.Event()
	result = {}

	def call():
		try:
			result["value"] = func(*args)
		except Exception as e:
			result["error"] = e
		finally:
			done.set()

	wx.CallAfter(call)
	done.wait()
	if "error" in result:
		raise result["error"]
	return result.get("value")


def _pauseFestival():
	"""Stop Festival while voice files change (Windows cannot replace files the
	helper has open). NVDA's silence synthesizer is used meanwhile as a
	fallback, so the configuration keeps Festival. Returns True if paused."""
	if not _festivalActive():
		return False
	synthDriverHandler.setSynth("silence", isFallback=True)
	return True


def _resumeFestival(paused):
	"""Start Festival again, so it reads the new voice list."""
	if paused:
		synthDriverHandler.setSynth("festival")


class VoicesDialog(wx.Dialog):
	_instance = None

	def __init__(self, parent):
		# Translators: title of the Festival voice downloader dialog.
		super().__init__(parent, title=_("Festival voices"))
		VoicesDialog._instance = self
		self._packages = []
		self._busy = False
		mainSizer = wx.BoxSizer(wx.VERTICAL)
		sHelper = guiHelper.BoxSizerHelper(self, orientation=wx.VERTICAL)
		# Translators: label of the list of Festival voices.
		self.list = sHelper.addLabeledControl(
			_("&Voices on festvox.org:"),
			wx.ListCtrl,
			style=wx.LC_REPORT | wx.LC_SINGLE_SEL,
			size=(640, 300),
		)
		# Translators: column headers in the Festival voice list.
		for i, (title, width) in enumerate(((_("Voice"), 160), (_("Description"), 290), (_("Size"), 60), (_("Status"), 120))):
			self.list.InsertColumn(i, title, width=width)
		self.list.Bind(wx.EVT_LIST_ITEM_SELECTED, lambda evt: self._updateButtons())
		self.list.Bind(wx.EVT_LIST_ITEM_DESELECTED, lambda evt: self._updateButtons())
		self.status = sHelper.addItem(wx.StaticText(self, label=""))
		buttons = guiHelper.ButtonHelper(wx.HORIZONTAL)
		# Translators: buttons of the Festival voice downloader.
		self.installButton = buttons.addButton(self, label=_("&Install"))
		self.removeButton = buttons.addButton(self, label=_("&Remove"))
		self.refreshButton = buttons.addButton(self, label=_("Re&fresh list"))
		self.folderButton = buttons.addButton(self, label=_("Open voice &folder"))
		closeButton = buttons.addButton(self, id=wx.ID_CLOSE, label=_("&Close"))
		sHelper.addItem(buttons)
		self.installButton.Bind(wx.EVT_BUTTON, self.onInstall)
		self.removeButton.Bind(wx.EVT_BUTTON, self.onRemove)
		self.refreshButton.Bind(wx.EVT_BUTTON, lambda evt: self.loadCatalog())
		self.folderButton.Bind(wx.EVT_BUTTON, self.onOpenFolder)
		closeButton.Bind(wx.EVT_BUTTON, lambda evt: self.Close())
		self.Bind(wx.EVT_CLOSE, self.onClose)
		self.EscapeId = wx.ID_CLOSE
		mainSizer.Add(sHelper.sizer, border=guiHelper.BORDER_FOR_DIALOGS, flag=wx.ALL | wx.EXPAND)
		self.SetSizer(mainSizer)
		mainSizer.Fit(self)
		self.CentreOnScreen()
		self._updateButtons()
		self.loadCatalog()

	# --- state ---

	def _installed(self):
		store = _store()
		return (
			catalog.installedVoices(_BUILTIN_VOICES),
			catalog.installedVoices(os.path.join(store, "voices")),
		)

	def _setStatus(self, text, speak=True):
		self.status.SetLabel(text)
		self.Layout()
		if text and speak:
			# a changed static text is not read automatically
			ui.message(text)

	def _selected(self):
		index = self.list.GetFirstSelected()
		if index < 0 or index >= len(self._packages):
			return None
		return self._packages[index]

	def _updateButtons(self):
		package = self._selected()
		builtin, downloaded = self._installed()
		self.installButton.Enable(bool(package) and not self._busy and package.voice not in builtin)
		self.removeButton.Enable(bool(package) and not self._busy and package.voice in downloaded)
		self.refreshButton.Enable(not self._busy)

	def _fillList(self, keepSelection=None):
		builtin, downloaded = self._installed()
		previous = self._selected()
		if keepSelection is None and previous is not None:
			keepSelection = previous.fileName
		self.list.DeleteAllItems()
		select = 0
		for i, package in enumerate(self._packages):
			if package.voice in builtin:
				# Translators: status of a voice that comes with the add-on.
				status = _("included")
			elif package.voice in downloaded:
				# Translators: status of a downloaded voice.
				status = _("installed")
			else:
				status = ""
			description = _(package.description) if package.description else ""
			self.list.Append((package.voice, description, package.size or "", status))
			if package.fileName == keepSelection:
				select = i
		if self._packages:
			self.list.Select(select)
			self.list.Focus(select)
		self._updateButtons()

	# --- catalog ---

	def loadCatalog(self):
		self._busy = True
		self._updateButtons()
		# Translators: shown while the voice list is downloaded.
		self._setStatus(_("Loading the voice list from festvox.org..."))

		def run():
			try:
				packages = catalog.fetchCatalog()
				error = None
			except Exception as e:
				log.debugWarning("festival voices: cannot load the catalog", exc_info=True)
				packages, error = [], e
			wx.CallAfter(self._onCatalog, packages, error)

		threading.Thread(target=run, name="festival-voices-catalog", daemon=True).start()

	def _onCatalog(self, packages, error):
		if not self:
			return
		self._busy = False
		self._packages = packages
		self._fillList()
		if error is not None:
			# Translators: the voice list could not be downloaded.
			self._setStatus(_("Cannot load the voice list: {error}").format(error=_errorText(error)))
		else:
			# Translators: number of voices in the list.
			self._setStatus(_("{count} voices available.").format(count=len(packages)))
		self.list.SetFocus()

	# --- install ---

	def onInstall(self, evt):
		package = self._selected()
		if not package or self._busy:
			return
		self._runInstall(package.fileName, package.url, package.voice, "voices")

	def _runInstall(self, fileName, url, title, kind, then=None):
		self._busy = True
		self._updateButtons()
		cancel = threading.Event()
		progress = wx.ProgressDialog(
			# Translators: title of the download progress dialog.
			_("Downloading {name}").format(name=title),
			# Translators: shown while a voice is downloaded.
			_("Downloading {name}...").format(name=title),
			maximum=100,
			parent=self,
			style=wx.PD_APP_MODAL | wx.PD_CAN_ABORT | wx.PD_AUTO_HIDE | wx.PD_ELAPSED_TIME,
		)
		state = {"last": -1}

		def onProgress(done, total):
			if total:
				percent = min(99, done * 100 // total)
				if percent != state["last"]:
					state["last"] = percent
					wx.CallAfter(update, percent, done, total)

		def update(percent, done, total):
			if not progress:
				return
			# Translators: download progress, in megabytes.
			message = _("{done:.1f} of {total:.1f} MB").format(done=done / 1048576, total=total / 1048576)
			cont, _skip = progress.Update(percent, message)
			if not cont:
				cancel.set()

		def run():
			store = _store()
			os.makedirs(store, exist_ok=True)
			archive = os.path.join(tempfile.gettempdir(), "nvda-" + fileName)
			installed = error = None
			try:
				catalog.download(url, archive, onProgress, cancel.is_set)
				# Translators: shown while a downloaded voice is unpacked.
				wx.CallAfter(lambda: progress and progress.Update(99, _("Unpacking...")))
				paused = _onMainThread(_pauseFestival)
				try:
					installed = catalog.installArchive(archive, store, kind, cancel.is_set)
				finally:
					_onMainThread(_resumeFestival, paused)
				info = catalog.loadInfo(store)
				info[fileName] = installed
				catalog.saveInfo(store, info)
			except catalog.Cancelled:
				installed = None
			except Exception as e:
				log.error("festival voices: cannot install %s" % url, exc_info=True)
				error = e
			finally:
				try:
					os.remove(archive)
				except OSError:
					pass
			wx.CallAfter(self._installDone, progress, installed, error, kind, fileName if kind == "voices" else None, then)

		threading.Thread(target=run, name="festival-voices-download", daemon=True).start()

	def _installDone(self, progress, installed, error, kind, fileName=None, then=None):
		if progress:
			progress.Destroy()
		if not self:
			return
		self._busy = False
		self._fillList(keepSelection=fileName)
		if error is not None:
			# Translators: a voice could not be installed.
			gui.messageBox(_("Installation failed: {error}").format(error=_errorText(error)), _("Festival voices"), wx.OK | wx.ICON_ERROR, self)
			self._setStatus("")
			return
		if not installed:
			# Translators: the user cancelled a download.
			self._setStatus(_("Cancelled."))
			return
		if then:
			then()
			return
		names = ", ".join(top.split("/")[-1] for top in installed)
		# Translators: a voice was installed.
		self._setStatus(_("Installed: {names}").format(names=names))
		self._offerLexicons(installed)

	def _offerLexicons(self, installed):
		store = _store()
		missing = set()
		for top in installed:
			folder = os.path.join(store, "voices", *top.split("/"))
			for lex in catalog.neededLexicons(folder):
				if not catalog.lexiconInstalled(lex, _BUILTIN_DICTS, os.path.join(store, "dicts")):
					missing.add(lex)
		for lex in sorted(missing):
			if lex == "oald":
				# Translators: asks whether to download the OALD lexicon.
				question = _(
					"This voice needs the OALD pronunciation dictionary. "
					"Its licence allows non-commercial use only. Download it now?"
				)
			else:
				# Translators: asks whether to download a lexicon.
				question = _("This voice needs the {name} dictionary. Download it now?").format(name=lex)
			if gui.messageBox(question, _("Festival voices"), wx.YES_NO | wx.ICON_QUESTION, self) == wx.YES:
				url = catalog.LEXICONS[lex]
				self._runInstall(url.rsplit("/", 1)[-1], url, lex.upper(), "dicts", then=self._lexiconDone)
				return
		self._announceReady()

	def _lexiconDone(self):
		self._announceReady()

	def _announceReady(self):
		if _festivalActive():
			# Translators: where to choose a newly installed voice.
			text = _("Choose the new voice in NVDA's voice settings (NVDA+Control+V).")
		else:
			# Translators: the new voice can be used after selecting Festival.
			text = _("Select Festival as the synthesizer to use the new voice.")
		self._setStatus(self.status.GetLabel() + " " + text)

	# --- remove ---

	def onRemove(self, evt):
		package = self._selected()
		if not package or self._busy:
			return
		store = _store()
		downloaded = catalog.installedVoices(os.path.join(store, "voices"))
		folder = downloaded.get(package.voice)
		if not folder:
			return
		# Translators: confirmation before removing a voice.
		if gui.messageBox(
			_("Remove the voice {name}?").format(name=package.voice),
			_("Festival voices"),
			wx.YES_NO | wx.ICON_QUESTION,
			self,
		) != wx.YES:
			return
		paused = _pauseFestival()
		try:
			try:
				catalog.removeVoice(folder, os.path.join(store, "voices"))
			finally:
				_resumeFestival(paused)
			info = catalog.loadInfo(store)
			info.pop(package.fileName, None)
			catalog.saveInfo(store, info)
			# Translators: a voice was removed.
			self._setStatus(_("Removed: {name}").format(name=package.voice))
		except Exception as e:
			log.error("festival voices: cannot remove %s" % folder, exc_info=True)
			gui.messageBox(_("Cannot remove the voice: {error}").format(error=_errorText(e)), _("Festival voices"), wx.OK | wx.ICON_ERROR, self)
		self._fillList(keepSelection=package.fileName)

	def onOpenFolder(self, evt):
		folder = os.path.join(_store(), "voices")
		os.makedirs(folder, exist_ok=True)
		os.startfile(folder)

	def onClose(self, evt):
		if self._busy and evt.CanVeto():
			# Translators: the dialog cannot close during a download.
			self._setStatus(_("Please wait until the download is finished, or cancel it."))
			evt.Veto()
			return
		VoicesDialog._instance = None
		self.Destroy()


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
	def __init__(self):
		super().__init__()
		if globalVars.appArgs.secure:
			return
		self._menuItem = gui.mainFrame.sysTrayIcon.toolsMenu.Append(
			wx.ID_ANY,
			# Translators: item in the NVDA Tools menu.
			_("&Festival voices..."),
			# Translators: help text of the menu item.
			_("Download more voices for the Festival synthesizer"),
		)
		gui.mainFrame.sysTrayIcon.Bind(wx.EVT_MENU, self.onMenu, self._menuItem)

	def onMenu(self, evt):
		if VoicesDialog._instance:
			VoicesDialog._instance.Raise()
			return
		gui.mainFrame.prePopup()
		VoicesDialog(gui.mainFrame).Show()
		gui.mainFrame.postPopup()

	def terminate(self):
		item = getattr(self, "_menuItem", None)
		if item is not None:
			try:
				gui.mainFrame.sysTrayIcon.toolsMenu.Remove(item)
			except Exception:
				pass
		super().terminate()
