#!/usr/bin/env python3
"""cmp.py rom.bin port.tr ref.tr: each opcode's cycles on the port (mdhost -t) and on MAME
(trace.lua), by mnemonic. An opcode's time: from its start to the next instruction's."""
import collections, struct, sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from genrom import OPS, SLOT, OPOFF, HANDLER

rom = open(sys.argv[1], 'rb').read()
first = struct.unpack_from('>I', rom, 0x200 + len(rom[0x200:]) - len(rom[0x200:]))[0]
first = next(a for a in range(0x200, 0x400, SLOT) if struct.unpack_from('>H', rom, a + OPOFF)[0] == OPS[0]
             and rom[a:a + 4] == bytes.fromhex('46fc2700'))


def times(seq):
    res = {}
    for k in range(len(seq) - 1):
        t, pc = seq[k]
        o = pc - first - OPOFF
        if o >= 0 and o % SLOT == 0 and o // SLOT < len(OPS) and pc not in res:
            res[pc] = (seq[k + 1][0] - t, seq[k + 1][1] == HANDLER)
    return res


d = open(sys.argv[2], 'rb').read()
port = times([(t, pc) for t, pc in struct.iter_unpack('<II', d) if not pc >> 31])
seq, dis = [], {}
for line in open(sys.argv[3]):
    p = line.split(None, 2)
    if len(p) >= 3 and p[1].endswith(':'):
        pc = int(p[1][:-1], 16); seq.append((int(p[0]), pc))
        dis.setdefault(pc, p[2].strip())
ref = times(seq)
diff = collections.defaultdict(list); same = illegal = 0
for pc, (dr, er) in sorted(ref.items()):
    dp, ep = port.get(pc, (None, None))
    if (dr, er) == (dp, ep): same += 1; continue
    if er and not ep: illegal += 1; continue                       # MAME traps it, the port runs it
    diff[dis[pc].split()[0]].append((struct.unpack_from('>H', rom, pc)[0], dr, dp, er, ep, dis[pc]))
print('opcodes %d: same %d, illegal (MAME traps, the port does not) %d, differ %d'
      % (len(ref), same, illegal, sum(map(len, diff.values()))))
for mn, v in sorted(diff.items(), key=lambda kv: -len(kv[1])):
    c = collections.Counter((a[1], a[2]) for a in v)
    print('%-10s %5d  mame/port: %s' % (mn, len(v), ', '.join('%s/%s x%d' % (k[0], k[1], n) for k, n in c.most_common(6))))
    for a in v[:int(__import__('os').getenv('OT_SHOW', '3'))]:
        print('      %04x %-36s mame %d port %s%s' % (a[0], a[5][:36], a[1], a[2], ' (exception)' if a[3] or a[4] else ''))
