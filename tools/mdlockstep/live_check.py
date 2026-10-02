#!/usr/bin/env python3
"""live_check.py [--follow] <dir>: compare the records live.lua writes on both sides of the
live lockstep ($LIVE_DIR/ref.rec: MAME's Mega Drive, port.rec: the i960 port on Model 2),
frame by frame (frame k = the k-th VINT on both).

  state     at each VINT: the 68000's registers, work RAM (16 blocks + the live stack), VRAM
            (16 blocks), CRAM, VSRAM, VDP registers: hashes, which must be identical
  writes    the YM2612 / PSG writes and Z80 commands the 68000 made in the frame (hash): the
            music and effects the game asks for
  pictures  each picture the port draws (320x224, Mega Drive colour levels) against MAME's
            picture of the same frame
  notes     what is played: each FM key-on (with its pitch), PSG tone / noise becoming audible
            or changing pitch, drum hits, on the Mega Drive from the 68000's chip writes, on
            the Model 2 from the SCSP slot writes the sound board makes; matched in order
            per voice, the Model 2 allowed to be up to 45 frames late (the serial line)
  loudness  the RMS of each machine's speaker output per frame, correlated over 10 s,
            at the best delay of the Model 2's (it re-voices the chips, so samples differ)

--follow: keep reading as the records grow, write a one-line verdict to <dir>/status.txt (both
screens show it) and each difference to <dir>/check.log; without: one pass, then the report.
"""
import math, os, sys, time

FOLLOW = '--follow' in sys.argv
args = [a for a in sys.argv[1:] if not a.startswith('--')]
D = args[0] if args else '/tmp/sonic-live'
LATE = 45                     # frames the Model 2 may play a note late
FIELDS = ('cpu', 'ram', 'vram', 'cram', 'vsram', 'vdp', 'writes')
RAMNAME = ['ff%x000' % b for b in range(16)] + ['stack']
VOICES = ['FM1', 'FM2', 'FM3', 'FM4', 'FM5', 'FM6', 'PSG1', 'PSG2', 'PSG3', 'noise', 'drums']


class Reader:
    """Lines of a record file as it grows; follows live.lua starting it over (renamed away)."""
    def __init__(self, path):
        self.path, self.fh, self.part = path, None, ''

    def lines(self):
        if self.fh is None:
            try: self.fh = open(self.path)
            except OSError: return []
        out = []
        while True:
            s = self.part + self.fh.read()
            self.part = ''
            if s:
                ls = s.split('\n')
                self.part = ls.pop()
                out += ls
            try: moved = os.stat(self.path).st_ino != os.fstat(self.fh.fileno()).st_ino
            except OSError: moved = False
            if not moved: return out
            self.fh.close(); self.fh = open(self.path)        # the rest was read: the new one


def cents(hz): return 1200 * math.log2(hz / 440) if hz > 0 else -99999


class RefVoices:
    """The Mega Drive's chips as the 68000 programs them -> note events."""
    def __init__(self, ev):
        self.ev, self.ym = ev, [[0] * 256, [0] * 256]
        self.att, self.tone, self.latch, self.was = [15] * 4, [0] * 3, 0, [None] * 4

    def write(self, k, kind, p, r, v):
        if kind == 1:
            self.ym[p][r] = v
            if p == 0 and r == 0x28:
                c = v & 7
                ch = c if c < 3 else c - 1 if 4 <= c < 7 else None
                if ch is not None and v & 0xf0 and not (ch == 5 and self.ym[0][0x2b] & 0x80):
                    lo, hi = self.ym[ch // 3][0xa0 + ch % 3], self.ym[ch // 3][0xa4 + ch % 3]
                    fnum, block = (hi & 7) << 8 | lo, hi >> 3 & 7
                    self.ev(ch, k, cents(fnum * 2 ** (block - 1) * 7670453 / 144 / 2 ** 20))
        elif kind == 2:
            if v & 0x80:
                self.latch = v
                c = v >> 5 & 3
                if v & 0x10: self.att[c] = v & 15
                elif c < 3: self.tone[c] = self.tone[c] & 0x3f0 | v & 15
            else:
                c = self.latch >> 5 & 3
                if not self.latch & 0x10 and c < 3: self.tone[c] = self.tone[c] & 15 | (v & 0x3f) << 4
        elif kind == 3 and 0x81 <= v <= 0x83:
            self.ev(10, k, 'hit')

    def frame_end(self, k):
        for c in range(4):
            now = None
            if self.att[c] < 15:
                now = cents(3579545 / (32 * (self.tone[c] or 1024))) if c < 3 else 0
            psg_event(self.ev, 6 + c, k, self.was[c], now)
            self.was[c] = now


class PortVoices:
    """The SCSP as the Model 2's sound board programs it (src/md_snd.h's slots: 0-5 FM, 6-8
    PSG tones, 9 noise, 10 drums) -> note events."""
    def __init__(self, ev):
        self.ev, self.reg, self.was = ev, [[0] * 16 for _ in range(32)], [None] * 4

    @staticmethod
    def hz(v, period):          # slot pitch: (1024 + FNS) << OCT / 1024 samples per 1/44100 s
        o = v >> 11 & 15
        return 44100 * (1024 + (v & 0x3ff)) / 1024 * 2.0 ** (o - 16 if o >= 8 else o) / period

    def write(self, k, slot, r, v):
        self.reg[slot][r >> 1] = v
        if r == 0 and v & 0x1800 == 0x1800:                       # KYONEX with KYONB: key on
            if slot < 6: self.ev(slot, k, cents(self.hz(self.reg[slot][8], 64)))
            elif slot == 10: self.ev(10, k, 'hit')

    def frame_end(self, k):
        for c in range(4):
            s = 6 + c
            now = None
            if self.reg[s][6] & 0xff < 255:
                now = cents(self.hz(self.reg[s][8], 32)) if c < 3 else 0
            psg_event(self.ev, s, k, self.was[c], now)
            self.was[c] = now


def psg_event(ev, voice, k, was, now):
    """a PSG voice turning audible, or a new tone while audible (more than a quarter tone)"""
    if now is not None and (was is None or abs(now - was) > 50): ev(voice, k, now)


class Check:
    def __init__(self):
        self.F = {'ref': {}, 'port': {}}
        self.pic = {'ref': {}, 'port': []}
        self.last = {'ref': 0, 'port': 0}
        self.rms = {'ref': {}, 'port': {}}
        self.n = 0; self.same = {f: 0 for f in FIELDS}; self.allsame = 0
        self.diffs = []                       # (frame, what) for check.log
        self.recent = {}                      # field -> last frame it differed
        self.blocks = {'ram': {}, 'vram': {}}
        self.pics = {'same': 0, 'near': 0, 'diff': 0}; self.picdiff = []
        self.events = {'ref': [[] for _ in VOICES], 'port': [[] for _ in VOICES]}
        self.voices = {'ref': RefVoices(lambda v, k, t: self.events['ref'][v].append((k, t))),
                       'port': PortVoices(lambda v, k, t: self.events['port'][v].append((k, t)))}
        self.sound_up = None                  # the first frame the Model 2's sound board wrote
        self.notes = [{'match': 0, 'missing': 0, 'extra': 0} for _ in VOICES]
        self.lags = {}; self.notediff = []
        self.loud = None

    def line(self, side, s):
        t = s.split()
        if not t: return
        if t[0] == 'F' and len(t) >= 11:
            k = int(t[1])
            self.F[side][k] = t
            self.rms[side][k] = float(t[10])
            self.voices[side].frame_end(k)
            self.last[side] = k
        elif t[0] == 'P':
            if side == 'ref': self.pic['ref'][int(t[1])] = t[2]
            else: self.pic['port'].append((int(t[1]), t[2]))
        elif t[0] == 'Y':
            self.voices['ref'].write(*map(int, t[1:6]))
        elif t[0] == 'W':
            k = int(t[1])
            if self.sound_up is None: self.sound_up = k
            self.voices['port'].write(k, int(t[2]), int(t[3]), int(t[4]))

    # ---- state: identical hashes ----
    def frames(self):
        for k in sorted(set(self.F['ref']) & set(self.F['port'])):
            r, p = self.F['ref'].pop(k), self.F['port'].pop(k)
            self.n += 1
            bad = []
            for f, i in zip(FIELDS, (3, 4, 5, 6, 7, 8, 9)):
                a, b = r[i], p[i]
                if a == b: self.same[f] += 1; continue
                self.recent[f] = k
                if f in ('ram', 'vram'):
                    ra, pa = a.split(','), b.split(',')
                    names = RAMNAME if f == 'ram' else ['%04x' % (b_ * 4096) for b_ in range(16)]
                    which = [names[j] for j in range(len(ra)) if ra[j] != pa[j]]
                    for w in which: self.blocks[f][w] = self.blocks[f].get(w, 0) + 1
                    bad.append('%s %s' % (f, ' '.join(which)))
                elif f == 'cpu': bad.append('cpu (pc ref %s port %s)' % (r[2], p[2]))
                elif f == 'writes': bad.append('writes (ref %s port %s: hash/count)' % (a, b))
                else: bad.append(f)
            if not bad: self.allsame += 1
            else: self.diffs.append((k, '; '.join(bad)))

    # ---- pictures ----
    def pictures(self):
        keep = []
        for k, h in self.pic['port']:
            ref = self.pic['ref']
            if k not in ref:
                if self.last['ref'] <= k + 1: keep.append((k, h))   # MAME's not made yet
                continue                                             # (or the run ended)
            if ref.get(k) == h: self.pics['same'] += 1
            elif h in (ref.get(k - 1), ref.get(k + 1)): self.pics['near'] += 1; self.picdiff.append((k, 'a frame off'))
            else: self.pics['diff'] += 1; self.picdiff.append((k, 'differs'))
        self.pic['port'] = keep
        for k in [k for k in self.pic['ref'] if k < self.last['ref'] - 600]: del self.pic['ref'][k]

    # ---- notes: matched in order per voice ----
    def match(self, final=False):
        horizon = min(self.last['ref'], self.last['port']) - (0 if final else LATE + 5)
        up = self.sound_up
        for v in range(len(VOICES)):
            R, Pe = self.events['ref'][v], self.events['port'][v]
            while R and R[0][0] <= horizon:
                k, t = R.pop(0)
                if up is None or k < up + 5: continue            # before the sound board answered
                while Pe and Pe[0][0] < k - 2:                     # ones nothing on the MD asked for
                    self.notes[v]['extra'] += 1; self.notediff.append((Pe[0][0], '%s extra %s' % (VOICES[v], tok(Pe[0][1])))); Pe.pop(0)
                hit = None
                for j, (pk, pt) in enumerate(Pe):
                    if pk > k + LATE: break
                    if t == pt or (t != 'hit' and pt != 'hit' and abs(t - pt) <= 50): hit = j; break
                if hit is None:
                    if final and k > horizon - LATE: continue      # the run ended before it was due
                    self.notes[v]['missing'] += 1
                    near = [tok(pt) for pk, pt in Pe if pk <= k + LATE][:3]
                    self.notediff.append((k, '%s %s not played%s' % (VOICES[v], tok(t), ' (played: %s)' % ', '.join(near) if near else '')))
                    continue
                self.notes[v]['match'] += 1
                lag = Pe[hit][0] - k
                self.lags[lag] = self.lags.get(lag, 0) + 1
                Pe.pop(hit)
            if final:
                for pk, pt in Pe:
                    if up is not None and pk >= up + 5 and pk <= horizon: self.notes[v]['extra'] += 1
                Pe.clear()

    # ---- loudness ----
    def loudness(self, window=600):
        r, p = self.rms['ref'], self.rms['port']
        top = min(self.last['ref'], self.last['port'])
        lo = max(1, top - window - 120) if window else (self.sound_up or 1) + 60
        ks = range(lo, top - 120)
        if len(ks) < 120: return
        a = [r.get(k, 0.0) for k in ks]
        best = None
        for lag in range(0, 121, 2):
            b = [p.get(k + lag, 0.0) for k in ks]
            c = corr(a, b)
            if best is None or c > best[0]: best = (c, lag)
        self.loud = best
        for k in [k for k in r if k < top - 2000]: del r[k]
        for k in [k for k in p if k < top - 2000]: del p[k]

    def notes_total(self):
        t = {'match': 0, 'missing': 0, 'extra': 0}
        for n in self.notes:
            for x in t: t[x] += n[x]
        return t

    def status(self):
        k = min(self.last['ref'], self.last['port'])
        bad = [f for f in FIELDS if self.recent.get(f, -999) > k - 60]
        s = 'check %d: %s' % (k, 'DIFF ' + ' '.join(bad) if bad else 'same')
        s += ' (%d/%d)' % (self.allsame, self.n)
        s += '  pic %d/%d' % (self.pics['same'], sum(self.pics.values()))
        t = self.notes_total()
        s += '  notes %d/%d' % (t['match'], t['match'] + t['missing'])
        if self.lags: s += ' +%df' % median(self.lags)
        if self.loud: s += '  loud r%.2f' % self.loud[0]
        return s

    def report(self):
        print('frames compared: %d; identical in everything: %d' % (self.n, self.allsame))
        for f in FIELDS:
            line = '  %-7s identical in %d of %d' % (f, self.same[f], self.n)
            if f in self.blocks and self.blocks[f]:
                line += '  (blocks differing, frames: %s)' % ', '.join('%s %d' % kv for kv in sorted(self.blocks[f].items(), key=lambda x: -x[1])[:6])
            print(line)
        if self.diffs:
            print('  differing frames: ' + ', '.join(str(k) for k, _ in self.diffs[:20]) + (' ...' if len(self.diffs) > 20 else ''))
        tp = sum(self.pics.values())
        print('pictures the port drew: %d; same as MAME\'s of that frame %d, of the frame before/after %d, different %d' % (
            tp, self.pics['same'], self.pics['near'], self.pics['diff']))
        if self.picdiff: print('  ' + ', '.join('%d %s' % x for x in self.picdiff[:12]) + (' ...' if len(self.picdiff) > 12 else ''))
        t = self.notes_total()
        print('notes (from frame %s, when the sound board answered): %d of %d played on the Model 2, %d not, %d extra' % (
            self.sound_up, t['match'], t['match'] + t['missing'], t['missing'], t['extra']))
        print('  ' + ', '.join('%s %d/%d%s' % (VOICES[v], n['match'], n['match'] + n['missing'], ' +%d' % n['extra'] if n['extra'] else '')
                               for v, n in enumerate(self.notes) if n['match'] + n['missing'] + n['extra']))
        if self.lags:
            print('  played late by (frames): median %d; %s' % (median(self.lags), ', '.join('%d: %d' % kv for kv in sorted(self.lags.items())[:12])))
        if self.notediff: print('  first: ' + '; '.join('%d %s' % x for x in self.notediff[:6]))
        if self.loud: print('loudness per frame, whole run: correlation %.3f with the Model 2 %d frames late' % self.loud)


def tok(t): return t if t == 'hit' else ('%+.0fc' % t if t else 'on')
def median(h):
    n, acc = sum(h.values()), 0
    for v, c in sorted(h.items()):
        acc += c
        if acc * 2 >= n: return v
def corr(a, b):
    n = len(a); ma, mb = sum(a) / n, sum(b) / n
    sab = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    saa = sum((x - ma) ** 2 for x in a); sbb = sum((y - mb) ** 2 for y in b)
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def main():
    c = Check()
    rd = {s: Reader(os.path.join(D, s + '.rec')) for s in ('ref', 'port')}
    if not FOLLOW:
        for s in ('ref', 'port'):
            for l in rd[s].lines(): c.line(s, l)
        c.frames(); c.pictures(); c.match(final=True); c.loudness(window=0)
        c.report()
        return
    logf = open(os.path.join(D, 'check.log'), 'w')
    seen = [0, 0, 0]
    tick = 0
    while True:
        for s in ('ref', 'port'):
            for l in rd[s].lines(): c.line(s, l)
        c.frames(); c.pictures(); c.match()
        tick += 1
        if tick % 20 == 0: c.loudness()
        for i, (lst, fmt) in enumerate(((c.diffs, 'frame %d: %s\n'), (c.picdiff, 'frame %d: picture %s\n'), (c.notediff, 'frame %d: %s\n'))):
            for x in lst[seen[i]:]: logf.write(fmt % x)
            seen[i] = len(lst)
            if len(lst) > 5000: del lst[:4000]; seen[i] = len(lst)
        logf.flush()
        tmp = os.path.join(D, 'status.tmp')
        with open(tmp, 'w') as f: f.write(c.status() + '\n')
        os.replace(tmp, os.path.join(D, 'status.txt'))
        time.sleep(0.25)


if __name__ == '__main__':
    main()
