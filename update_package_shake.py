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

# Replace the previous value-display options with the single-shake option.
message_keys[:] = [
    key for key in message_keys
    if key not in ("ShowValues", "DoubleTapValues")
]

for key in (
    "BackgroundColor",
    "BlobColor",
    "ValueColor",
    "ShakeValues",
):
    if key not in message_keys:
        message_keys.append(key)

path.write_text(
    json.dumps(data, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8"
)

print("ShakeValues wurde ergänzt; alte Werte-Schalter wurden entfernt.")
