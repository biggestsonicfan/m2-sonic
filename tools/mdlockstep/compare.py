#!/usr/bin/env python3
"""compare.py <dir>: compare mdlockstep's recordings of MAME's Mega Drive (ref_*) and the
i960 port (port_*), frame by frame (frame k = the k-th VINT, both run the same inputs).

  registers   d0-d7 a0-a7 pc sr usp as each interrupt is taken
  timing      the 68000 cycle each VINT is acknowledged at (ref_iack / port_iack)
  memory      at each VINT: work RAM, VRAM, CRAM, VSRAM, the VDP's 24 registers
  sound       the YM2612 / PSG writes and Z80 sound commands the 68000 made, per frame
  pictures    the 320x224 picture (ref: MAME's Mega Drive screen, port: the Model 2
              screen's window), colours reduced to the Mega Drive's 3 bits a channel
  audio       ref.wav vs port.wav: loudness envelopes and spectra per second
Writes diff pictures (diff_NNNNN.png) for the worst frames when PIL is there.
"""
import os, struct, sys

D = sys.argv[1] if len(sys.argv) > 1 else '/tmp/mdlockstep'
IRQ = 84
MEM = 65536 + 65536 + 128 + 80 + 24
REGN = ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(8)] + ['pc', 'sr', 'usp']


def rd(n):
    p = os.path.join(D, n)
    return open(p, 'rb').read() if os.path.exists(p) else b''


def irqs(b):
    out = []
    for i in range(0, len(b) - IRQ + 1, IRQ):
        v = struct.unpack_from('<21I', b, i)
        out.append((v[0], v[1], v[2:]))
    return out


def section(t): print('\n== ' + t)


def iacks(b):     # frame -> 68000 cycles at the VINT's acknowledge
    return {f: c for f, l, c in (struct.unpack_from('<IIQ', b, i) for i in range(0, len(b) - 15, 16)) if l == 6}


def timing():
    """VINT times: MAME's 68000 cycle count at the acknowledge bus cycle (device.total_cycles)
    vs the port's md_now() as it takes the interrupt, which is earlier: the exception's first
    cycles and the E-clock wait (vpa_sync) come between. Each side counts from its first
    common VINT, so a steady lead shows as a constant; the spread around it is the timing."""
    r, p = iacks(rd('ref_iack.bin')), iacks(rd('port_iack.bin'))
    common = sorted(set(r) & set(p))
    if len(common) < 2:
        print('\n(no interrupt timing: needs ref_iack.bin, a MAME with device.total_cycles)'); return
    section('VINT acknowledge times (68000 cycles, %d frames)' % len(common))
    k0 = common[0]
    off = [((p[k] - p[k0]) & 0xffffffff) - (r[k] - r[k0]) for k in common]
    so = sorted(off); med = so[len(so) // 2]
    near = sum(1 for d in off if abs(d - med) <= 10)
    print('port - ref: min %d, median %d, max %d; within 10 cycles of the median in %d of %d frames'
          % (so[0], med, so[-1], near, len(off)))
    for name, t in (('ref', r), ('port', p)):
        iv = {}
        for a, b in zip(common, common[1:]):
            if b == a + 1: d = (t[b] - t[a]) & 0xffffffff; iv[d] = iv.get(d, 0) + 1
        print('%-4s intervals: %s' % (name, ', '.join('%d x%d' % kv for kv in sorted(iv.items(), key=lambda x: -x[1])[:6])))
    bad = [(k, d - med) for k, d in zip(common[1:], off[1:]) if abs(d - med) > 10]
    if bad: print('first further off: frame %d, %+d cycles from the median' % bad[0])


def main():
    ri, pi = irqs(rd('ref_irq.bin')), irqs(rd('port_irq.bin'))
    section('interrupts taken')
    for name, l in (('ref', ri), ('port', pi)):
        v6 = sum(1 for x in l if x[1] == 6); v4 = sum(1 for x in l if x[1] == 4)
        print('%-4s VINT %d, HINT %d' % (name, v6, v4))
    rv = {x[0]: x[2] for x in ri if x[1] == 6}
    pv = {x[0]: x[2] for x in pi if x[1] == 6}
    common = sorted(set(rv) & set(pv))
    same = 0; diff_by = {}; first = None; idle_pc = 0
    IDLE = (0x29ac, 0x29b2)       # Sonic's WaitForVBla: tst.b / bne.s, either is the same wait
    def norm(r, level):
        r = list(r)
        # MAME shows SR as the acknowledge leaves it (supervisor, mask = the level); the port
        # records it just before: compare the port's with that applied
        return r
    for k in common:
        r, p = list(rv[k]), list(pv[k])
        p[17] = (p[17] & ~0x0700 & ~0x8000) | 0x2000 | (6 << 8)
        if IDLE[0] <= r[16] <= IDLE[1] and IDLE[0] <= p[16] <= IDLE[1] and r[16] != p[16]:
            idle_pc += 1; p[16] = r[16]
        bad = [REGN[j] for j in range(19) if r[j] != p[j]]
        if not bad: same += 1
        else:
            first = first or (k, bad, r, p)
            for r in bad: diff_by[r] = diff_by.get(r, 0) + 1
    section('registers at VINT acceptance (%d frames)' % len(common))
    print('identical in %d of %d (SR as MAME shows it after the acknowledge; in %d frames the two'
          ' wait in WaitForVBla on different instructions of the loop, counted identical)' % (same, len(common), idle_pc))
    if diff_by: print('differing, per register:', ', '.join('%s %d' % kv for kv in sorted(diff_by.items(), key=lambda x: -x[1])))
    if first:
        k, bad, r, p = first
        print('first at frame %d: %s' % (k, ' '.join('%s ref %08x port %08x' % (n, r[REGN.index(n)], p[REGN.index(n)]) for n in bad)))

    timing()

    rm, pm = rd('ref_mem.bin'), rd('port_mem.bin')
    n = min(len(rm), len(pm)) // MEM
    section('memory at VINT (%d frames)' % n)
    parts = (('work RAM', 0, 65536), ('VRAM', 65536, 65536), ('CRAM', 131072, 128), ('VSRAM', 131200, 80), ('VDP regs', 131280, 24))
    stack = 0
    for name, off, size in parts:
        exact = 0; firstf = None; worst = (0, 0); addrs = {}
        for f in range(n):
            a = rm[f * MEM + off: f * MEM + off + size]; b = pm[f * MEM + off: f * MEM + off + size]
            if a == b: exact += 1; continue
            d = [i for i in range(size) if a[i] != b[i]]
            if name == 'work RAM' and (f + 1) in rv and (f + 1) in pv:
                # below both stack pointers (at the VINT, before its frame is pushed): dead
                sp = min(rv[f + 1][15], pv[f + 1][15]) & 0xffff
                live = [i for i in d if not (0xfd00 <= i < sp)]     # Sonic's stack page
                if not live: exact += 1; stack += 1; continue
                d = live
            if firstf is None: firstf = (f + 1, d[:8])
            if len(d) > worst[0]: worst = (len(d), f + 1)
            for i in d: addrs[i >> 8] = addrs.get(i >> 8, 0) + 1
        line = '%-9s identical in %d of %d frames' % (name, exact, n)
        if name == 'work RAM' and stack: line += ' (%d of them differ only in dead stack, below the SP)' % stack
        if firstf:
            base = 0xff0000 if name == 'work RAM' else 0
            line += '; first difference frame %d at %s; most in one frame %d bytes (frame %d)' % (
                firstf[0], ' '.join('%06x' % (base + i) for i in firstf[1]), worst[0], worst[1])
            hot = sorted(addrs.items(), key=lambda x: -x[1])[:6]
            line += '\n          by 256-byte block (block: frames x bytes): ' + ', '.join('%06x: %d' % (base + (b << 8), c) for b, c in hot)
        print(line)

    def sndmap(b):
        m = {}
        for i in range(0, len(b) - 7, 8):
            f, kind, port, reg, val = struct.unpack_from('<IBBBB', b, i)
            m.setdefault(f, []).append((kind, port, reg, val))
        return m
    rs, ps = sndmap(rd('ref_snd.bin')), sndmap(rd('port_snd.bin'))
    frames = [k for k in range(1, n + 1)]
    section('sound chip writes by the 68000 (%d frames)' % len(frames))
    same = sum(1 for k in frames if rs.get(k, []) == ps.get(k, []))
    tot = lambda m, kind: sum(1 for k in frames for e in m.get(k, []) if e[0] == kind)
    print('frames with the same writes, in order: %d of %d' % (same, len(frames)))
    for kind, name in ((1, 'YM2612'), (2, 'PSG'), (3, 'Z80 commands')):
        print('  %-12s ref %6d  port %6d' % (name, tot(rs, kind), tot(ps, kind)))
    for k in frames:
        if rs.get(k, []) != ps.get(k, []):
            print('  first different frame %d: ref %s ... port %s ...' % (k, rs.get(k, [])[:4], ps.get(k, [])[:4])); break

    section('pictures (colours as Mega Drive levels)')
    # Each machine's picture against the reference renderer (src/md_render.h, tools/mdlockstep/
    # render.c) drawing the VDP state that picture was made from: the port's tilemap output
    # (the Model 2 screen, through its colour tables), MAME's Mega Drive screen. With the
    # VDP memories identical (above), the two together check the whole picture path.
    import subprocess
    here = os.path.dirname(os.path.abspath(__file__))
    rend = os.path.join(D, 'mdrender')
    if True:                  # always: a stale one would draw with old rules
        subprocess.check_call(['cc', '-O2', '-I' + os.path.join(here, '..', '..', 'src'), '-o', rend, os.path.join(here, 'render.c')])
    try:
        from PIL import Image
    except ImportError:
        Image = None
    def levels(names):
        vals = set()
        for n in names: vals |= set(open(os.path.join(D, n), 'rb').read())
        vals = sorted(vals)
        return {v: (round(v * 7 / 255) if len(vals) != 8 else i) for i, v in enumerate(vals)}
    # the port's state: dumped as the picture was handed to the tilemaps (port_NNNNN.mem);
    # MAME's: the record at the VINT that ends the frame it drew (ref_mem.bin; the handler
    # after it already changes sprites and colours for the next frame)
    def refmem(k, out):
        with open(os.path.join(D, 'ref_mem.bin'), 'rb') as fh:
            fh.seek((k - 1) * MEM); b = fh.read(MEM)
        if len(b) < MEM: return False
        open(out, 'wb').write(b); return True
    for side in ('port', 'ref'):
        ks = sorted(int(f[len(side) + 1:len(side) + 6]) for f in os.listdir(D) if f.startswith(side + '_') and f.endswith('.rgb')
                    and (side == 'ref' or os.path.exists(os.path.join(D, f[:-4] + '.mem'))))
        if not ks: print('%s: no pictures' % side); continue
        lv = levels(['%s_%05d.rgb' % (side, k) for k in ks])
        rows = []
        for k in ks:
            b = bytes(lv[c] for c in open(os.path.join(D, '%s_%05d.rgb' % (side, k)), 'rb').read())
            best = None
            for dk in ((0,) if side == 'port' else (-1, 0, 1)):
                mem = os.path.join(D, '%s_%05d.mem' % (side, k))
                if side == 'ref':
                    mem = os.path.join(D, 'swr_ref.mem')
                    if not refmem(k + dk, mem): continue
                out = os.path.join(D, 'swr_%s_%05d.rgb' % (side, k))
                subprocess.check_call([rend, mem, '0', out])
                a = bytes(round(c * 7 / 255) for c in open(out, 'rb').read())
                nd = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])
                if best is None or nd < best[0]: best = (nd, k, a, b)
            if best: rows.append(best)
        print('%-4s %d pictures vs the renderer on their own VDP state: %d identical, median %d, max %d differing pixels (of 71680)' % (
            side, len(rows), sum(1 for r in rows if r[0] == 0), sorted(r[0] for r in rows)[len(rows) // 2], max(r[0] for r in rows)))
        bad = [(r[1], r[0]) for r in rows if r[0]]
        if bad: print('     differing: ' + ', '.join('frame %d (%d px)' % x for x in bad[:12]) + (' ...' if len(bad) > 12 else ''))
        if Image:
            for nd, k, a, b in sorted(rows, key=lambda r: -r[0])[:3]:
                if not nd: break
                up = lambda x: bytes(c * 255 // 7 for c in x)
                d = bytearray()
                for i in range(0, len(a), 3):
                    d += b'\xff\x00\x00' if a[i:i + 3] != b[i:i + 3] else bytes(c * 20 for c in a[i:i + 3])
                img = Image.new('RGB', (960, 224))
                img.paste(Image.frombytes('RGB', (320, 224), up(a)), (0, 0))
                img.paste(Image.frombytes('RGB', (320, 224), up(b)), (320, 0))
                img.paste(Image.frombytes('RGB', (320, 224), bytes(d)), (640, 0))
                img.save(os.path.join(D, 'diff_%s_%05d.png' % (side, k)))

    section('audio')
    try:
        import numpy as np, wave
        def load(p):
            w = wave.open(p); r = w.getframerate(); ch = w.getnchannels()
            x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float)
            return (x.reshape(-1, ch).mean(1) if ch > 1 else x), r
        a, ra = load(os.path.join(D, 'ref.wav')); b, rb = load(os.path.join(D, 'port.wav'))
        def env(x, r, h=0.02):
            k = int(r * h); return np.array([np.abs(x[i * k:(i + 1) * k] - x[i * k:(i + 1) * k].mean()).mean() for i in range(len(x) // k)])
        ea, eb = env(a, ra), env(b, rb)
        z = lambda v: (v - v.mean()) / (v.std() + 1e-9)
        lags = range(0, min(600, len(eb) - 200))
        seg = z(ea[: min(len(ea), len(eb)) - 600])
        c = [np.dot(seg, z(eb[L:L + len(seg)])) / len(seg) for L in lags]
        L = int(np.argmax(c))
        print('the port trails by %.2f s (boot); loudness correlation after that: %.3f' % (L * 0.02, max(c)))
        # spectra per second, on the aligned timeline
        sims = []
        for t in range(2, int(min(len(a) / ra, len(b) / rb - L * 0.02)) - 1):
            def spec(x, r, t0):
                s = x[int(t0 * r): int(t0 * r) + 8192]
                m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); f = np.fft.rfftfreq(len(s), 1 / r)
                g = np.linspace(60, 4000, 400); return np.interp(g, f, np.log1p(m))
            sa, sb = spec(a, ra, t), spec(b, rb, t + L * 0.02)
            if sa.std() > 0.1 and sb.std() > 0.1: sims.append(np.dot(z(sa), z(sb)) / len(sa))
        if sims: print('spectrum similarity per second: median %.3f, >= 0.8 in %d%% of %d seconds' % (
            float(np.median(sims)), 100 * sum(1 for s in sims if s >= 0.8) // len(sims), len(sims)))
    except Exception as e:
        print('audio: skipped (%s)' % e)


if __name__ == '__main__':
    main()
