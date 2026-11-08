# Festival synthesizer for NVDA

NVDA add-on for the Festival Speech Synthesis System 2.5.1, with English and
Czech voices, for 64-bit NVDA 2026.1 and later. More voices can be downloaded
from festvox.org inside NVDA (NVDA menu, Tools, Festival voices).

Based on the Festival synthDriver and festlib by Olga Yakovleva (2008–2011).
64-bit port, pitch, voice downloader and NVDA 2026 driver: Jiří Holzinger.

## Repository

| folder | content |
|---|---|
| `festival/` | Festival 2.5.1 sources with the Windows x64 patches and `CMakeLists.txt`; `festival/lib` holds the Scheme library and the voices that go into the add-on |
| `festlib64/` | festlib.dll (64-bit port), `festhost64.exe`, the NVDA driver, the voice downloader, Czech translation, patches and tests (see `festlib64/README.md`) |
| `addon/` | manifest, documentation (English, Czech), licence and `festsettings.xml` |
| `tools/build-addon.sh` | builds everything and packs `dist/festival-<version>.nvda-addon` |
| `.github/workflows/build.yml` | GitHub Actions: builds on every push, publishes releases for `v*` tags |

The Edinburgh Speech Tools are not stored here: the build clones
https://github.com/festvox/speech_tools at the tested commit and applies
`festlib64/patches_speech_tools-2.5.1_win64.diff`.

## Uploading to GitHub (first time)

1. On https://github.com/new create an empty repository, e.g. `festival-nvda`
   (no README, licence or .gitignore – they are here already).
2. In this folder (Git for Windows, or any shell with git):

   ```
   git init -b main
   git add -A
   git commit -m "Festival synthesizer for NVDA 2026.11.5"
   git remote add origin https://github.com/jiriholzinger-crypto/festival-nvda.git
   git push -u origin main
   ```

   The push is about 55 MB (mostly the voices), so it takes a moment. No file is over
   GitHub's 100 MB limit, Git LFS is not needed.
3. The push starts the workflow (tab **Actions**). After about 10 minutes the
   built add-on is under the run, section **Artifacts**.

## Making a release

1. Add a section for the new version at the top of `CHANGELOG.md`:

   ```
   ## 2026.11.6
   - what changed
   ```

   (The changes in `addon/doc/*/readme.html` are kept by hand, as before.)
2. Create the release, either way:
   - on github.com: **Releases** → **Draft a new release** → new tag `2026.11.6`
     (with or without `v`) → **Publish release**, or
   - with git: `git tag v2026.11.6` and `git push origin v2026.11.6`.
3. The workflow builds the add-on with that version (the version in
   `manifest.ini` is set from the tag) and attaches
   `festival-2026.11.6.nvda-addon` to the release, after about 10 minutes
   (tab **Actions**). A release created by the workflow itself gets the title
   **Festival for NVDA 2026.11.6** and the `CHANGELOG.md` section as notes.

To add the add-on to an existing release later: **Actions** → **Build add-on** →
**Run workflow**, enter its version and tick **Publish a release**.

If the release step fails with "Resource not accessible by integration", allow
it once in the repository: **Settings** → **Actions** → **General** →
**Workflow permissions** → **Read and write permissions**.

## Building locally

Linux (or MSYS2 on Windows) with git, python3, meson, ninja, cmake, zip and
llvm-mingw (https://github.com/mstorsjo/llvm-mingw, release 20251104):

```
sh tools/build-addon.sh 2026.11.6 /path/to/llvm-mingw
```

## Licence

GNU General Public License version 2 or later (driver, festlib, downloader).
Festival and the Speech Tools: X11-style licence (University of Edinburgh and
others). The voices have their own licences in their folders.
