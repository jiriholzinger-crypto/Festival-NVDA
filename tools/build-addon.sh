#!/bin/sh
# Build festlib.dll and festhost64.exe from source and assemble the NVDA add-on.
#
# usage: tools/build-addon.sh <version> <path to llvm-mingw>
#   e.g. tools/build-addon.sh 2026.11.6 /opt/llvm-mingw
# result: dist/festival-<version>.nvda-addon
#
# Needs: git, python3, meson, ninja, cmake, zip.
set -e
VERSION=${1:?usage: build-addon.sh <version> <llvm-mingw dir>}
LLVM_MINGW=${2:?usage: build-addon.sh <version> <llvm-mingw dir>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SPEECH_TOOLS_REPO=https://github.com/festvox/speech_tools
SPEECH_TOOLS_COMMIT=63ff019

case "$VERSION" in
  *[!0-9.]*|"") echo "version must look like 2026.11.6, got '$VERSION'" >&2; exit 1 ;;
esac

# 1. Edinburgh Speech Tools at the tested commit (patched by festlib64/build.sh)
if [ ! -d "$ROOT/speech_tools/.git" ]; then
  git clone --quiet "$SPEECH_TOOLS_REPO" "$ROOT/speech_tools"
fi
git -C "$ROOT/speech_tools" -c advice.detachedHead=false checkout --quiet "$SPEECH_TOOLS_COMMIT" 2>/dev/null || {
  # already patched working tree: keep it
  echo "speech_tools: keeping the existing working tree"
}

# 2. festlib.dll + festhost64.exe
sh "$ROOT/festlib64/build.sh" "$LLVM_MINGW"
BUILD="$ROOT/festlib64/build-win64"

# 3. tests that need no Windows
python3 -I "$ROOT/festlib64/tests/catalog/test_catalog.py" "$ROOT/festlib64/addon/globalPlugins/festivalVoices" | tail -1 | grep -q "ALL PASSED" \
  || { echo "catalog tests failed" >&2; exit 1; }

# 4. assemble the add-on
OUT="$ROOT/dist/addon"
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$ROOT/addon/." "$OUT/"
sed -i "s/^version = .*/version = $VERSION/" "$OUT/manifest.ini"

SD="$OUT/synthDrivers/festival"
mkdir -p "$SD/festival"
cp "$ROOT/festlib64/driver/__init__.py" "$SD/"
cp "$BUILD/festlib.dll" "$BUILD/festhost64.exe" "$SD/"
# Festival's Scheme library and voices, without the Unix/VC build files
(cd "$ROOT/festival" && tar -cf - --exclude='Makefile*' --exclude=VCLocalRules lib) | (cd "$SD/festival" && tar -xf -)
cp "$ROOT/festlib64/scm/festlib_helper.scm" "$SD/festival/lib/"

cp -r "$ROOT/festlib64/addon/globalPlugins" "$OUT/"
for po in "$ROOT"/festlib64/addon/locale/*/LC_MESSAGES/nvda.po; do
  [ -f "$po" ] || continue
  lang=$(basename "$(dirname "$(dirname "$po")")")
  mkdir -p "$OUT/locale/$lang/LC_MESSAGES"
  python3 -I "$ROOT/tools/po2mo.py" "$po" "$OUT/locale/$lang/LC_MESSAGES/nvda.mo" >/dev/null
done
find "$OUT" -name __pycache__ -prune -exec rm -rf {} +

# 5. pack (an .nvda-addon is a zip with manifest.ini at the top)
ADDON="$ROOT/dist/festival-$VERSION.nvda-addon"
rm -f "$ADDON"
(cd "$OUT" && zip -q -r -X "$ADDON" .)
echo "built: $ADDON"
