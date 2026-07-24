#!/usr/bin/env python3
import json
from pathlib import Path

package_path = Path("package.json")
font_path = Path("resources/fonts/Modak-Regular.ttf")

if not package_path.exists():
    raise SystemExit("package.json wurde im aktuellen Ordner nicht gefunden.")

if not font_path.exists():
    raise SystemExit(
        "Font nicht gefunden: resources/fonts/Modak-Regular.ttf"
    )

data = json.loads(package_path.read_text(encoding="utf-8"))
pebble = data.setdefault("pebble", {})
resources = pebble.setdefault("resources", {})
media = resources.setdefault("media", [])

media[:] = [
    item for item in media
    if not str(item.get("name", "")).startswith("FONT_MODAK_")
]

for size in (26, 32, 36, 42, 48, 54):
    media.append({
        "type": "font",
        "name": f"FONT_MODAK_{size}",
        "file": "fonts/Modak-Regular.ttf",
        "characterRegex": "[0-9]"
    })

package_path.write_text(
    json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8"
)

print("Modak 26, 32, 36, 42, 48 und 54 wurden eingetragen.")
