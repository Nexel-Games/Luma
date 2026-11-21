"""Generates the asset-import test fixtures (T-0003).

The committed files in this folder were produced by this script; re-run it only
to regenerate them (needs Python 3 with Pillow + NumPy, and ffmpeg on PATH):

    python Tests/Fixtures/Import/make_fixtures.py

Contents (tests assert these exact facts):
- Scene "Robot", Y-up, metres, two meshes, two materials:
  * mesh "Body": axis-aligned box 1 x 1 x 1 centred at (0, 0.5, 0), 24 vertices,
    36 indices, material "Paint" (baseColorTexture T_Paint_D.png,
    normalTexture T_Paint_N.png, metallic 0, roughness 0.6).
  * mesh "Arm": box 0.2 x 1 x 0.2 centred at (1, 0.5, 0) (node translation
    (1, 0, 0)), 24 vertices, 36 indices, material "Metal" (baseColorFactor
    0.8 0.8 0.8 1, metallic 1, roughness 0.3, no textures).
  robot.gltf + robot.bin + external PNGs; robot.glb with both PNGs embedded;
  robot.obj + robot.mtl (same geometry baked to world space, same materials).
- T_Paint_D.png 64x64 RGB checker (8 px cells, (200,40,40)/(240,200,200)).
- T_Paint_N.png 64x64 RGB flat normal (128,128,255).
- tone_mono.wav: 1.0 s, 44100 Hz, mono, 16-bit PCM, 440 Hz sine.
- tone_stereo.mp3: 1.0 s, 44100 Hz, stereo, 128 kbps, 440 Hz sine.
- img_rgba.png / img.tga 32x16 RGBA; img.jpg / img.bmp / img.psd 32x16 RGB:
  pixel (x, y) = (x*8, y*16, 128) (alpha 255 except img_rgba.png/img.tga
  where alpha = x*8).
- img.hdr 4x2 float RGB, every pixel (2.0, 0.5, 0.25).
- img.exr 4x2 32-bit float RGB, every pixel (2.0, 0.5, 0.25).
"""

import json
import os
import struct
import subprocess

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))


def path(name):
    return os.path.join(HERE, name)


# --------------------------------------------------------------------------- images
def make_textures():
    d = np.zeros((64, 64, 3), np.uint8)
    for y in range(64):
        for x in range(64):
            d[y, x] = (200, 40, 40) if ((x // 8) + (y // 8)) % 2 == 0 else (240, 200, 200)
    Image.fromarray(d, "RGB").save(path("T_Paint_D.png"))
    n = np.full((64, 64, 3), (128, 128, 255), np.uint8)
    Image.fromarray(n, "RGB").save(path("T_Paint_N.png"))


def grid(alpha):
    a = np.zeros((16, 32, 4), np.uint8)
    for y in range(16):
        for x in range(32):
            a[y, x] = (x * 8, y * 16, 128, x * 8 if alpha else 255)
    return a


def write_psd(fname, rgb):
    """Minimal PSD: 8-bit RGB, no layers, raw image data (planar)."""
    h, w, _ = rgb.shape
    out = bytearray()
    out += b"8BPS" + struct.pack(">H", 1) + b"\0" * 6
    out += struct.pack(">HIIHH", 3, h, w, 8, 3)  # channels, height, width, depth, RGB
    out += struct.pack(">I", 0)  # color mode data
    out += struct.pack(">I", 0)  # image resources
    out += struct.pack(">I", 0)  # layer and mask info
    out += struct.pack(">H", 0)  # raw compression
    for c in range(3):
        out += rgb[:, :, c].tobytes()
    with open(fname, "wb") as f:
        f.write(out)


def write_hdr(fname, w, h, rgb):
    """Radiance RGBE, flat (non-RLE) scanlines (width < 8)."""
    def rgbe(r, g, b):
        m = max(r, g, b)
        if m < 1e-32:
            return bytes((0, 0, 0, 0))
        mant, e = np.frexp(m)
        scale = mant * 256.0 / m
        return bytes((int(r * scale), int(g * scale), int(b * scale), int(e + 128)))
    with open(fname, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(f"-Y {h} +X {w}\n".encode())
        px = rgbe(*rgb)
        f.write(px * (w * h))


def make_images():
    rgba = grid(True)
    rgb = grid(False)[:, :, :3]
    Image.fromarray(rgba, "RGBA").save(path("img_rgba.png"))
    Image.fromarray(rgba, "RGBA").save(path("img.tga"))
    Image.fromarray(rgb, "RGB").save(path("img.jpg"), quality=95)
    Image.fromarray(rgb, "RGB").save(path("img.bmp"))
    write_psd(path("img.psd"), rgb)
    write_hdr(path("img.hdr"), 4, 2, (2.0, 0.5, 0.25))
    # EXR via ffmpeg from a float PFM (PF = colour, little-endian negative scale).
    pfm = path("_tmp.pfm")
    with open(pfm, "wb") as f:
        f.write(b"PF\n4 2\n-1.0\n")
        f.write(struct.pack("<" + "f" * 24, *([2.0, 0.5, 0.25] * 8)))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", pfm, "-c:v", "exr",
                    "-pix_fmt", "gbrpf32le", "-compression", "0", path("img.exr")], check=True)
    os.remove(pfm)


# --------------------------------------------------------------------------- audio
def make_audio():
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=44100:duration=1", "-ac", "1",
                    "-c:a", "pcm_s16le", path("tone_mono.wav")], check=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=44100:duration=1", "-ac", "2",
                    "-c:a", "libmp3lame", "-b:a", "128k", path("tone_stereo.mp3")], check=True)


# --------------------------------------------------------------------------- geometry
FACES = [  # normal, tangent-u axis, tangent-v axis
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
]


def box(size, centre):
    """24 vertices (4 per face, CCW from outside), 36 indices."""
    sx, sy, sz = (s * 0.5 for s in size)
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in FACES:
        base = len(pos)
        for cu, cv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [n[i] + cu * u[i] + cv * v[i] for i in range(3)]
            pos.append([centre[0] + p[0] * sx, centre[1] + p[1] * sy, centre[2] + p[2] * sz])
            nrm.append(list(n))
            uv.append([(cu + 1) * 0.5, 1.0 - (cv + 1) * 0.5])  # top-left UV origin
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


MESHES = [
    # name, size, local centre (glTF node local), node translation, material index
    ("Body", (1.0, 1.0, 1.0), (0.0, 0.5, 0.0), (0.0, 0.0, 0.0), 0),
    ("Arm", (0.2, 1.0, 0.2), (0.0, 0.5, 0.0), (1.0, 0.0, 0.0), 1),
]


def gltf_doc(embed):
    blobs = bytearray()
    views, accessors, meshes, nodes = [], [], [], []

    def add_view(data, target=None):
        while len(blobs) % 4:
            blobs.append(0)
        views.append({"buffer": 0, "byteOffset": len(blobs), "byteLength": len(data),
                      **({"target": target} if target else {})})
        blobs.extend(data)
        return len(views) - 1

    for name, size, centre, translation, mat in MESHES:
        pos, nrm, uv, idx = box(size, centre)
        p = np.array(pos, np.float32)
        acc = {}
        for key, arr, typ in (("POSITION", p, "VEC3"), ("NORMAL", np.array(nrm, np.float32), "VEC3"),
                              ("TEXCOORD_0", np.array(uv, np.float32), "VEC2")):
            v = add_view(arr.tobytes(), 34962)
            a = {"bufferView": v, "componentType": 5126, "count": len(arr), "type": typ}
            if key == "POSITION":
                a["min"] = p.min(axis=0).tolist()
                a["max"] = p.max(axis=0).tolist()
            accessors.append(a)
            acc[key] = len(accessors) - 1
        iv = add_view(np.array(idx, np.uint32).tobytes(), 34963)
        accessors.append({"bufferView": iv, "componentType": 5125, "count": len(idx), "type": "SCALAR"})
        meshes.append({"name": name, "primitives": [{"attributes": acc, "indices": len(accessors) - 1,
                                                     "material": mat}]})
        nodes.append({"name": name, "mesh": len(meshes) - 1, "translation": list(translation)})

    images = []
    for fname in ("T_Paint_D.png", "T_Paint_N.png"):
        if embed:
            with open(path(fname), "rb") as f:
                v = add_view(f.read())
            images.append({"name": fname[:-4], "bufferView": v, "mimeType": "image/png"})
        else:
            images.append({"name": fname[:-4], "uri": fname})

    doc = {
        "asset": {"version": "2.0", "generator": "Luma fixture script"},
        "scene": 0,
        "scenes": [{"name": "Robot", "nodes": [0, 1]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": [
            {"name": "Paint", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                       "metallicFactor": 0.0, "roughnessFactor": 0.6},
             "normalTexture": {"index": 1}},
            {"name": "Metal", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.8, 0.8, 1.0],
                                                       "metallicFactor": 1.0, "roughnessFactor": 0.3}},
        ],
        "textures": [{"source": 0, "sampler": 0}, {"source": 1, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "images": images,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blobs)}],
    }
    return doc, bytes(blobs)


def make_gltf():
    doc, blob = gltf_doc(embed=False)
    doc["buffers"][0]["uri"] = "robot.bin"
    with open(path("robot.bin"), "wb") as f:
        f.write(blob)
    with open(path("robot.gltf"), "w") as f:
        json.dump(doc, f, indent=1)

    doc, blob = gltf_doc(embed=True)
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    blob += b"\0" * ((4 - len(blob) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path("robot.glb"), "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(blob), 0x004E4942) + blob)


def make_obj():
    lines = ["# Luma import fixture", "mtllib robot.mtl"]
    vbase = 1
    for name, size, centre, translation, mat in MESHES:
        world = [centre[i] + translation[i] for i in range(3)]
        pos, nrm, uv, idx = box(size, world)
        lines.append(f"o {name}")
        lines += [f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}" for p in pos]
        lines += [f"vt {t[0]:.6f} {1.0 - t[1]:.6f}" for t in uv]  # OBJ UV origin bottom-left
        lines += [f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}" for n in nrm]
        lines.append("usemtl " + ("Paint" if mat == 0 else "Metal"))
        for i in range(0, len(idx), 3):
            a, b, c = (vbase + idx[i + k] for k in range(3))
            lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
        vbase += len(pos)
    with open(path("robot.obj"), "w") as f:
        f.write("\n".join(lines) + "\n")
    with open(path("robot.mtl"), "w") as f:
        f.write("newmtl Paint\nKd 1.0 1.0 1.0\nNs 64\nmap_Kd T_Paint_D.png\nnorm T_Paint_N.png\n\n"
                "newmtl Metal\nKd 0.8 0.8 0.8\nKs 1.0 1.0 1.0\nNs 400\n")


if __name__ == "__main__":
    make_textures()
    make_images()
    make_audio()
    make_gltf()
    make_obj()
    print("fixtures written to", HERE)
