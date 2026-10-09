#!/usr/bin/env python3
"""Writes a .tzx file exercising every block type to stdout.

    python3 complete-tzx.py > complete-tzx.tzx
"""

import sys


def u8(b):
    return bytes((b & 0xff,))


def u16(w):
    return bytes((w & 0xff, (w >> 8) & 0xff))


def u24(three):
    return bytes((three & 0xff, (three >> 8) & 0xff, (three >> 16) & 0xff))


def u32(dw):
    return bytes((dw & 0xff, (dw >> 8) & 0xff, (dw >> 16) & 0xff, (dw >> 24) & 0xff))


def header():
    return b"ZXTape!\x1a" + u8(1) + u8(20)


def standard_speed_data_block(data, pause):
    return u8(0x10) + u16(pause) + u16(len(data)) + data


def turbo_speed_data_block(pilot_length, pilot_count, sync1_length, sync2_length,
                            zero_length, one_length, data, bits_in_last_byte, pause):
    return (
        u8(0x11) + u16(pilot_length) + u16(sync1_length) + u16(sync2_length) +
        u16(zero_length) + u16(one_length) + u16(pilot_count) +
        u8(bits_in_last_byte) + u16(pause) + u24(len(data)) + data
    )


def pure_tone_block(length, count):
    return u8(0x12) + u16(length) + u16(count)


def pulse_sequence_block(*data):
    return u8(0x13) + u8(len(data)) + b"".join(u16(x) for x in data)


def pure_data_block(zero_length, one_length, data, bits_in_last_byte, pause):
    return (
        u8(0x14) + u16(zero_length) + u16(one_length) +
        u8(bits_in_last_byte) + u16(pause) + u24(len(data)) + data
    )


def pause_block(pause):
    return u8(0x20) + u16(pause)


def group_start_block(name):
    return u8(0x21) + u8(len(name)) + name


def group_end_block():
    return u8(0x22)


def jump_block(offset):
    return u8(0x23) + u16(offset)


def loop_start_block(iterations):
    return u8(0x24) + u16(iterations)


def loop_end_block():
    return u8(0x25)


def stop_tape_if_in_48k_mode_block():
    return u8(0x2a) + u32(0)


def text_description_block(text):
    return u8(0x30) + u8(len(text)) + text


def message_block(text, time):
    return u8(0x31) + u8(time) + u8(len(text)) + text


def archive_info_block(*strings):
    data = b"".join(u8(s['id']) + u8(len(s['text'])) + s['text'] for s in strings)
    return u8(0x32) + u16(len(data)) + u8(len(strings)) + data


def hardware_type_block(*info):
    body = b"".join(u8(i['type']) + u8(i['id']) + u8(i['used']) for i in info)
    return u8(0x33) + u8(len(info)) + body


def custom_info_block(id_, data):
    if len(id_) < 16:
        id_ = id_ + b' ' * (16 - len(id_))
    elif len(id_) > 16:
        id_ = id_[:16]
    return u8(0x35) + id_ + u32(len(data)) + data


def main(argv):
    out = bytearray()

    out += header()

    out += standard_speed_data_block(b"\xaa", 2345)

    out += turbo_speed_data_block(1000, 5, 123, 456, 789, 400,
                                   b"\x00\xff\x55\xa0", 4, 987)

    out += pure_tone_block(535, 666)

    out += pulse_sequence_block(772, 297, 692)

    out += pure_data_block(552, 1639, b"\xff\x00\xfc", 6, 554)

    out += pause_block(618)

    out += group_start_block(b"Group Start")

    out += group_end_block()

    out += jump_block(2)

    out += pure_tone_block(303, 678)

    out += loop_start_block(3)

    out += pure_tone_block(837, 185)

    out += loop_end_block()

    out += stop_tape_if_in_48k_mode_block()

    out += text_description_block(b"Comment here")

    out += message_block(b"A message", 1)

    out += archive_info_block({'id': 0x00, 'text': b"Full title"},
                               {'id': 0x03, 'text': b"Year"})

    out += hardware_type_block({'type': 0x00, 'id': 0x01, 'used': 0x00},
                                {'type': 0x02, 'id': 0x02, 'used': 0x03})

    out += custom_info_block(b"Complete TZX", b"Arbitrary custom data")

    out += pure_tone_block(820, 941)

    sys.stdout.buffer.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
