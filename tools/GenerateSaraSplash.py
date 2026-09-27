from PIL import Image, ImageDraw, ImageFont, ImageFilter
from pathlib import Path
import math
import random

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "resources" / "assets"
OUT = ASSETS / "SARA-Splash.png"
ICON = ASSETS / "SARA-Icon.png"

W, H = 1672, 941

def font(size, bold=False):
    candidates = [
        Path(r"C:\Windows\Fonts\segoeuib.ttf" if bold else r"C:\Windows\Fonts\segoeui.ttf"),
        Path(r"C:\Windows\Fonts\arialbd.ttf" if bold else r"C:\Windows\Fonts\arial.ttf"),
    ]
    for p in candidates:
        if p.exists():
            return ImageFont.truetype(str(p), size)
    return ImageFont.load_default()

# Deep navy background with subtle vertical glow.
img = Image.new("RGBA", (W, H), (2, 15, 35, 255))
px = img.load()
for y in range(H):
    for x in range(W):
        cx = (x - W * 0.50) / W
        cy = (y - H * 0.45) / H
        radial = max(0.0, 1.0 - math.sqrt(cx * cx * 1.3 + cy * cy * 1.9) * 2.1)
        horizon = math.exp(-((y - H * 0.86) / (H * 0.06)) ** 2)
        px[x, y] = (
            int(2 + 2 * radial),
            int(15 + 25 * radial + 2 * horizon),
            int(35 + 54 * radial + 18 * horizon),
            255,
        )

# Atmospheric blue/cyan glow layers.
glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow, "RGBA")
gd.ellipse((W*0.22, -H*0.20, W*0.78, H*0.98), fill=(0, 84, 181, 42))
gd.rectangle((0, H*0.855, W, H*0.875), fill=(0, 112, 235, 42))
glow = glow.filter(ImageFilter.GaussianBlur(55))
img = Image.alpha_composite(img, glow)

# Subtle circuit traces, nodes, and dotted grids.
overlay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(overlay, "RGBA")
random.seed(109)
for side in (0, 1):
    for row in range(7):
        y = 165 + row * 82 + random.randint(-15, 15)
        if side == 0:
            x0 = 0
            x1 = 105 + random.randint(0, 120)
            x2 = x1 + 70
            x3 = min(380, x2 + random.randint(45, 130))
        else:
            x0 = W
            x1 = W - 105 - random.randint(0, 120)
            x2 = x1 - 70
            x3 = max(W - 380, x2 - random.randint(45, 130))
        col = (0, 126, 255, 76)
        d.line((x0, y, x1, y), fill=col, width=2)
        d.line((x1, y, x2, y + (35 if row % 2 == 0 else -35)), fill=col, width=2)
        d.line((x2, y + (35 if row % 2 == 0 else -35), x3, y + (35 if row % 2 == 0 else -35)), fill=col, width=2)
        rr = 4
        d.ellipse((x3-rr, y + (35 if row % 2 == 0 else -35)-rr,
                   x3+rr, y + (35 if row % 2 == 0 else -35)+rr),
                  fill=(0, 207, 255, 160))

for side_x in (28, W - 180):
    for gy in range(5):
        for gx in range(6):
            x = side_x + gx * 28
            y = 25 + gy * 25
            d.ellipse((x, y, x+4, y+4), fill=(0, 96, 190, 72))

# Faint oversized fingerprint watermark using the real icon.
if ICON.exists():
    icon = Image.open(ICON).convert("RGBA")
    # Remove any accidental fully black background if source had one; preserve colored mark.
    wm = icon.copy()
    wm.thumbnail((920, 920), Image.Resampling.LANCZOS)
    alpha = wm.getchannel("A").point(lambda a: int(a * 0.11))
    wm.putalpha(alpha)
    img.alpha_composite(wm, ((W-wm.width)//2, -10))

    # Bright hero icon.
    hero = icon.copy()
    hero.thumbnail((365, 330), Image.Resampling.LANCZOS)
    # cyan aura
    aura = Image.new("RGBA", (hero.width+100, hero.height+100), (0,0,0,0))
    aura.alpha_composite(hero, (50, 50))
    aa = aura.getchannel("A").filter(ImageFilter.GaussianBlur(24)).point(lambda a: min(115, a))
    cyan = Image.new("RGBA", aura.size, (0, 223, 255, 0))
    cyan.putalpha(aa)
    hx = (W - aura.width)//2
    hy = 105
    img.alpha_composite(cyan, (hx, hy))
    img.alpha_composite(hero, ((W-hero.width)//2, 145))

d = ImageDraw.Draw(img, "RGBA")

# Main SARA wordmark; native-rendered text stays crisp at full resolution.
title_font = font(126, True)
subtitle_font = font(32, False)
loading_font = font(24, False)

title = "SARA"
bbox = d.textbbox((0, 0), title, font=title_font)
tx = (W - (bbox[2]-bbox[0])) // 2
ty = 495
# soft glow
for blur_offset, alpha in ((5, 45), (2, 80)):
    d.text((tx+blur_offset, ty+blur_offset), title, font=title_font, fill=(16, 93, 191, alpha))
d.text((tx, ty), title, font=title_font, fill=(238, 247, 255, 255))

subtitle = "Synthetic Adaptive Response Agent"
sb = d.textbbox((0,0), subtitle, font=subtitle_font)
sx = (W - (sb[2]-sb[0])) // 2
d.text((sx, 650), subtitle, font=subtitle_font, fill=(222, 237, 252, 244))

loading = "Loading..."
lb = d.textbbox((0,0), loading, font=loading_font)
lx = (W - (lb[2]-lb[0])) // 2
d.text((lx, 790), loading, font=loading_font, fill=(93, 193, 255, 235))

# Dim base track; the app paints the moving cyan segment and dots live.
track_w = 300
track_x = (W-track_w)//2
track_y = 835
d.rounded_rectangle((track_x, track_y, track_x+track_w, track_y+6), radius=3, fill=(18, 79, 137, 190))

# Thin floor glow.
floor = Image.new("RGBA", (W, H), (0,0,0,0))
fd = ImageDraw.Draw(floor, "RGBA")
fd.rectangle((0, 850, W, 854), fill=(0, 116, 242, 70))
floor = floor.filter(ImageFilter.GaussianBlur(18))
img = Image.alpha_composite(img, floor)

OUT.parent.mkdir(parents=True, exist_ok=True)
img.convert("RGB").save(OUT, "PNG", optimize=True)
with Image.open(OUT) as check:
    assert check.format == "PNG"
    assert check.size == (1672, 941), check.size
print(f"Generated {OUT} at {W}x{H}")
