# Minitel-Arduino Unified Driver (Minitel 1 compatible)
**A modern Arduino/PlatformIO library for vintage Minitel terminals — including *true Minitel 1* with full PT/TP support and *no more Action key at startup*.**

---

## 🌟 Why this library exists

Most existing Arduino/Minitel projects were written for **late Minitel models** (1B, US variants, Thomson Series 2…), which behave differently and require the user to:

- Press **"Connexion/Fin"**, **"Action"**, or a menu key at startup  
- Use **8N1** instead of the official 7E1 format  
- Ignore **PT/TP signaling**  
- Assume modern keyboard codepages  
- Expect ASCII or raw keycodes instead of SEP sequences  

But **true Minitel 1** (especially STUM M1 refurb models) behaves very differently:

- Requires **1200 baud, SERIAL_7E1**  
- Requires correct **PT line activation** to open a session  
- Uses **SEP and ESC sequences** heavily  
- Shows *no usable behavior* until the host initializes it properly  

This library was created to fix that.  
It is the first modern, fully documented library designed **from the ground up for Minitel 1 compatibility**, while still supporting newer models.

---

# 🚀 Features

### ✅ Full Minitel 1 compatibility
Works on original Minitel 1 units **without pressing any key**, exactly like the host system in the 80s.

### ✅ PT/TP session management
Implements the official PT protocol:

- `PT` pulled LOW → terminal wakes  
- `TP` read as LOW → terminal is powered on  

This enables **automatic startup**, perfect for kiosks, installations, BBS gateways, etc.

### ✅ Unified event parser
Everything the terminal sends is normalized into a single `Event` structure:

- Printable characters  
- SEP keycodes  
- ESC sequences  
- C0/C1 controls  
- Timeouts  

This drastically simplifies application code.

### ✅ Async transaction engine
Wait for a specific SEP or ESC sequence with a callback and timeout.

```cpp
minitel.beginTransactionWaitSep(4, 13, 200, onEnvoi, onTimeout);
```

### ✅ High-level screen helpers

```cpp
minitel.clearScreen();
minitel.setCursor(1,1);
minitel.println("Hello Minitel!");
```

### ✅ Semi-graphics (G1) helpers

```cpp
minitel.beginSemiGraphics();
minitel.putSemiGraphic(0x5B);
```

### ✅ Keyboard helpers

```cpp
char buf[32];
if (minitel.readLine(buf, sizeof(buf), 5000)) {
    // user pressed ENVOI or ENTER
}
```

### ✅ PlatformIO-ready
With `library.json`, examples, and GitHub-friendly versioning.

---

# 🛠️ Wiring Guide (IMPORTANT)

Minitel hardware is robust but **not Arduino-level compatible**.  
Correct wiring is essential — especially for **Minitel 1**, which is electrically simpler but stricter.

## 📌 The DIN-5 Connector (Minitel rear port)

Pins (view from front of connector):

```
  5   4   3
    2   1
```

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| 1   | PT   | Output ↑  | Terminal wake (host pulls LOW to power ON) |
| 2   | TXT  | Input →   | Arduino receives data (Minitel → Arduino) |
| 3   | RXT  | Output ←  | Arduino sends data (Arduino → Minitel) |
| 4   | TP   | Input ↑   | Terminal power detect (LOW = ON) |
| 5   | GND  | —         | Common ground |

---

# ⚡ Required Electrical Components

### 1️⃣ Pull-up resistor on the Minitel RX line (PTT requirement)

The **Minitel expects an open-collector bus** on its receive line.  
If you connect Arduino TX directly, you MUST add:

- **4.7 kΩ pull-up to +5V** on the Minitel RX pin  
- plus a **transistor (NPN) or MOSFET level interface** to simulate open-collector behavior

This matches the original modem’s design and prevents the Minitel from misinterpreting the line at boot.

**Without this, the Minitel 1 often stays stuck in a half-boot state or ignores data.**

---

### 2️⃣ NPN transistor (or MOSFET) for PT (wake signal)

PT must be **actively pulled LOW** by the host:  

- Use an NPN transistor (2N3904) or a MOSFET (2N7000).  
- Emitter → GND  
- Collector → PT pin  
- Base → Arduino pin via **4.7k resistor**

This allows the Arduino to safely pull PT low without directly sinking the terminal’s internal bias.

---

### 3️⃣ Optional transistor for TX (recommended)

For safety and correctness:

- Arduino TX → **transistor open collector** → Minitel RX  
- Minitel RX has its own pull-up (or you add one)  
- Prevents putting 5V directly into a line expecting a current-limited open collector

This is the **proper historical interface**, proven stable on all units.

---

### 4️⃣ Reading TP (terminal power detect)

TP is an open-collector output from the Minitel:

- Add a **10 kΩ pull-up** to 5V  
- Read normally with Arduino digital input

When TP = LOW → terminal is powered ON (after wake).

---

## 🗺️ Recommended Minimal Interface Schematic

```text
Arduino TX --- NPN transistor ---> Minitel RX (pin 3)
                   |
                 4.7k
                   |
                  GND

Arduino PT Pin -- 4.7k --> Base of NPN --> Minitel PT (pin 1)

TP (pin 4) -- 10k pull-up --> +5V --> Arduino digital input

GND <----------------------------------- Minitel GND (pin 5)
```

This wiring works with:

- Minitel 1 (F.T. 1983, 1984, 1985)
- Minitel 1 STUM refurb models
- Minitel 1B / 1B US
- Alcatel & Radiotechnique derivative models

---

# 🧪 Minimal Example

```cpp
#include <Minitel.h>

Minitel mt;

void setup() {
    Serial1.begin(1200, SERIAL_7E1);
    mt.begin(Serial1, 4, 5);    // PT=4, TP=5

    mt.startSession();          // wakes terminal — no Action key needed
    mt.clearScreen();
    mt.println("Hello Minitel!");
}

void loop() {
    mt.poll();
}
```

---

# 📦 Install via PlatformIO

```ini
lib_deps =
    https://github.com/<your-user>/minitel-arduino.git
```

Pin to a stable release (recommended):

```ini
lib_deps =
    https://github.com/<your-user>/minitel-arduino.git#v0.1.0
```

---

# 🧭 Roadmap

- Example: Snake (semi-graphics mode)  
- BBS-style text browser  
- Automatic terminal detection (Minitel 1 / 1B / 2)  
- Videotex character rendering utilities  
- Minitel-to-USB bridge for PC emulators  

---

# ❤️ Contributions Welcome

Bug reports, hardware testing, and improvements are encouraged.  
The goal is to provide a **robust, long-term, well-engineered Minitel driver** for the entire retrocomputing community.

---

# 📚 Credits

This library was built from extensive testing on multiple **real Minitel 1 hardware units**, reverse-engineering PT/TP behavior, ESC/SEP parsing, and historical documentation (STUM M1).

It brings modern tooling (PlatformIO, Arduino classes, async engine) to a vintage French icon.
