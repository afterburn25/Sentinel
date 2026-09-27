from pathlib import Path
import base64
import hashlib
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "resources" / "assets"
SOURCE_DIR = ASSETS / "splash-source"
OUT = ASSETS / "SARA-Splash.png"

EXPECTED_SHA256 = "2308f6f3241e08f19e09a663715963d959a06c531be2d9840c5cd2c9cc2258ed"
EXPECTED_SIZE = (1672, 941)

parts = sorted(SOURCE_DIR.glob("part*.b64"))
if len(parts) != 18:
    raise RuntimeError(f"Expected 18 approved splash source chunks, found {len(parts)}")

encoded = "".join(p.read_text(encoding="ascii").strip() for p in parts)
raw = base64.b64decode(encoded, validate=True)

digest = hashlib.sha256(raw).hexdigest()
if digest != EXPECTED_SHA256:
    raise RuntimeError(
        f"Approved SARA splash checksum mismatch: {digest} != {EXPECTED_SHA256}"
    )

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_bytes(raw)

with Image.open(OUT) as image:
    if image.format != "PNG":
        raise RuntimeError(f"SARA splash must be PNG, got {image.format}")
    if image.size != EXPECTED_SIZE:
        raise RuntimeError(
            f"SARA splash must be exactly {EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]}, got {image.size}"
        )

print(
    f"Restored exact approved SARA splash: {OUT} "
    f"{EXPECTED_SIZE[0]}x{EXPECTED_SIZE[1]} sha256={digest}"
)
