#!/usr/bin/env python3
"""Wrap an assembled gfx1030 code object in an Xbox Series X|S shader container.

The layout written here is the one documented in docs/XBOX_SERIES_X.md
(DXBC -> DXIL part -> XDAT -> XBCH -> FSSM -> record -> code). The C++ reader
implements that document independently, so a disagreement between the two shows
up as a failing integration test rather than as a silently different shader.

The fixture is synthetic in one respect only: it is built from `llvm-mc` output
instead of a shipped title's shader pack, so the record metadata (wave size,
register counts) is chosen here to exercise the parser.

Usage: build_xbox.py compute.hsaco compute.bin
"""

import hashlib
import struct
import sys
import zlib

ABSENT = 0xDEADBEEFDEADBEEF
GRAPHICS_SLOTS = 6
RECORD_SIZE = 0x54
CODE_OFFSET_IN_PAYLOAD = 0x110

SHADER_KIND_COMPUTE = 5
SHADER_MODEL_MAJOR = 6
SHADER_MODEL_MINOR = 0

XDAT_HASH = 0x12
XDAT_XBCH = 0x0D


def read_elf_text(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise SystemExit(f"{path}: not a little-endian ELF64 object")

    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    headers = []
    for i in range(shnum):
        base = shoff + i * shentsize
        name, _, _, _, offset, size = struct.unpack_from("<IIQQQQ", data, base)
        headers.append((name, offset, size))

    strings_offset = headers[shstrndx][1]
    for name, offset, size in headers:
        start = strings_offset + name
        end = data.index(b"\0", start)
        if data[start:end] == b".text":
            return data[offset : offset + size]

    raise SystemExit(f"{path}: no .text section")


def build_fssm(code):
    payload = bytearray()

    def put64(at, value):
        while len(payload) < at + 8:
            payload.append(0)
        struct.pack_into("<Q", payload, at, value)

    def put32(at, value):
        while len(payload) < at + 4:
            payload.append(0)
        struct.pack_into("<I", payload, at, value)

    put64(0x00, GRAPHICS_SLOTS)
    for off in (0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x48):
        put64(off, ABSENT)

    record = 0x68
    put64(0x60, record)

    put64(record + 0x00, CODE_OFFSET_IN_PAYLOAD)
    put64(record + 0x08, len(code))
    put32(record + 0x14, 1 << 5)  # wave32
    put64(record + 0x28, 0)  # root layout table
    put64(record + 0x30, 0)  # used-resource table
    put32(record + 0x38, 0)
    put32(record + 0x3C, 0)
    put32(record + 0x4C, 0)  # LDS bytes
    put32(record + 0x50, 2 | (2 << 20))  # 2 VGPRs, 2 SGPRs

    stage_data = record + RECORD_SIZE
    put32(stage_data + 0x24, 2)  # COMPUTE_PGM_RSRC2: two user SGPRs
    put32(stage_data + 0x28, 64)
    put32(stage_data + 0x2C, 1)
    put32(stage_data + 0x30, 1)

    while len(payload) < CODE_OFFSET_IN_PAYLOAD:
        payload.append(0)
    payload += code

    header = b"FSSM" + struct.pack("<IQ", 0x101, len(payload))
    return header + bytes(payload)


def build_xbch(fssm):
    stream = zlib.compress(fssm, 9)
    header = b"XBCH" + struct.pack("<IIIII", 3, len(stream), 0, len(fssm), 0)
    return header + stream


def build_dxil_part(xbch):
    part = bytearray(b"\0" * 0x18)

    table_offset = 0x24
    hash_offset = table_offset + 2 * 12
    xbch_offset = hash_offset + 16
    digest = hashlib.sha256(xbch).digest()[:16]

    part += b"XDAT"
    part += struct.pack("<II", 2, table_offset)
    part += struct.pack("<III", XDAT_HASH, hash_offset, len(digest))
    part += struct.pack("<III", XDAT_XBCH, xbch_offset, len(xbch))
    part += digest
    part += xbch

    bitcode_offset = len(part)
    part += b"NOBC"

    version = (SHADER_KIND_COMPUTE << 16) | (SHADER_MODEL_MAJOR << 4) | SHADER_MODEL_MINOR
    struct.pack_into("<I", part, 0x00, version)
    struct.pack_into("<I", part, 0x04, len(part) // 4)
    part[0x08:0x0C] = b"DXIL"
    struct.pack_into("<I", part, 0x0C, 1)
    struct.pack_into("<I", part, 0x10, bitcode_offset)
    struct.pack_into("<I", part, 0x14, 4)
    part[0x18:0x1C] = b"XDAT"

    return bytes(part)


def build_container(part):
    chunk_hash = b"HASH" + struct.pack("<I", 16) + bytes(range(16))
    chunk_dxil = b"DXIL" + struct.pack("<I", len(part)) + part

    header = bytearray(b"DXBC" + b"\0" * 16 + struct.pack("<HHI", 1, 0, 0))
    header += b"\0" * 4  # total size, patched once the body is complete
    table_size = 4 * 2
    offset = 32 + table_size
    offsets = []
    for chunk in (chunk_hash, chunk_dxil):
        offsets.append(offset)
        offset += len(chunk)
        if offset % 4:
            offset += 4 - offset % 4

    header += struct.pack("<II", offsets[0], offsets[1])
    body = bytearray()
    for chunk in (chunk_hash, chunk_dxil):
        body += chunk
        while (32 + table_size + len(body)) % 4:
            body.append(0)

    total = 32 + table_size + len(body)
    struct.pack_into("<I", header, 24, total)
    struct.pack_into("<I", header, 28, 2)

    blob = bytes(header) + bytes(body)
    assert len(blob) == total
    return blob


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)

    code = read_elf_text(argv[1])
    assert len(code) % 4 == 0, "code size is not a multiple of 4"

    container = build_container(build_dxil_part(build_xbch(build_fssm(code))))
    open(argv[2], "wb").write(container)
    print(f"{argv[2]}: {len(code)} bytes of code in a {len(container)}-byte container")


if __name__ == "__main__":
    main(sys.argv)
