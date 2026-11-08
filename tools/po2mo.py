# minimal msgfmt: .po -> .mo (single-line and concatenated strings, no plurals)
import ast, struct, sys
def parse(path):
    msgs, key, cur, field = {}, None, None, None
    def flush():
        if field is not None: pass
    entries = []; msgid = msgstr = None; mode = None
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("msgid "):
            if msgid is not None: entries.append((msgid, msgstr))
            msgid = ast.literal_eval(line[6:]); msgstr = ""; mode = "id"
        elif line.startswith("msgstr "):
            msgstr = ast.literal_eval(line[7:]); mode = "str"
        elif line.startswith('"'):
            if mode == "id": msgid += ast.literal_eval(line)
            else: msgstr += ast.literal_eval(line)
    if msgid is not None: entries.append((msgid, msgstr))
    return {k: v for k, v in entries if v}
msgs = parse(sys.argv[1])
keys = sorted(msgs)
ids = b"".join(k.encode() + b"\0" for k in keys); strs = b"".join(msgs[k].encode() + b"\0" for k in keys)
n = len(keys); o1 = 28; o2 = o1 + n * 8; data = o2 + n * 8
koffs, voffs, pos = [], [], 0
for k in keys:
    b = k.encode(); koffs.append((len(b), data + pos)); pos += len(b) + 1
base = data + len(ids); pos = 0
for k in keys:
    b = msgs[k].encode(); voffs.append((len(b), base + pos)); pos += len(b) + 1
out = struct.pack("<7I", 0x950412de, 0, n, o1, o2, 0, 0)
out += b"".join(struct.pack("<2I", *x) for x in koffs) + b"".join(struct.pack("<2I", *x) for x in voffs) + ids + strs
open(sys.argv[2], "wb").write(out)
print(n, "messages")
