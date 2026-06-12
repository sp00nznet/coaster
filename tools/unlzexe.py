#!/usr/bin/env python3
"""UNLZEXE - decompress LZEXE v0.90 / v0.91 packed DOS executables.

A faithful Python port of the classic public-domain ``unlzexe.c``
(originally by Kou Yuan / mitaki, widely redistributed). LZEXE was a 1989-era
self-extracting EXE compressor by Fabrice Bellard; tons of early-90s DOS games
shipped their main binary packed with it. You cannot statically disassemble a
packed EXE - the on-disk bytes are a tiny decompressor stub plus a compressed
blob - so this is always step zero of a 16-bit recomp.

Output is a plain, fully-relocated MZ executable identical to what the LZEXE
stub would have produced in memory at load time.

Usage:
    python unlzexe.py PACKED.EXE OUTPUT.EXE
"""
import struct
import sys


def _u16(buf, off):
    return struct.unpack_from('<H', buf, off)[0]


class _Bits:
    """LZEXE control bit-stream: 16-bit words, LSB first, interleaved with the
    literal/length bytes in a single forward stream."""

    def __init__(self, data, pos):
        self.data = data
        self.pos = pos
        self.span = _u16(data, pos)
        self.pos += 2
        self.count = 16

    def bit(self):
        b = self.span & 1
        self.span >>= 1
        self.count -= 1
        if self.count == 0:
            self.span = _u16(self.data, self.pos)
            self.pos += 2
            self.count = 16
        return b

    def byte(self):
        b = self.data[self.pos]
        self.pos += 1
        return b


def unlzexe(data):
    ihead = list(struct.unpack_from('<14H', data, 0))
    if ihead[0] not in (0x5A4D, 0x4D5A):
        raise ValueError('not an MZ executable')
    # LZEXE-packed images always have e_ovno==0 and e_lfarlc==0x1c, with the
    # version marker sitting in the (otherwise unused) reloc-pointer area.
    if ihead[0x0D] != 0 or ihead[0x0C] != 0x1C:
        raise ValueError('not an LZEXE image (e_ovno/e_lfarlc mismatch)')
    marker = bytes(data[0x1C:0x20])
    if marker == b'LZ91':
        ver = 91
    elif marker == b'LZ90':
        ver = 90
    else:
        raise ValueError('unknown LZEXE marker %r' % marker)

    ohead = ihead[:]

    # --- info table sits at the start of the decompressor segment ---
    dec = (ihead[0x0B] + ihead[4]) << 4
    inf = list(struct.unpack_from('<8H', data, dec))
    ohead[0x0A] = inf[0]   # original IP
    ohead[0x0B] = inf[1]   # original CS
    ohead[0x08] = inf[2]   # original SP
    ohead[0x07] = inf[3]   # original SS
    ohead[0x0C] = 0x1C     # reloc table goes right after the 28-byte header

    # --- rebuild the relocation table ---
    relocs = bytearray()
    rel_count = 0
    if ver == 91:
        pos = dec + 0x158
        rel_off = 0
        rel_seg = 0
        while True:
            span = data[pos]
            pos += 1
            if span == 0:
                span = _u16(data, pos)
                pos += 2
                if span == 0:
                    rel_seg = (rel_seg + 0x0FFF) & 0xFFFF
                    continue
                if span == 1:
                    break
            rel_off = (rel_off + span) & 0xFFFF
            rel_seg = (rel_seg + ((rel_off & ~0x0F) >> 4)) & 0xFFFF
            rel_off &= 0x0F
            relocs += struct.pack('<HH', rel_off, rel_seg)
            rel_count += 1
    else:  # ver 90
        pos = dec + 0x19D
        rel_seg = 0
        while rel_seg != 0x10000:
            c = _u16(data, pos)
            pos += 2
            for _ in range(c):
                rel_off = _u16(data, pos)
                pos += 2
                relocs += struct.pack('<HH', rel_off, rel_seg & 0xFFFF)
                rel_count += 1
            rel_seg += 0x1000
    ohead[3] = rel_count

    # header padded out to the next 512-byte page
    hdr_end = 0x1C + len(relocs)
    pad = (0x200 - hdr_end) & 0x1FF
    hdr_bytes = hdr_end + pad
    ohead[4] = hdr_bytes >> 4

    # --- decompress the image ---
    in_pos = (ihead[0x0B] - inf[4] + ihead[4]) << 4
    bits = _Bits(data, in_pos)
    out = bytearray()
    while True:
        if bits.bit():
            out.append(bits.byte())
            continue
        if not bits.bit():                       # short match: len 2..5, 8-bit offset
            length = (bits.bit() << 1) | bits.bit()
            length += 2
            span = bits.byte() | 0xFF00
        else:                                    # long match: 13-bit offset
            lo = bits.byte()
            hi = bits.byte()
            span = lo | (((hi & ~0x07) << 5) | 0xE000)
            length = (hi & 0x07) + 2
            if length == 2:                      # escape: explicit length / end
                n = bits.byte()
                if n == 0:
                    break
                if n == 1:
                    continue
                length = n + 1
        offset = span - 0x10000                  # always negative
        base = len(out)
        for i in range(length):
            out.append(out[base + offset + i])
    loadsize = len(out)

    # --- finalize the output header ---
    if ihead[6] != 0:
        ohead[5] = (ohead[5] - (inf[5] + ((inf[6] + 15) >> 4) + 9)) & 0xFFFF
        if ihead[6] != 0xFFFF:
            ohead[6] = (ohead[6] - ((ihead[5] - ohead[5]) & 0xFFFF)) & 0xFFFF
    total = loadsize + hdr_bytes
    ohead[1] = total & 0x1FF
    ohead[2] = (total + 0x1FF) >> 9

    result = bytearray(struct.pack('<14H', *ohead))
    result += relocs
    result += b'\x00' * (hdr_bytes - len(result))
    result += out
    return bytes(result), {
        'version': ver, 'relocs': rel_count, 'loadsize': loadsize,
        'cs': inf[1], 'ip': inf[0], 'ss': inf[3], 'sp': inf[2],
        'hdr_bytes': hdr_bytes,
    }


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: unlzexe.py PACKED.EXE OUTPUT.EXE')
    with open(sys.argv[1], 'rb') as f:
        data = f.read()
    out, info = unlzexe(data)
    with open(sys.argv[2], 'wb') as f:
        f.write(out)
    print('LZEXE v0.%d  ->  %s' % (info['version'], sys.argv[2]))
    print('  unpacked image : %d bytes' % info['loadsize'])
    print('  relocations    : %d' % info['relocs'])
    print('  entry CS:IP    : %04X:%04X' % (info['cs'], info['ip']))
    print('  stack SS:SP    : %04X:%04X' % (info['ss'], info['sp']))


if __name__ == '__main__':
    main()
