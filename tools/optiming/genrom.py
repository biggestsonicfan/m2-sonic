#!/usr/bin/env python3
"""genrom.py out.bin: a Mega Drive cartridge that runs every 68000 opcode once, for timing
the port's 68000 against MAME's (run.sh). Not run: flow control (Bcc, DBcc, JMP, JSR, RTS,
RTE, RTR, TRAP, TRAPV), STOP, RESET, writes to SR, lines A and F.

Each opcode has a 32-byte slot: move #$2700,sr; movem.l ($8000).w,d0-a7 (the same
registers every time); the opcode; 10 x $7000, its extension words (immediate $7000,
abs.w $7000 and abs.l $00007000 in ROM, d16 = $7000, d8(An,D7.w) = +0) or, when not used,
moveq #0,d0. An exception (illegal opcode, CHK, division by zero) resumes at the next slot.
"""
import struct, sys

SLOT, OPOFF = 32, 10


def excluded(op):
    if op >> 12 in (0x6, 0xa, 0xf): return True                    # Bcc/BRA/BSR, lines A/F
    if op & 0xf0f8 == 0x50c8: return True                          # DBcc
    if 0x4e40 <= op <= 0x4e4f or op == 0x4e70 or 0x4e72 <= op <= 0x4e77: return True
    if 0x4e80 <= op <= 0x4eff: return True                         # JSR, JMP
    if 0x46c0 <= op <= 0x46ff or op in (0x007c, 0x027c, 0x0a7c): return True   # to SR
    return False


OPS = [op for op in range(65536) if not excluded(op)]
REGS = [0x00000001, 0x00010003, 0x80000005, 0x0000ffff, 0x7fff0007, 0x00000020, 0xffff0011, 0x00000002,
        0x00ffc000, 0x00ffc200, 0x00ffc400, 0x00ffc600, 0x00ffc800, 0x00ffca00, 0x00ffcc00, 0x00fff000]
HANDLER = 0x1e0


def build():
    rom = bytearray(0x200)
    struct.pack_into('>II', rom, 0, 0x00fff000, 0x200)
    for v in range(2, 64): struct.pack_into('>I', rom, v * 4, HANDLER)
    rom[0x100:0x110] = b'SEGA MEGA DRIVE '
    # move.l 2(sp),d0; addi.l #SLOT,d0; andi.w #-SLOT,d0; movea.l d0,a0; jmp (a0)
    h = bytes.fromhex('202f0002' '0680') + struct.pack('>I', SLOT) + bytes.fromhex('0240') + \
        struct.pack('>H', 0x10000 - SLOT) + bytes.fromhex('2040' '4ed0')
    rom[HANDLER:HANDLER + len(h)] = h
    init = bytearray(bytes.fromhex('46fc2700' '43f900ff8000'))     # move #$2700,sr; lea $ff8000,a1
    for r in REGS: init += bytes.fromhex('22fc') + struct.pack('>I', r)   # move.l #r,(a1)+
    # RAM ffa000-ffffff: a non-zero pattern (no division by zero)
    init += bytes.fromhex('41f900ffa000' '303c17ff' '20fc00030005' '51c8fffa')
    first = (0x200 + len(init) + 6 + SLOT - 1) & -SLOT
    init += bytes.fromhex('4ef9') + struct.pack('>I', first)
    rom += init
    rom += bytes(first - len(rom))
    for op in OPS:
        rom += bytes.fromhex('46fc2700' '4cf8ffff8000') + struct.pack('>H', op) + bytes.fromhex('7000') * 10
    rom += bytes.fromhex('60fe')
    rom += bytes((-len(rom)) % 0x20000)
    return rom, first


if __name__ == '__main__':
    rom, first = build()
    open(sys.argv[1], 'wb').write(rom)
    print('%d opcodes, first slot %x, %d bytes' % (len(OPS), first, len(rom)))
