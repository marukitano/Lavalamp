#!/usr/bin/env python3
import json
from pathlib import Path

path = Path("package.json")

if not path.exists():
    raise SystemExit("package.json wurde im aktuellen Ordner nicht gefunden.")

data = json.loads(path.read_text(encoding="utf-8"))
pebble = data.setdefault("pebble", {})

capabilities = pebble.setdefault("capabilities", [])
if "configurable" not in capabilities:
    capabilities.append("configurable")

message_keys = pebble.setdefault("messageKeys", [])
for key in ("BackgroundColor", "BlobColor", "ValueColor"):
    if key not in message_keys:
        message_keys.append(key)

path.write_text(
    json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8"
)

print("package.json wurde für Hintergrund-, Blob- und Zahlenfarbe vorbereitet.")
