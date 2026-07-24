# Lavalamp

A modern fork of the classic **Binary Blob** watchface for Pebble.

Lavalamp is a binary clock that displays the current time as animated blobs. Every minute the blobs flow across the screen like a lava lamp before settling into their new positions. The result is a minimalist watchface that combines binary time with smooth organic animations.

This project is written entirely in **C** using the Pebble SDK and is fully open source.

---

## Features

- 🫧 Animated lava lamp blobs every minute
- 🕒 Binary clock with hour and minute values
- 🔢 Optional value overlay (1, 2, 4, 8, 16, 32)
- 📳 Shake the watch to briefly reveal the values
- 🎨 Configurable colors
  - Background color
  - Blob color
  - Value color
- 💾 Settings are stored permanently
- ⌚ Supports Pebble Time 2 and other color Pebble watches

---

## How to read the clock

The watchface is a binary clock.

The upper four blobs represent the **hours**:

```
8 4 2 1
```

The lower six blobs represent the **minutes**:

```
32 16 8 4 2 1
```

Simply add the visible values together.

Example:

Hours

```
8 + 2 + 1 = 11
```

Minutes

```
32 + 16 + 4 = 52
```

Current time:

```
11:52
```

If reading binary is still unfamiliar, enable **"Shake to see values"** in the settings. A short wrist movement will briefly display the values inside the blobs.

---

## Configuration

The following options are available:

- Background color
- Blob color
- Value color
- Enable or disable **Shake to see values**

---

## Credits

Lavalamp is based on the original **Binary Blob** watchface by **jmlait** (2014).

The original author kindly granted permission to fork and improve the project under a BSD-style license.

Original project:
https://github.com/jmlaitpebble/binaryblob

Many thanks to **jmlait** for creating one of the most unique Pebble watchfaces.

---

## What's new in Lavalamp?

Compared to the original project, Lavalamp adds:

- Modern Pebble SDK compatibility
- Pebble Time 2 support
- Configurable colors
- Optional value display
- Custom Modak font
- Smooth value fade animation
- Shake gesture to reveal values
- General code cleanup and modernization

---

## Building

```bash
npm install
pebble build
```

Install on a connected watch:

```bash
pebble install build/Lavalamp.pbw --phone <WATCH_IP>
```

---

## License

BSD-style license.

See the included LICENSE file for details.
