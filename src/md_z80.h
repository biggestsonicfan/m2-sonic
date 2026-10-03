/* md_z80.h - Sonic 1's Z80 DAC driver, timed as MAME runs it, for what the 68000 sees of it.
 *
 * The port has no Z80: the drums play on the SCSP (md_snd.h). But the 68000's sound
 * driver asks the Z80 for the bus, then reads $A01FFD, which the Z80 sets while it plays
 * a nibble ($FD) and clears after ($1F); if it is set, the 68000 lets the bus go and tries
 * again, which costs it time. So that the 68000 runs as long as in MAME, this follows the
 * Z80 program (the driver the 68000 loads: poll $1FFF, play the sample it names, or the
 * SEGA voice) instruction by instruction, and its writes to $1FFD/$1FFF, in MAME's time:
 *
 * - The 68000's write to $A11100 makes MAME run the Z80 up to the 68000's time (the bus
 *   cycle's start, md_now() - MZ_TAP). The Z80 runs whole cycles, so to the last Z80 clock
 *   edge before it (E); an instruction that began before E runs on, but an access of it at
 *   E or later waits (MAME's Z80 stops between M-cycles) and is done in the Z80's next
 *   timeslice: at the next scheduler boundary (a scanline: MAME's VDP timer, MZ_LINE) or
 *   the next bus request write, whichever comes first. The 68000 runs on until then: it
 *   reads the old $1FFD, and the Z80 reads the $1FFF it wrote before then.
 * - If the 68000 had already used its slice when it wrote (less than a 68000 cycle before
 *   a scanline boundary), the Z80 runs to the boundary.
 * - While it has no bus the Z80 waits at its next instruction; once it has it again it goes
 *   on from the last Z80 clock edge before the 68000's write.
 * - Times are MAME's: attoseconds (s64, relative to the frame mz.ep began), with its
 *   periods for a 68000 cycle, a Z80 cycle, a frame and a line.
 * Checked against MAME's Z80 (tools: the 817 reads of $1FFD and all 224231 writes to
 * $1FFD/$1FFF in 700 frames: all the same). The idle loops (the poll, djnz, a whole
 * sample nibble pair) are skipped in one step, so a bus request costs a few dozen steps. */
#ifndef MD_Z80_H
#define MD_Z80_H

typedef long long s64;
#define MZ_P68   130370396637LL         /* attoseconds a 68000 cycle: 1e18 / 7670453, truncated */
#define MZ_PZ    279365114840LL         /* a Z80 cycle: 1e18 / 3579545 */
#define MZ_FRAME 16688154500083112LL    /* MAME's frame (the screen's refresh) */
#define MZ_LINE  63695246183523LL       /* its scanline timer: the frame / 262 */
#define MZ_TAP   8                      /* the bus cycle starts 8 cycles before the instruction
                                         * ends (move.w #,abs.l; btst #,abs.l; move.b d0,abs.l) */

/* the driver's code, as instructions: cycles | kind << 5 | value << 8. An access is in an
 * instruction's last M-cycle: 3 cycles before its end. */
enum { MZ_N = 0, MZ_W1FFD = 1 << 5, MZ_W1FFF = 2 << 5, MZ_WA = 3 << 5, MZ_R = 4 << 5 };
#define MZ_V(v) ((v) << 8)
static const unsigned short mz_code[] = {
#define MZ_INIT 0                       /* 0000: di x3, ld sp, ld ix, xor a, ld (1ffd),a ... */
    4, 4, 4, 10, 14, 4, 13 | MZ_W1FFD | MZ_V(0), 13 | MZ_W1FFF | MZ_V(0), 7, 13, 7,
    7, 13, 4, 13, 7, 13, 4, 13, 7, 13, 4, 13, 7, 13, 4, 13,
    7, 13, 4, 13, 7, 13, 4, 13, 7, 13, 4, 13, 7, 13, 4, 8, 12, 0,
#define MZ_TOP 45                       /* 0032: ld hl,$1fff */
    10, 0,
#define MZ_POLL 47                      /* 0035: ld a,(hl); or a; jp p */
    7 | MZ_R, 4, 10, 0,
#define MZ_CMD 51                       /* 003a: sub $81; ld (hl),a; cp 6 */
    7, 7 | MZ_WA, 7, 0,
#define MZ_SAMPLE 55                    /* 003f: jr nc (not taken) ... ld (hl),0; exx; ld h,0 */
    7, 10, 14, 8, 8, 8, 7, 4, 15, 19, 19, 19, 19, 4, 7, 10, 7 | MZ_W1FFD | MZ_V(0x80),
    19, 7, 19, 19, 10 | MZ_W1FFD | MZ_V(0), 4, 7, 0,
#define MZ_H0 80                        /* 0077: the high nibble, ld (hl),l ... ld b,c */
    7, 7, 4, 4, 4, 4, 7, 4, 7, 4, 4, 4, 7 | MZ_W1FFD | MZ_V(0xfd), 19, 19,
    7 | MZ_W1FFD | MZ_V(0x1f), 4, 0,
#define MZ_H1 98                       /* 0090: the low nibble */
    4, 7, 7, 7, 4, 7, 4, 4, 4, 7 | MZ_W1FFD | MZ_V(0xfd), 19, 19, 7 | MZ_W1FFD | MZ_V(0x1f), 4, 0,
#define MZ_END 113                      /* 00a6: exx; ld a,($1fff); bit 7,a; jp nz */
    4, 13 | MZ_R, 8, 10, 0,
#define MZ_NEXT 118                     /* 00af: inc de; dec bc; ld a,c; or b; jp nz */
    6, 6, 4, 4, 10, 0,
#define MZ_JP 124                       /* jp $0032 */
    10, 0,
#define MZ_SEGA0 126                    /* 00b9: jr nc (taken); ld de; ld hl; ld c */
    12, 10, 10, 7, 0,
#define MZ_SEGA 131                     /* 00c1: ld a,(de); ld (ix+0),c; ld (ix+1),a; ld b */
    7, 19, 19, 7, 0,
#define MZ_SEGA2 136                    /* 00cc: inc de; dec hl; ld a,l; or h; jp nz */
    6, 6, 4, 4, 10, 0,
};
#define MZ_ENDCODE 141                  /* the last run's 0 */
#define MZ_DJNZ 0xffff                  /* ip while in a djnz loop (b: mz.b) */
#define MZ_PAIR(d) (285 + 2 * (d))      /* MZ_H0 .. MZ_NEXT, d: a djnz loop's cycles */
#define MZ_SEGA_IT(d) (82 + (d))        /* MZ_SEGA .. MZ_SEGA2 */

static struct {
    s64 t;                  /* the Z80's time: its next instruction (or the end of the waiting one) */
    s64 dl;                 /* a waiting access is done by then */
    s64 ep_rel;             /* the 68000's time at ep_n68, from the frame the times count from */
    u32 ep_n68;
    u32 n;                  /* nibble pairs left (the sample), or SEGA bytes */
    unsigned short ip, ret; /* the instruction (mz_code), and where a djnz loop goes after */
    unsigned short b;       /* djnz count left */
    u8 on, busreq, reset, pend;
    u8 a, rv;               /* the command (a - $81), the last value read */
    u8 rate, sb;            /* djnz counts: the sample's rate, the SEGA voice's */
} mz;

/* small rather than fast: the program ROM is full, and these run a few times a line */
#pragma GCC push_options
#pragma GCC optimize("Os")

/* n / d for 0 <= n < 2^58 and d >= MZ_PZ, without a 64-bit divide (the i960 build has none,
 * and it would be slow): estimates from the top bits (d >> 26 >= 4162) close in in a few steps */
static u32 mz_div(s64 n, s64 d) {
    u32 q = 0, dd = (u32)(d >> 26) + 1;
    while (n >= d) {
        u32 e = (u32)(n >> 26) / dd;
        if (!e) e = 1;
        q += e; n -= (s64)e * d;
    }
    return q;
}

static inline int mz_djnz_cyc(int b) { return (b ? b : 256) * 13 - 5; }

static s64 mz_next_line(s64 t) {        /* the first scanline timer boundary after t */
    s64 f = t >= 0 ? mz_div(t, MZ_FRAME) : -1, b = f * MZ_FRAME, k = mz_div(t - b, MZ_LINE) + 1;
    return k < 262 ? b + k * MZ_LINE : b + MZ_FRAME;
}

/* the 68000's time at its bus cycle, as MAME's Z80 sees it (see above) */
static s64 mz_time(void) {
    s64 t = mz.ep_rel + (s64)(int)(md_now() - MZ_TAP - mz.ep_n68) * MZ_P68, s = mz_next_line(t);
    return s - t < MZ_P68 ? s : t;
}

/* the instruction at ip: its cycles, kind and value */
static inline u32 mz_ins(void) {
    if (mz.ip == MZ_DJNZ) return mz.b > 1 ? 13 : 8;
    return mz_code[mz.ip];
}

/* the instruction at ip is done: on to the next */
static void mz_adv(void) {
    if (mz.ip == MZ_DJNZ) {
        if (--mz.b) return;
        mz.ip = mz.ret;
        return;
    }
    if (mz_code[++mz.ip]) return;
    switch (mz.ip) {                    /* the end of a run (its 0): where the code goes */
    case MZ_TOP - 1: mz.ip = MZ_TOP; break;
    case MZ_POLL - 1: mz.ip = MZ_POLL; break;
    case MZ_CMD - 1:
        if (mz.rv & 0x80) { mz.a = (u8)(mz.rv - 0x81); mz.ip = MZ_CMD; }
        else mz.ip = MZ_POLL;
        break;
    case MZ_SAMPLE - 1:
        if (mz.a >= 6) {                /* the SEGA voice: hl bytes, b from the code */
            mz.n = md_zram[0xbd] | md_zram[0xbe] << 8; if (!mz.n) mz.n = 0x10000;
            mz.sb = md_zram[0xc9];
            mz.ip = MZ_SEGA0;
        } else {                        /* the sample table at $00D6: pointer, length, rate */
            const u8 *e = &md_zram[0xd6 + 8 * mz.a];
            mz.n = e[2] | e[3] << 8; if (!mz.n) mz.n = 0x10000;
            mz.rate = e[4];
            mz.ip = MZ_SAMPLE;
        }
        break;
    case MZ_H0 - 1: mz.ip = MZ_H0; break;
    case MZ_H1 - 1: mz.b = mz.rate ? mz.rate : 256; mz.ret = MZ_H1; mz.ip = MZ_DJNZ; break;
    case MZ_END - 1: mz.b = mz.rate ? mz.rate : 256; mz.ret = MZ_END; mz.ip = MZ_DJNZ; break;
    case MZ_NEXT - 1: mz.ip = (mz.rv & 0x80) ? MZ_TOP : MZ_NEXT; break;
    case MZ_JP - 1: mz.ip = --mz.n ? MZ_H0 : MZ_JP; break;
    case MZ_SEGA0 - 1: mz.ip = MZ_TOP; break;
    case MZ_SEGA - 1: mz.ip = MZ_SEGA; break;
    case MZ_SEGA2 - 1: mz.b = mz.sb ? mz.sb : 256; mz.ret = MZ_SEGA2; mz.ip = MZ_DJNZ; break;
    case MZ_ENDCODE: mz.ip = --mz.n ? MZ_SEGA : MZ_JP; break;
    }
}

static void mz_access(u32 e) {
    switch (e & 0xe0) {
    case MZ_W1FFD: md_zram[0x1ffd] = (u8)(e >> 8); break;
    case MZ_W1FFF: md_zram[0x1fff] = (u8)(e >> 8); break;
    case MZ_WA:    md_zram[0x1fff] = mz.a; break;
    case MZ_R:     mz.rv = md_zram[0x1fff]; break;
    }
}

/* the waiting access, done as the Z80 runs again */
static void mz_flush(void) {
    if (!mz.pend) return;
    mz.pend = 0;
    mz_access(mz_ins());
    mz_adv();
}

/* the last Z80 clock edge at or before t */
static inline s64 mz_edge(s64 t) { return t > mz.t ? mz.t + (s64)mz_div(t - mz.t, MZ_PZ) * MZ_PZ : mz.t; }

/* run the Z80 to the 68000's time t: the instructions that begin before the edge E */
static void mz_run(s64 t) {
    s64 e;
    if (!mz.on || mz.reset) return;
    e = mz_edge(t);
    mz_flush();
    if (mz.busreq) return;
    while (mz.t < e) {
        u32 in, c;
        s64 k;
        /* loops that change nothing the 68000 sees, whole ones at a time */
        if (mz.ip == MZ_DJNZ && mz.b > 1) {
            k = mz_div(e - mz.t, 13 * MZ_PZ);
            if (k > mz.b - 1) k = mz.b - 1;
            mz.t += k * 13 * MZ_PZ; mz.b -= (unsigned short)k;
            if (mz.t >= e) break;
        } else if (!(md_zram[0x1fff] & 0x80)) {
            if (mz.ip == MZ_POLL) {
                k = mz_div(e - mz.t, 21 * MZ_PZ);
                mz.t += k * 21 * MZ_PZ;
                if (mz.t >= e) break;
            } else if (mz.ip == MZ_H0 && mz.n > 1) {
                k = mz_div(e - mz.t, MZ_PAIR(mz_djnz_cyc(mz.rate)) * MZ_PZ);
                if (k > mz.n - 1) k = mz.n - 1;
                if (k) md_zram[0x1ffd] = 0x1f;
                mz.t += k * MZ_PAIR(mz_djnz_cyc(mz.rate)) * MZ_PZ; mz.n -= (u32)k;
                if (mz.t >= e) break;
            }
        }
        if (mz.ip == MZ_SEGA && mz.n > 1) {
            k = mz_div(e - mz.t, MZ_SEGA_IT(mz_djnz_cyc(mz.sb)) * MZ_PZ);
            if (k > mz.n - 1) k = mz.n - 1;
            mz.t += k * MZ_SEGA_IT(mz_djnz_cyc(mz.sb)) * MZ_PZ; mz.n -= (u32)k;
            if (mz.t >= e) break;
        }
        in = mz_ins(); c = in & 31;
        if (in & 0xe0) {
            s64 at = mz.t + (s64)(c - 3) * MZ_PZ;
            mz.t += c * MZ_PZ;
            if (at >= e) { mz.pend = 1; mz.dl = mz_next_line(t); return; }
            mz_access(in);
        } else mz.t += c * MZ_PZ;
        mz_adv();
    }
}

/* the 68000 is about to read or write Z80 RAM: a waiting access done by now is done */
static void mz_sync(void) {
    if (mz.pend && mz_time() >= mz.dl) mz_flush();
}

/* the 68000 writes $A11100 (req: it asks for the bus) or $A11200 (rst: holds the Z80 in reset) */
static void mz_busreq(int req) {
    s64 t = mz_time();
    if (mz.pend && t >= mz.dl) mz_flush();
    mz_run(t);
    if (mz.busreq && !req) { s64 e = mz_edge(t); if (e > mz.t) mz.t = e; }
    mz.busreq = (u8)req;
}
static void mz_reset_w(int rst) {
    s64 t = mz_time();
    if (mz.pend && t >= mz.dl) mz_flush();
    mz_run(t);
    if (mz.reset && !rst && md_zram[0] == 0xf3 && md_zram[3] == 0x31) {
        /* out of reset with the driver loaded: it starts at 0 */
        mz.t = mz_edge(t); mz.ip = MZ_INIT; mz.pend = 0; mz.on = 1;
    }
    mz.reset = (u8)rst;
}

/* once a frame: keep the times small (they count from a frame boundary behind the 68000) */
static void mz_frame(void) {
    s64 t = mz.ep_rel + (s64)(int)(md_now() - mz.ep_n68) * MZ_P68;
    while (t >= 2 * MZ_FRAME) {
        t -= MZ_FRAME; mz.t -= MZ_FRAME; mz.dl -= MZ_FRAME;
    }
    if (mz.t < -4 * MZ_FRAME) mz.t += (s64)mz_div(-mz.t - MZ_FRAME, MZ_PZ) * MZ_PZ;  /* stopped long ago */
    mz.ep_rel = t; mz.ep_n68 = md_now();
}

static void mz_init(void) {
    mz.t = 0; mz.dl = 0; mz.ep_rel = 0; mz.ep_n68 = 0;
    mz.on = 0; mz.busreq = 0; mz.reset = 1; mz.pend = 0; mz.ip = MZ_INIT;
}

#pragma GCC pop_options

#endif /* MD_Z80_H */
