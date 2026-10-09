#!/usr/bin/env python3
"""Writes a named .wav test fixture to stdout.

    python3 wav-fixtures.py <fixture> > fixture.wav
"""

import struct
import sys

FIXTURES = {
    'mono-threshold': (1, 22050, [
        2000, -2000, 12000, -12000,
        32767, -32768, -4000, 8000,
    ]),
    'stereo-mixdown': (2, 22050, [
        2000, 2000,
        2000, -2000,
        -2000, 2000,
        -2000, -2000,
        4000, 2000,
        2000, 4000,
        1, 0,
        0, 3,
    ]),
}


def main(argv):
    if len(argv) < 2:
        sys.exit("usage: %s <fixture>" % argv[0])

    fixture = argv[1]
    if fixture not in FIXTURES:
        sys.exit("unknown fixture '%s'" % fixture)
    channels, sample_rate, samples = FIXTURES[fixture]

    bits_per_sample = 16
    bytes_per_sample = bits_per_sample // 8
    block_align = channels * bytes_per_sample
    byte_rate = sample_rate * block_align
    data = struct.pack('<%dh' % len(samples), *samples)
    data_length = len(data)
    riff_length = 36 + data_length

    out = bytearray()
    out += b"RIFF"
    out += struct.pack('<I', riff_length)
    out += b"WAVE"

    out += b"fmt "
    out += struct.pack('<IHHIIHH', 16, 1, channels, sample_rate, byte_rate,
                        block_align, bits_per_sample)

    out += b"data"
    out += struct.pack('<I', data_length)
    out += data

    sys.stdout.buffer.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
