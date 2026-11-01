"""Convert Apple's CgBI-optimized PNG into a conventional RGBA PNG."""

import argparse
import struct
import zlib
from pathlib import Path

from PIL import Image


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def chunks(data: bytes):
    position = len(PNG_SIGNATURE)
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4 : position + 8]
        payload = data[position + 8 : position + 8 + length]
        yield kind, payload
        position += 12 + length


def make_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def convert(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    parsed = list(chunks(data))
    if not any(kind == b"CgBI" for kind, _ in parsed):
        destination.write_bytes(data)
        return

    idat = b"".join(payload for kind, payload in parsed if kind == b"IDAT")
    scanlines = zlib.decompress(idat, -15)
    conventional = bytearray(PNG_SIGNATURE)
    inserted_idat = False
    for kind, payload in parsed:
        if kind in (b"CgBI", b"iDOT"):
            continue
        if kind == b"IDAT":
            if not inserted_idat:
                conventional += make_chunk(b"IDAT", zlib.compress(scanlines, 9))
                inserted_idat = True
            continue
        conventional += make_chunk(kind, payload)

    temporary = destination.with_suffix(".cgbi-intermediate.png")
    temporary.write_bytes(conventional)
    with Image.open(temporary).convert("RGBA") as image:
        pixels = bytearray(image.tobytes())
        for index in range(0, len(pixels), 4):
            blue, green, red, alpha = pixels[index : index + 4]
            if alpha:
                red = min(255, (red * 255 + alpha // 2) // alpha)
                green = min(255, (green * 255 + alpha // 2) // alpha)
                blue = min(255, (blue * 255 + alpha // 2) // alpha)
            pixels[index : index + 4] = bytes((red, green, blue, alpha))
        result = Image.frombytes("RGBA", image.size, bytes(pixels))
        result.save(destination, "PNG", optimize=True)
    temporary.unlink()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    convert(args.source, args.destination)


if __name__ == "__main__":
    main()
