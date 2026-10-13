# Renders Resources/Filey.svg into the PNG sizes the Linux icon theme takes and packs the Windows sizes into Resources/Filey.ico.
# Needs rsvg-convert (librsvg). Commit the outputs; builds never run this.
#   python3 Source/Tools/IconBake.py

import pathlib
import struct
import subprocess

root = pathlib.Path(__file__).resolve().parents[1] / "Resources"
svg = root / "Filey.svg"
theme_sizes = [16, 24, 32, 48, 64, 128, 256, 512]
# Explorer, the taskbar and Alt+Tab pick from these at 100% to 250% scale.
ico_sizes = [16, 20, 24, 32, 40, 48, 64, 256]


def render(size):
    return subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), str(svg)], check=True, capture_output=True).stdout


def write(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


pngs = {size: render(size) for size in sorted(set(theme_sizes + ico_sizes))}

icons = root / "Icons"
icons.mkdir(exist_ok=True)
for size in theme_sizes:
    write(icons / f"filey-{size}.png", pngs[size])

# An .ico is a directory of images; Windows Vista and later take PNG images in it at every size.
header = struct.pack("<HHH", 0, 1, len(ico_sizes))
offset = len(header) + 16 * len(ico_sizes)
entries, images = b"", b""
for size in ico_sizes:
    data = pngs[size]
    entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
    images += data
    offset += len(data)
write(root / "Filey.ico", header + entries + images)
print(f"{len(theme_sizes)} theme sizes and a {len(ico_sizes)}-size icon from {svg.name}")
