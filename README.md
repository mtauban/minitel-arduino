# Minitel-Arduino Unified Driver (Minitel 1 compatible)
**A modern Arduino/PlatformIO library for vintage Minitel terminals — including *true Minitel 1* with full PT/TP support and *no more Action key at startup*.**


---

## ✨ Overview

**Minitel-Arduino** is a two-layer Arduino library designed to drive **real Minitel terminals** (Minitel 1 / STUM M1) as interactive text and graphics displays.

It provides:

- A **low-level Minitel protocol driver** (`Minitel`)
- A **high-level pixel graphics engine** (`MinitelGfx`)
- A **software sprite engine** (animation, rotation, scaling, mirroring)
- A **unified keyboard & event system** (including SEP keys)
- Careful **performance optimizations** for 1200‑baud hardware

This library targets **real hardware**, not emulators.

---

## 🎯 Design Philosophy

- Faithful to **STUM M1** specifications
- Explicit control over **G0 / G1 character sets**
- No hidden framebuffer magic
- Optimized serial output (REP, cursor path minimization)
- Simple, explicit APIs — no “usine à gaz”

---

## 🧱 Architecture

```
Your Sketch
    │
    ▼
MinitelGfx   →  pixel graphics, sprites, diff flushing
    │
    ▼
Minitel     →  protocol, keyboard, screen, session
    │
    ▼
Serial / PT / TP
    │
    ▼
Real Minitel Terminal
```

---

## 🧩 Supported Hardware

- Minitel 1 / Minitel 1B
- Arduino Mega 2560 (recommended)
- Any Arduino with:
  - 1 hardware serial port
  - ~8 KB RAM available

---

## 📦 Installation

Clone the repository:

```bash
git clone https://github.com/yourname/MinitelGFX.git
```

Include the headers in your sketch:

```cpp
#include "Minitel.h"
#include "MinitelGfx.h"
```

---

## 🚀 Quick Start

### Minimal Setup

```cpp
#include <Arduino.h>
#include "Minitel.h"
#include "MinitelGfx.h"

constexpr uint8_t PIN_PT = 4;
constexpr uint8_t PIN_TP = 5;

Minitel minitel;
MinitelGfx gfx(minitel);

void setup() {
  Serial.begin(115200);                // Debug
  Serial1.begin(1200, SERIAL_7E1);     // Minitel

  minitel.begin(&Serial1, PIN_PT, PIN_TP, &Serial);
  minitel.startSession(2000);

  gfx.clear(true);
}

void loop() {}
```

---

## ⌨️ Keyboard & Event System

All incoming data is normalized into **events**.

### Reading Events

```cpp
Minitel::Event ev;

if (minitel.waitEvent(ev, 100)) {
  if (ev.type == Minitel::Event::CHAR) {
    minitel.print((char)ev.code);
  }
  else if (ev.type == Minitel::Event::SEP) {
    if (ev.code == Minitel::SEP_SEND) {
      minitel.println("ENVOI pressed");
    }
  }
}
```

### Reading a Line

```cpp
char buffer[32];

if (minitel.readLine(buffer, sizeof(buffer))) {
  minitel.println("You typed:");
  minitel.println(buffer);
}
```

---

## 🖥️ Screen & Text Control

### Clear & Cursor

```cpp
minitel.clearScreen();
minitel.home();
minitel.setCursor(5, 10);
minitel.print("Hello Minitel");
```

### Status Line (Row 00)

```cpp
minitel.printRow0("CONNECTED – DEMO MODE");
```

---

## 🎨 Colors & Text Attributes

```cpp
minitel.setCharColor(Minitel::Color::Green);
minitel.setBgColor(Minitel::Color::Black);

minitel.setFlash(true);
minitel.setDoubleSize(true);

minitel.print("Important text");
```

Supported:

- Foreground / background colors
- Flashing
- Double width / height / size
- Polarity (positive / negative)
- Global conceal / reveal

---

## 🟦 Graphics Model

MinitelGfx exposes a **virtual pixel grid** built on semi‑graphics:

| Space | Size |
|------|------|
| Characters | 40 × 24 |
| Pixels | **80 × 72** |
| Sub‑pixels | 2 × 3 per character |

---

## ✏️ Drawing Primitives

```cpp
gfx.setDrawColor(Minitel::Color::White);

gfx.drawPixel(10, 10);
gfx.drawLine(0, 0, 79, 71);
gfx.drawRect(10, 10, 20, 15, false);
gfx.drawCircle(40, 36, 10, false);

gfx.flush();
```

Available primitives:

- Pixel
- Line (thin / thick)
- Rectangle (filled / outline)
- Polyline
- Polygon
- Circle
- Triangle

---

## ⚡ Flush Modes

```cpp
gfx.flush(MinitelGfx::FlushMode::OptimizedDiff);
```

| Mode | Description |
|------|------------|
| `FullRedraw` | Redraws entire screen |
| `OptimizedDiff` | Updates only changed cells (default) |

---

## 👾 Sprite Engine

### Sprite Definition

```cpp
static const uint8_t pacmanFrames[] = {
  // frame data: width × height × frameCount
};

MinitelGfx::Sprite pacman;
```

### Initialization

```cpp
gfx.spriteInit(pacman, pacmanFrames, 6, 6, 2);
gfx.spriteSetPosition(pacman, 20, 30);
```

### Animation & Drawing

```cpp
gfx.spriteNextFrame(pacman);
gfx.spriteDraw(pacman);
gfx.flush();
```

---

## 🔄 Sprite Transformations

```cpp
gfx.spriteSetAngle(pacman, 45);     // degrees
gfx.spriteSetScale(pacman, 2);      // integer scaling
gfx.spriteSetFlip(pacman, true, false); // mirror X
```

Supported:

- Frame animation
- Rotation (degrees)
- Integer scaling (×1 to ×6)
- Horizontal / vertical mirroring
- Safe clipping (never writes outside screen)

---

## 🧠 Performance Notes

- Designed for **1200 baud**
- Uses REP compression automatically
- Smart cursor movement (relative vs absolute)
- No `delay()` calls in critical paths

---

## 🧪 Known Limitations

- Scrolling behavior is simplified
- Sprite transparency is binary (ON / OFF)
- Rotation uses floating point math

---

## 📜 License

MIT License — free to use, modify, and redistribute.

---

## 🙌 Credits

- STUM Minitel specifications
- Real Minitel 1 hardware testing
- Old‑school demo‑scene constraints

---

Happy hacking on real terminals ✨

