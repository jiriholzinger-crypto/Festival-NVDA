import io, os, sys, tarfile, shutil, tempfile
sys.path.insert(0, os.path.abspath(sys.argv[1]))
os.chdir(tempfile.mkdtemp(prefix="festival-catalog-test-"))
os.makedirs("work")
import catalog as c
ok = True
def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what); ok &= bool(cond)

listing = '''<html><body><h1>Index of /packed/festival/2.5/voices</h1><table>
<tr><th><a href="?C=N;O=D">Name</a></th><th><a href="?C=M;O=A">Last modified</a></th><th><a href="?C=S;O=A">Size</a></th></tr>
<tr><td><a href="/packed/festival/2.5/">Parent Directory</a></td><td>&nbsp;</td><td align="right">  - </td></tr>
<tr><td><a href="festvox_cmu_us_awb_cg.tar.gz">festvox_cmu_us_awb_cg.tar.gz</a></td><td align="right">2017-12-26 20:21  </td><td align="right"> 34M</td></tr>
<tr><td><a href="festvox_cmu_us_slt_cg.tar.gz">festvox_cmu_us_slt_cg.tar.gz</a></td><td align="right">2017-12-26 20:21  </td><td align="right">2.5M</td></tr>
<tr><td><a href="festvox_kallpc16k.tar.gz">festvox_kallpc16k.tar.gz</a></td><td align="right">2017-12-26 20:21  </td><td align="right">3.6M</td></tr>
<tr><td><a href="festvox_rablpc16k.tar.gz">festvox_rablpc16k.tar.gz</a></td><td align="right">2017-12-26 20:21  </td><td align="right">4.1M</td></tr>
<tr><td><a href="README">README</a></td><td align="right">2017-12-26</td><td align="right">1.2K</td></tr>
</table></body></html>'''
pk = c.parseListing(listing)
print(pk)
check([p.voice for p in pk] == ["cmu_us_awb_cg", "cmu_us_slt_cg", "kal_diphone", "rab_diphone"], "voice names from listing")
check([p.size for p in pk] == ["34M", "2.5M", "3.6M", "4.1M"], "sizes from listing")
check(pk[0].url == "http://festvox.org/packed/festival/2.5/voices/festvox_cmu_us_awb_cg.tar.gz", "absolute download URL")
pre = '<pre><a href="festvox_cmu_us_bdl_cg.tar.gz">festvox_cmu_us_bdl_cg.tar.gz</a>   26-Dec-2017 20:21   69M\n<a href="festvox_kedlpc16k.tar.gz">festvox_kedlpc16k.tar.gz</a>   26-Dec-2017 20:21   3.1M\n</pre>'
pk2 = c.parseListing(pre)
check([(p.voice, p.size) for p in pk2] == [("cmu_us_bdl_cg", "69M"), ("ked_diphone", "3.1M")], "nginx/pre style listing")

def mktar(path, entries):
    with tarfile.open(path, "w:gz") as t:
        for name, data in entries:
            info = tarfile.TarInfo(name)
            if data is None:
                info.type = tarfile.DIRTYPE
                t.addfile(info)
            elif data == "LINK":
                info.type = tarfile.SYMTYPE; info.linkname = "/etc/passwd"; t.addfile(info)
            else:
                info.size = len(data); t.addfile(info, io.BytesIO(data))
store = os.path.abspath("work/store"); os.makedirs(store)
arch = "work/festvox_cmu_us_test_cg.tar.gz"
mktar(arch, [("festival/", None), ("festival/lib/", None), ("festival/lib/voices/", None),
  ("festival/lib/voices/us/", None), ("festival/lib/voices/us/cmu_us_test_cg/", None),
  ("festival/lib/voices/us/cmu_us_test_cg/festvox/cmu_us_test_cg.scm", b"(setup_oald_lex)\n(proclaim_voice 'cmu_us_test_cg)"),
  ("festival/lib/voices/us/cmu_us_test_cg/festival/trees/x.tree", b"tree"),
  ("festival/lib/voices/../../../evil.txt", b"evil"),
  ("festival/lib/voices/us/cmu_us_test_cg/../../../../evil2.txt", b"evil"),
  ("/festival/lib/voices/abs.txt", b"x"),
  ("festival/lib/voices/us/cmu_us_test_cg/link", "LINK"),
  ("festival/lib/init.scm", b"must not be installed"),
  ("COPYING", b"licence")])
inst = c.installArchive(arch, store, "voices")
check(inst == ["us/cmu_us_test_cg"], "installed folders %s" % inst)
files = sorted(os.path.relpath(os.path.join(r, f), store) for r, _, fs in os.walk(store) for f in fs)
print(files)
check(files == ["voices/us/cmu_us_test_cg/festival/trees/x.tree", "voices/us/cmu_us_test_cg/festvox/cmu_us_test_cg.scm"], "only voice files, no traversal, no links")
check(not os.path.exists("work/evil.txt") and not os.path.exists(os.path.join(store, "evil.txt")), "no files outside the store")
v = c.installedVoices(os.path.join(store, "voices"))
check(list(v) == ["cmu_us_test_cg"], "installedVoices")
check(c.neededLexicons(v["cmu_us_test_cg"]) == {"oald"}, "voice needs OALD detected")
check(not c.lexiconInstalled("oald", os.path.join(store, "dicts")), "OALD not installed yet")
lex = "work/festlex_OALD.tar.gz"
mktar(lex, [("festival/lib/dicts/oald/oaldlex.scm", b"; lex"), ("festival/lib/dicts/oald/oald-0.4.out", b"data"), ("festival/lib/dicts/COPYING.oald", b"c")])
check(c.installArchive(lex, store, "dicts") == ["COPYING.oald", "oald"], "lexicon installed into dicts")
check(c.installArchive(lex, store, "dicts") == ["COPYING.oald", "oald"], "lexicon reinstall replaces files")
check(c.lexiconInstalled("oald", os.path.join(store, "dicts")), "OALD found")
# reinstall replaces cleanly
check(c.installArchive(arch, store, "voices") == ["us/cmu_us_test_cg"], "reinstall")
try:
    c.removeVoice("/etc", os.path.join(store, "voices")); check(False, "remove outside store refused")
except ValueError:
    check(True, "remove outside store refused")
c.removeVoice(v["cmu_us_test_cg"], os.path.join(store, "voices"))
check(c.installedVoices(os.path.join(store, "voices")) == {} and not os.path.exists(os.path.join(store, "voices", "us")), "removed with empty language folder")
empty = "work/empty.tar.gz"; mktar(empty, [("README", b"x")])
try:
    c.installArchive(empty, store, "voices"); check(False, "archive without voices rejected")
except ValueError:
    check(True, "archive without voices rejected")
check(not [d for d in os.listdir(store) if d.startswith(".staging")], "no staging folders left")
c.saveInfo(store, {"a.tar.gz": ["us/x"]}); check(c.loadInfo(store) == {"a.tar.gz": ["us/x"]}, "info file")
print("RESULT", "ALL PASSED" if ok else "FAILED")
