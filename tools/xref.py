import sys, struct
data = open("nfs13.bin", "rb").read(); BASE = 0x400000
for t in [int(a, 16) for a in sys.argv[1:]]:
    hits = []
    for i in range(len(data) - 5):
        b = data[i]
        if b in (0xE8, 0xE9):
            rel = struct.unpack_from("<i", data, i + 1)[0]
            if BASE + i + 5 + rel == t: hits.append(("call" if b == 0xE8 else "jmp", BASE + i))
    raw = struct.pack("<I", t); j = data.find(raw)
    while j != -1:
        hits.append(("ref", BASE + j)); j = data.find(raw, j + 1)
    print(hex(t), [(k, hex(a)) for k, a in hits][:20])
