#!/usr/bin/env python3
import json
from pathlib import Path

path = Path("package.json")

if not path.exists():
    raise SystemExit("package.json wurde im aktuellen Ordner nicht gefunden.")

data = json.loads(path.read_text(encoding="utf-8"))
pebble = data.setdefault("pebble", {})

pebble["enableMultiJS"] = True

capabilities = pebble.setdefault("capabilities", [])
if "configurable" not in capabilities:
    capabilities.append("configurable")

message_keys = pebble.setdefault("messageKeys", [])

# The old permanent-values setting is replaced by the double-tap setting.
message_keys[:] = [
    key for key in message_keys
    if key != "ShowValues"
]

for key in (
    "BackgroundColor",
    "BlobColor",
    "ValueColor",
    "DoubleTapValues",
):
    if key not in message_keys:
        message_keys.append(key)

path.write_text(
    json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8"
)

print("DoubleTapValues wurde ergänzt; ShowValues wurde entfernt.")
