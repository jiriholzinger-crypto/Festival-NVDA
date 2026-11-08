# festlib 2.1 – 64-bit Festival engine for the NVDA Festival synthDriver

`festlib.dll` is Olga Yakovleva's festlib (2008) ported to Windows x64 for
64-bit NVDA (2026.1 and later), with a new **pitch** setting. It contains
Festival 2.5.1 and Edinburgh Speech Tools 2.5.1, statically linked.

## Installation

In the add-on's synthDriver directory (`synthDrivers\festival\`):

| file | action |
|---|---|
| `__init__.py` | replace with `driver\__init__.py` (new driver, loads the DLL directly) |
| `festlib.dll` | replace with the 64-bit one |
| `festival\lib\festlib_helper.scm` | replace – **required**, it implements the pitch |
| `festhost.exe` and the old 32-bit DLL | delete, no longer used |

Everything else (the `festival\lib` directory, voices, `festsettings.xml`) stays
as it is. Set `minimumNVDAVersion = 2026.1` in the add-on manifest: the
DLL is 64-bit, so the driver only appears in 64-bit NVDA. The DLL depends only on Windows system DLLs (Universal CRT,
Windows 10 or later).

## API

All functions are `extern "C"` (x64 has one calling convention). See `src/api.hpp`.

Unchanged from the original:
`festlib_initialize(path)`, `festlib_say_text(text, bookmark)`, `festlib_stop()`,
`festlib_set_audio_device(id)`, `festlib_set_volume/get_volume`,
`festlib_get_bookmark()`, `festlib_set_rate/get_rate`,
`festlib_get_voice_count()`, `festlib_get_voice_name(i)`,
`festlib_get_current_voice_name()`, `festlib_set_voice(name)`.

New:

| function | meaning |
|---|---|
| `void festlib_set_pitch(int)` / `int festlib_get_pitch()` | 0–100, 50 = the voice's own pitch, 0 / 100 = one octave down / up |
| `int festlib_is_speaking()` | 1 while text is queued, synthesized or playing (for `synthDoneSpeaking`) |
| `void festlib_terminate()` | stops speech and closes the audio device (call from the driver's `terminate`) |
| `int festlib_get_version()` | `0x00020100` |
| `void festlib_set_audio_callback(cb)` | the caller plays the audio (see below) |

### Audio callback (2.1)

```c
typedef int (__cdecl *festlib_audio_callback)(const short* samples, int num_frames,
                                              int channels, int sample_rate, int bookmark);
```

With a callback set, festlib opens no audio device. The callback runs on the
synthesis thread for every piece of audio of a text (with the bookmark given to
`festlib_say_text`), and once more with `samples = NULL, num_frames = 0` when
the text is finished – also when it produced no sound. Return non-zero to abort
the rest of the text. After `festlib_stop()` no more audio or end marker comes
for the cancelled texts. The synthesis thread initializes COM (multithreaded),
so the callback may open audio devices. `NULL` switches back to the built-in
waveOut player.

The pitch works for every voice: the F0 targets of each utterance are scaled
just before waveform synthesis (diphone, MultiSyn and clunits voices, including
the Czech voices), and HTS voices get the hts_engine half-tone shift (`-fm`).
Pitch and rate are kept when the voice changes (the old DLL reset the rate to 50).
Rate 50 is the voice's own tempo (kal_diphone and ked_diphone set their own
`Duration_Stretch 1.1`, which the old mapping overrode), 0 is half speed and
100 double speed, exponential in between (the old mapping went from 10x slower
at 0 to 2x faster at 100).

## The NVDA driver (`driver/__init__.py`) and `festhost64.exe`

Festival runs in the 64-bit helper process `festhost64.exe` (`host/festhost64.cpp`),
which loads `festlib.dll` in callback mode and sends the audio to the driver
through a pipe (frames: `[uint32 length][type][payload]`, see the source). A crash
inside Festival only ends the helper: the driver releases what NVDA is waiting
for, restarts the helper with the next speech (at most 3 times a minute) and the
helper writes the crash address to `festhost-crash.log`. The driver plays the
audio with NVDA's own `nvwave.WavePlayer` from its own thread, like the built-in
eSpeak and OneCore drivers. So:

- the NVDA output device, audio ducking, leading silence trimming and pause work as with built-in voices,
- index reporting is exact (an index fires when the audio before it has played), no more timeouts for pieces without sound,
- new **Pitch** setting in the voice settings and the synth settings ring,
- **capital letter pitch change** works (`PitchCommand` is supported, only for the marked piece),
- voices with different sample rates (English 16 kHz, Czech 32 kHz) switch players automatically,
- the text splitting, voice list and voice languages are the same as before.

## Voice downloader (`addon/globalPlugins/festivalVoices`)

NVDA menu > Tools > Festival voices lists the packages in
http://festvox.org/packed/festival/2.5/voices/ (read from the directory listing at
runtime), downloads and unpacks the chosen one and restarts Festival.

- Voices go to `<NVDA config>\festivalVoices\voices\<language>\<voice>`, lexicons
  to `...\festivalVoices\dicts` – outside the add-on, so updates keep them.
- The driver passes these folders to `festhost64.exe` as `FESTLIB_VOICE_PATH` and
  `FESTLIB_DICT_PATH`; `festlib_helper.scm` registers the extra voices and points
  `setup_oald_lex` at a downloaded OALD lexicon (needed by rab_diphone).
- Only `festival/lib/voices/...` (or `.../dicts/...`) members are unpacked; paths
  with `..`, absolute paths, links and devices are skipped.
- While files change, NVDA's silence synthesizer is used as a fallback (not saved
  to the configuration), because Windows cannot replace files the helper has open.
- `catalog.py` has no GUI code; `tests/catalog/test_catalog.py` tests it, and
  `tests/dialog/run.py` runs the dialog logic with fake wx/NVDA modules.



**festlib**
- Boost (thread, signals, regex, format, smart_ptr), thread_safe_signals and
  TinyXML replaced by the C++17 standard library and TinyXML-2 (bundled).
- Cancelling is done with generation counters instead of signal/slot connections.
- New audio player: same waveOut output, up to three buffers queued so the parts
  of long texts play without gaps; the device falls back to the default one if
  the selected device disappears.
- The TTS thread gets a 16 MB stack (Festival/SIOD recurse deeply).
- No more exceptions thrown through the C API (`festlib_set_voice` with an unknown
  voice used to crash the caller).
- `festsettings.xml` is read exactly as before (`cp` attribute and `replace`
  rules). Rules are now `std::wregex` (ECMAScript); `$1` in `<with>` works as
  before; Perl-only syntax such as look-behind is skipped with a note in festival.log.

**Speech Tools 2.5.1** (`patches_speech_tools-2.5.1_win64.diff`), Win64 (LLP64, `long` is 32 bit) and MinGW:
- pointer → `long` casts replaced (new `EST_String::Number(long long)`),
- MSVC-only workarounds (`isnan`, `_finite`, `LPCVOID`, `ssize_t`, `read` macro) limited to old MSVC,
- `siod/io.h` renamed to `siod_io.h` (it hid the system `<io.h>`),
- missing `editline_history_file` when built without editline, OpenMP made optional.

**Festival 2.5.1** (`patches_festival-2.5.1_win64.diff`):
- `/dev/null` → `NUL` for debug output on Windows (the old code wrote to a NULL `FILE*`),
- HTS audio callback uses `DWORD_PTR` (64-bit pointer truncation),
- the us_mapping.cc pitchmark bounds fix from Olga's original patch2 (only half of it was upstream),
- HTS `-fm` half-tone parameter for pitch,
- `CMakeLists.txt` for building libFestival without the Unix make system.

## Building

`build.sh` does everything (tested: cross-compiling on Linux; the result is
byte-identical on a clean rebuild). Tools: llvm-mingw 20251104, meson, cmake, ninja.

```
ROOT/festival       your Festival 2.5.1 tree
ROOT/speech_tools   git clone https://github.com/festvox/speech_tools  (commit 63ff019)
ROOT/festlib64      this directory
./festlib64/build.sh /path/to/llvm-mingw
```

The same works on Windows with the Windows build of llvm-mingw in an MSYS2 or
Git-Bash shell. CMake files are written to also accept MSVC x64, but that path
has not been tried.

`tests/test_dll.cpp` is a console test: it loads the DLL, lists voices, speaks
in English and Czech at pitch 20/50/80, tests long text, stop and voice change.
`tests/test_driver.py` with `tests/mock/` runs the NVDA driver with stand-in
NVDA modules (simulated real-time WavePlayer).

## Testing done

Under Wine 10 (x64) with captured audio output:
- initialization (~0.1 s), 6 voices listed (czech_dita, czech_krb, czech_machac,
  czech_ph, kal_diphone, ked_diphone), voice switching, rate, bookmarks,
  `is_speaking`, stop during a long text, `festsettings.xml` rules.
- measured pitch, median F0 at pitch 20 / 50 / 80:
  kal_diphone 66 / 99 / 151 Hz, czech_machac 49 / 75 / 113 Hz,
  czech_dita 146 / 219 / 337 Hz (expected ratio 0.66 / 1 / 1.52).

Driver, with 64-bit Windows Python 3.13 under Wine and stand-in NVDA modules:
indexes and synthDoneSpeaking in order (English and Czech), a sequence without
words, cancel during a long text (no further indexes) and speaking again, pause
and resume, capital pitch (F0 118 → 160 Hz only for the marked piece), the pitch
setting, terminate and create the driver again.

Not tested yet: real Windows and NVDA, HTS voices (pitch `-fm` path), MSVC build.

## License

GNU GPL version 2 or later (festlib). Festival and Speech Tools: X11-style
licence (see their COPYING files). TinyXML-2: zlib licence.

Copyright 2008 Olga Yakovleva; 64-bit port and pitch 2026 Jiří Holzinger.
