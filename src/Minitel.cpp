#include "Minitel.h"

// ---- C0 / control codes ----------------------------------------------------
static const uint8_t C_NUL = 0x00;
static const uint8_t C_BS  = 0x08;
static const uint8_t C_HT  = 0x09;
static const uint8_t C_LF  = 0x0A;
static const uint8_t C_FF  = 0x0C; // clear screen
static const uint8_t C_CR  = 0x0D;
static const uint8_t C_SO  = 0x0E; // shift-out  (G1)
static const uint8_t C_SI  = 0x0F; // shift-in   (G0)
static const uint8_t C_SEP = 0x13; // SEP
static const uint8_t C_REP = 0x12; // REP
static const uint8_t C_RS  = 0x1E; // home
static const uint8_t C_US  = 0x1F; // cursor position
static const uint8_t C_ESC = 0x1B; // ESC

// ---- STUM module transmission / reception codes ----------------------------
static const uint8_t MOD_SCREEN_TX   = 0x50; // 5/0
static const uint8_t MOD_KEYBOARD_TX = 0x51; // 5/1
static const uint8_t MOD_MODEM_TX    = 0x52; // 5/2
static const uint8_t MOD_SOCKET_TX   = 0x53; // 5/3

static const uint8_t MOD_SCREEN_RX   = 0x58; // 5/8
static const uint8_t MOD_KEYBOARD_RX = 0x59; // 5/9
static const uint8_t MOD_MODEM_RX    = 0x5A; // 5/A
static const uint8_t MOD_SOCKET_RX   = 0x5B; // 5/B

// PRO3 control codes
static const uint8_t PRO3_CTRL_OFF   = 0x60; // 6/0
static const uint8_t PRO3_CTRL_ON    = 0x61; // 6/1

// ----------------------------------------------------------------------------

Minitel::Minitel() {
    tx_.active     = false;
    tx_.waitSep    = false;
    tx_.timeoutMs  = 0;
    tx_.startMs    = 0;
    tx_.onSuccess  = nullptr;
    tx_.onTimeout  = nullptr;
    tx_.userData   = nullptr;
}

// ----------------------------------------------------------------------------

void Minitel::begin(Stream& s, int ptPin, int tpPin) {
    io_    = &s;
    ptPin_ = ptPin;
    tpPin_ = tpPin;

    if (ptPin_ >= 0) {
        pinMode(ptPin_, INPUT); // hi-Z
    }
    if (tpPin_ >= 0) {
        pinMode(tpPin_, INPUT);
    }

    sessionState_ = SessionState::Closed;
    lastSessionEventMs_ = millis();
    eventHead_ = eventTail_ = 0;

    waitingSepSecond_ = false;
    escState_ = ESC_IDLE;
    escTmpLen_ = 0;

    tx_.active = false;
}

// ----------------------------------------------------------------------------
// PT / TP / session
// ----------------------------------------------------------------------------

void Minitel::setPT(bool active) {
    if (ptPin_ < 0) return;

    if (active) {
        // drive transistor → PT low
        pinMode(ptPin_, OUTPUT);
        digitalWrite(ptPin_, HIGH);
    } else {
        // hi-Z → Minitel pulls PT high internally
        pinMode(ptPin_, INPUT);
    }
}

void Minitel::startSession() {
    setPT(true);
    sessionState_       = SessionState::Opening;
    lastSessionEventMs_ = millis();
}

void Minitel::endSession() {
    setPT(false);
    sessionState_       = SessionState::Closing;
    lastSessionEventMs_ = millis();
}

bool Minitel::isTerminalOn() const {
    if (tpPin_ < 0) return true;
    return (digitalRead(tpPin_) == LOW); // TP low => ON in STUM
}

// ----------------------------------------------------------------------------
// Unified event FIFO
// ----------------------------------------------------------------------------

bool Minitel::eventAvailable() const {
    return eventHead_ != eventTail_;
}

bool Minitel::readEvent(Event& ev) {
    if (eventHead_ == eventTail_) return false;
    ev = eventBuf_[eventTail_];
    eventTail_ = (uint8_t)((eventTail_ + 1) % EVENTBUF_SIZE);
    return true;
}

void Minitel::pushEvent(const Event& ev) {
    uint8_t next = (uint8_t)((eventHead_ + 1) % EVENTBUF_SIZE);
    if (next == eventTail_) {
        // overflow: drop oldest
        eventTail_ = (uint8_t)((eventTail_ + 1) % EVENTBUF_SIZE);
    }
    eventBuf_[eventHead_] = ev;
    eventHead_ = next;

    if (debug_) {
        debug_->print(F("EV "));
        debug_->print((int)ev.type);
        debug_->print(F(" code=0x"));
        if (ev.code < 0x10) debug_->print('0');
        debug_->print(ev.code, HEX);
        debug_->print(F(" row="));
        debug_->print(ev.row);
        debug_->print(F(" col="));
        debug_->println(ev.col);
    }
}

// ----------------------------------------------------------------------------
// TX helpers
// ----------------------------------------------------------------------------

void Minitel::writeRaw(uint8_t b) {
    if (!io_) return;
    uint8_t v = b & 0x7F;
    if (debug_) {
        debug_->print(F("TX "));
        if (v < 0x10) debug_->print('0');
        debug_->print(v, HEX);
        debug_->print(' ');
    }
    io_->write(v);
}

void Minitel::writeRaw(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        writeRaw(data[i]);
    }
}

// ----------------------------------------------------------------------------
// ESC / SEP parsing
// ----------------------------------------------------------------------------

void Minitel::handleSep(uint8_t secondByte) {
    uint8_t row = (secondByte >> 4) & 0x07;
    uint8_t col = (secondByte & 0x0F);

    // Transaction hook: SEP-based ack
    onSepForTransaction(row, col);

    // Session management: SEP 5/4
    if (row == 5 && col == 4) {
        if (sessionState_ == SessionState::Opening) {
            sessionState_ = SessionState::Open;
        } else if (sessionState_ == SessionState::Closing) {
            sessionState_ = SessionState::Closed;
        }
        lastSessionEventMs_ = millis();
    }

    Event ev;
    ev.type   = Event::SEP;
    ev.code   = secondByte & 0x7F;
    ev.row    = row;
    ev.col    = col;
    ev.escLen = 0;
    for (uint8_t i = 0; i < sizeof(ev.escData); ++i) ev.escData[i] = 0;

    pushEvent(ev);
}

void Minitel::handleEscByte(uint8_t c) {
    switch (escState_) {
    case ESC_IDLE:
        // should never be called in this state
        break;

    case ESC_GOT_ESC:
        if (c == 0x3B) {
            // ESC 3B a b c → PRO3-like sequence (3 bytes)
            escState_  = ESC_3B;
            escTmpLen_ = 0;
        } else if (c >= 0x40 && c <= 0x7F) {
            // Single-byte C1 after ESC
            Event ev;
            ev.type   = Event::ESCSEQ;
            ev.code   = c;
            ev.row    = 0;
            ev.col    = 0;
            ev.escLen = 0;
            for (uint8_t i = 0; i < sizeof(ev.escData); ++i) ev.escData[i] = 0;
            pushEvent(ev);
            escState_ = ESC_IDLE;
        } else {
            // Unknown / unsupported => drop
            escState_ = ESC_IDLE;
        }
        break;

    case ESC_3B:
        escTmp_[escTmpLen_++] = c;
        if (escTmpLen_ >= 3) {
            Event ev;
            ev.type   = Event::ESCSEQ;
            ev.code   = 0x3B; // ESC 3B ...
            ev.row    = 0;
            ev.col    = 0;
            ev.escLen = 3;
            for (uint8_t i = 0; i < 3; ++i) {
                ev.escData[i] = escTmp_[i] & 0x7F;
            }
            for (uint8_t i = 3; i < sizeof(ev.escData); ++i) {
                ev.escData[i] = 0;
            }
            pushEvent(ev);
            escState_  = ESC_IDLE;
            escTmpLen_ = 0;
        }
        break;
    }
}

void Minitel::parseByte(uint8_t c) {
    c &= 0x7F;

    if (debug_) {
        debug_->print(F("RX "));
        if (c < 0x10) debug_->print('0');
        debug_->print(c, HEX);
        debug_->print(' ');
    }

    // ESC state machine has priority
    if (escState_ != ESC_IDLE) {
        handleEscByte(c);
        return;
    }

    // Waiting for SEP's second byte
    if (waitingSepSecond_) {
        waitingSepSecond_ = false;
        handleSep(c);
        return;
    }

    // Start ESC sequence
    if (c == C_ESC) {
        escState_  = ESC_GOT_ESC;
        escTmpLen_ = 0;
        return;
    }

    // Start SEP sequence
    if (c == C_SEP) {
        waitingSepSecond_ = true;
        return;
    }

    // Printable ASCII
    if (c >= 0x20 && c <= 0x7E) {
        Event ev;
        ev.type   = Event::CHAR;
        ev.code   = c;
        ev.row    = 0;
        ev.col    = 0;
        ev.escLen = 0;
        for (uint8_t i = 0; i < sizeof(ev.escData); ++i) ev.escData[i] = 0;
        pushEvent(ev);
        return;
    }

    // CR / LF / BS as CHAR events
    if (c == C_CR || c == C_LF || c == C_BS) {
        Event ev;
        ev.type   = Event::CHAR;
        ev.code   = c;
        ev.row    = 0;
        ev.col    = 0;
        ev.escLen = 0;
        for (uint8_t i = 0; i < sizeof(ev.escData); ++i) ev.escData[i] = 0;
        pushEvent(ev);
        return;
    }

    // Other C0 controls as CONTROL events
    if (c < 0x20) {
        Event ev;
        ev.type   = Event::CONTROL;
        ev.code   = c;
        ev.row    = 0;
        ev.col    = 0;
        ev.escLen = 0;
        for (uint8_t i = 0; i < sizeof(ev.escData); ++i) ev.escData[i] = 0;
        pushEvent(ev);
        return;
    }

    // Everything else ignored
}

// ----------------------------------------------------------------------------
// Transaction helpers
// ----------------------------------------------------------------------------

bool Minitel::beginTransactionWaitSep(uint8_t row,
                                      uint8_t col,
                                      uint16_t timeoutMs,
                                      void (*onSuccess)(void*),
                                      void (*onTimeout)(void*),
                                      void* userData)
{
    if (tx_.active) {
        return false;
    }

    tx_.active    = true;
    tx_.waitSep   = true;
    tx_.sepRow    = row;
    tx_.sepCol    = col;
    tx_.timeoutMs = timeoutMs;
    tx_.startMs   = millis();
    tx_.onSuccess = onSuccess;
    tx_.onTimeout = onTimeout;
    tx_.userData  = userData;

    return true;
}

void Minitel::cancelTransaction() {
    tx_.active = false;
}

void Minitel::onSepForTransaction(uint8_t row, uint8_t col) {
    if (!tx_.active) return;
    if (!tx_.waitSep) return;
    if (tx_.sepRow != row || tx_.sepCol != col) return;

    // success
    tx_.active = false;
    if (tx_.onSuccess) {
        tx_.onSuccess(tx_.userData);
    }
}

void Minitel::checkTransactionTimeout() {
    if (!tx_.active) return;
    if (tx_.timeoutMs == 0) return; // no timeout
    unsigned long now = millis();
    if ((uint16_t)(now - tx_.startMs) > tx_.timeoutMs) {
        tx_.active = false;
        if (tx_.onTimeout) {
            tx_.onTimeout(tx_.userData);
        }
    }
}

// ----------------------------------------------------------------------------
// poll()
// ----------------------------------------------------------------------------

void Minitel::poll() {
    if (!io_) return;

    while (io_->available() > 0) {
        int r = io_->read();
        if (r < 0) break;
        parseByte((uint8_t)r);
    }

    checkTransactionTimeout();
}

// ----------------------------------------------------------------------------
// Blocking event helpers (used by readChar/readLine)
// ----------------------------------------------------------------------------

bool Minitel::waitEvent(Event& ev, uint16_t timeoutMs) {
    unsigned long start = millis();

    while (true) {
        if (eventAvailable()) {
            if (readEvent(ev)) return true;
        }

        if (timeoutMs > 0) {
            if ((uint16_t)(millis() - start) > timeoutMs) {
                return false;
            }
        }

        poll();
        delay(1); // be gentle
    }
}

// ----------------------------------------------------------------------------
// Keyboard helpers
// ----------------------------------------------------------------------------

bool Minitel::readChar(char& c, uint16_t timeoutMs) {
    Event ev;
    while (waitEvent(ev, timeoutMs)) {
        if (ev.type == Event::CHAR) {
            c = (char)ev.code;
            return true;
        }
        // All other events (SEP, ESCSEQ, CONTROL) are ignored here
    }
    return false;
}

bool Minitel::readLine(char* buf,
                       size_t maxLen,
                       uint16_t timeoutMs,
                       bool stopOnEnvoi,
                       bool echoLocally)
{
    if (!buf || maxLen == 0) return false;

    size_t idx = 0;
    buf[0] = '\0';

    unsigned long start = millis();
    Event ev;

    while (true) {
        while (eventAvailable()) {
            if (!readEvent(ev)) break;

            if (ev.type == Event::CHAR) {
                uint8_t c = ev.code;

                // CR or LF ends line
                if (c == C_CR || c == C_LF) {
                    buf[idx] = '\0';
                    return true;
                }

                // Backspace
                if (c == C_BS) {
                    if (idx > 0) {
                        idx--;
                        buf[idx] = '\0';
                        if (echoLocally) {
                            putChar('\b');
                            putChar(' ');
                            putChar('\b');
                        }
                    }
                    continue;
                }

                // Printable
                if (c >= 0x20 && c <= 0x7E) {
                    if (idx < maxLen - 1) {
                        buf[idx++] = (char)c;
                        buf[idx]   = '\0';
                        if (echoLocally) {
                            putChar((char)c);
                        }
                    }
                    continue;
                }

                // other controls ignored
            }
            else if (ev.type == Event::SEP) {
                if (stopOnEnvoi && ev.row == 4 && ev.col == 13) {
                    // ENVOI
                    buf[idx] = '\0';
                    return true;
                }
                // other SEP ignored
            }
            // ESCSEQ, CONTROL ignored in readLine
        }

        if (timeoutMs > 0) {
            if ((uint16_t)(millis() - start) > timeoutMs) {
                buf[idx] = '\0';
                return false;
            }
        }

        poll();
        delay(1);
    }
}

// ----------------------------------------------------------------------------
// Screen / text helpers
// ----------------------------------------------------------------------------

void Minitel::clearScreen() {
    writeRaw(C_FF);
}

void Minitel::home() {
    writeRaw(C_RS);
}

void Minitel::setCursor(uint8_t row, uint8_t col) {
    if (row < 1) row = 1;
    if (row > 24) row = 24;
    if (col < 1) col = 1;
    if (col > 40) col = 40;

    uint8_t rowCode = 0x40 | (row & 0x1F);
    uint8_t colCode = 0x40 | (col & 0x3F);

    writeRaw(C_US);
    writeRaw(rowCode);
    writeRaw(colCode);
}

void Minitel::putChar(char c) {
    writeRaw((uint8_t)c);
}

// ----- generic printing helpers ---------------------------------------------

void Minitel::print(const char* s) {
    if (!s) return;
    while (*s) {
        putChar(*s++);
    }
}

void Minitel::println(const char* s) {
    print(s);
    putChar('\r');
    putChar('\n');
}

void Minitel::println() {
    putChar('\r');
    putChar('\n');
}

// Single char
void Minitel::print(char c) {
    putChar(c);
}

// Numeric helpers using standard libc converters
void Minitel::print(uint8_t v, int base) {
    print((unsigned long)v, base);
}

void Minitel::print(int v, int base) {
    print((long)v, base);
}

void Minitel::print(unsigned int v, int base) {
    print((unsigned long)v, base);
}

void Minitel::print(long v, int base) {
    char buf[16];
    ltoa(v, buf, base);   // Arduino’s libc supports this
    print(buf);
}

void Minitel::print(unsigned long v, int base) {
    char buf[16];
    ultoa(v, buf, base);
    print(buf);
}

// ----------------------------------------------------------------------------
// Semi-graphics
// ----------------------------------------------------------------------------

void Minitel::beginSemiGraphics() {
    writeRaw(C_SO);
}

void Minitel::endSemiGraphics() {
    writeRaw(C_SI);
}

void Minitel::putSemiGraphic(uint8_t code) {
    uint8_t c = code & 0x7F;
    if (c < 0x20) c = 0x20;
    if (c > 0x7E) c = 0x7E;
    writeRaw(c);
}

void Minitel::putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code) {
    setCursor(row, col);
    beginSemiGraphics();
    putSemiGraphic(code);
    endSemiGraphics();
}

// ----------------------------------------------------------------------------
// PRO3: keyboard/screen switching
// ----------------------------------------------------------------------------

void Minitel::enablePRO3() {
    uint8_t seq[] = { C_ESC, 0x3B, PRO3_CTRL_ON, 0x5F, 0x5F };
    writeRaw(seq, sizeof(seq));
}

static void sendPRO3(Minitel& m, uint8_t control, uint8_t rx, uint8_t tx) {
    uint8_t seq[5];
    seq[0] = C_ESC;
    seq[1] = 0x3B;      // '3B'
    seq[2] = control;   // 6/0 or 6/1
    seq[3] = rx;        // receiver module
    seq[4] = tx;        // transmitter module
    m.writeRaw(seq, 5);
}

void Minitel::configureKeyboardToSocketOnly(bool useTransaction,
                                            uint16_t timeoutMs)
{
    // Optionally start a transaction to wait for a PT SEP 5/4 or similar,
    // but since PRO3 may not always ack with a specific SEP, we keep it
    // simple: PRO3 is "fire and forget" by default.
    if (useTransaction) {
        // Example: wait for SEP 5/4 as a generic "status changed"
        beginTransactionWaitSep(5, 4, timeoutMs);
    }

    // keyboard -> modem OFF
    sendPRO3(*this, PRO3_CTRL_OFF, MOD_MODEM_RX, MOD_KEYBOARD_TX);
    // modem -> screen OFF
    sendPRO3(*this, PRO3_CTRL_OFF, MOD_SCREEN_RX, MOD_MODEM_TX);
    // keyboard -> socket ON
    sendPRO3(*this, PRO3_CTRL_ON,  MOD_SOCKET_RX, MOD_KEYBOARD_TX);
}

