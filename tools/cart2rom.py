#!/usr/bin/env python3
"""cart2rom.py <sonic_rom.h> <out_dir>  ->  <out_dir>/epr-19003.7 + epr-19004.8

Puts the cartridge (src/sonic_rom.h, tools/mdrom.py) in sfight's data EPROM pair, so it
does not take half of the program EPROMs: MAME sega/model2.cpp loads the pair with
ROM_LOAD32_WORD at "main_data" + 0x1000000, which the i960 reads at 0x03000000
(src/sonic.c MD_ROM_AT). The image is md_rom[] as the i960 sees it, each 16-bit word
little-endian, the even halfwords in epr-19003.7 and the odd ones in epr-19004.8, as the
program pair; 512 KB each, unused space 0xFF. Burn them in place of the two data EPROMs,
or put them beside the program EPROMs in a MAME rompath.
"""
import os, re, sys

SIZE = 0x80000                          # one EPROM; the pair holds 1 MB


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = open(sys.argv[1]).read()
    words = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', text[text.index('{') + 1:text.rindex('}')])]
    img = bytearray(b'\xff' * (2 * SIZE))
    if 2 * len(words) > len(img):
        sys.exit('%s: %d KB, more than the 1 MB data EPROM pair' % (sys.argv[1], len(words) // 512))
    for i, w in enumerate(words):
        img[2 * i] = w & 0xff
        img[2 * i + 1] = w >> 8
    for n, name in enumerate(('epr-19003.7', 'epr-19004.8')):
        out = bytearray(SIZE)
        for k in range(2):
            out[k::2] = img[2 * n + k::4]
        open(os.path.join(sys.argv[2], name), 'wb').write(out)
    print('%s: %d KB cartridge in the data EPROMs epr-19003.7 + epr-19004.8' % (sys.argv[2], len(words) // 512))


if __name__ == '__main__':
    main()
