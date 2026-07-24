#!/usr/bin/env python3
import json
from pathlib import Path

package_path = Path("package.json")

if not package_path.exists():
    raise SystemExit("package.json wurde im aktuellen Ordner nicht gefunden.")

font_path = Path("resources/fonts/Modak-Regular.ttf")
if not font_path.exists():
    raise SystemExit(
        "Font nicht gefunden: resources/fonts/Modak-Regular.ttf"
    )

data = json.loads(package_path.read_text(encoding="utf-8"))

pebble = data.setdefault("pebble", {})
resources = pebble.setdefault("resources", {})
media = resources.setdefault("media", [])

font_resource = {
    "type": "font",
    "name": "FONT_MODAK_32",
    "file": "fonts/Modak-Regular.ttf",
    "characterRegex": "[0-9]"
}

for item in media:
    if item.get("name") == "FONT_MODAK_32":
        item.update(font_resource)
        break
else:
    media.append(font_resource)

package_path.write_text(
    json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8"
)

print("FONT_MODAK_32 wurde in package.json eingetragen.")
