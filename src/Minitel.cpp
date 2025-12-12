#include "Minitel.h"
#include <string.h>

// ---- C0 / control codes ----------------------------------------------------
static const uint8_t C_NUL = 0x00;
static const uint8_t C_SOH = 0x01;
static const uint8_t C_STX = 0x02;
static const uint8_t C_ETX = 0x03;
static const uint8_t C_EOT = 0x04;
static const uint8_t C_ENQ = 0x05;

static const uint8_t C_BEL = 0x07;
static const uint8_t C_BS  = 0x08;
static const uint8_t C_HT  = 0x09; // Horizontal Tab
static const uint8_t C_LF  = 0x0A;
static const uint8_t C_VT  = 0x0B; // Vertical Tab
static const uint8_t C_FF  = 0x0C; // clear screen
static const uint8_t C_CR  = 0x0D;
static const uint8_t C_SO  = 0x0E; // shift-out  (G1)
static const uint8_t C_SI  = 0x0F; // shift-in   (G0)


static const uint8_t C_DLE = 0x10;
static const uint8_t C_Con = 0x11;
static const uint8_t C_REP = 0x12; // REP
static const uint8_t C_SEP = 0x13; // SEP
static const uint8_t C_Coff = 0x14; // REP
static const uint8_t C_NACK = 0x15; // REP
static const uint8_t C_SYN = 0x16; // REP

static const uint8_t C_CAN = 0x18; // CANCEL (Clear Line)
static const uint8_t C_SS2 = 0x19; // CANCEL (Clear Line)
static const uint8_t C_SUB = 0x1A; // CANCEL (Clear Line)
static const uint8_t C_ESC = 0x1B; // ESC
static const uint8_t C_RS  = 0x1E; // home
static const uint8_t C_US  = 0x1F; // cursor position
static const uint8_t C_DEL = 0x7F; // DELETE

// ---- STUM M1 SEP codes -----------------------------------------------------


static const uint8_t SEP_STATUS_CS     = 0x50; // 5/0
static const uint8_t SEP_STATUS_PT     = 0x54; // 5/4

// ---- STUM module transmission / reception codes ----------------------------
static const uint8_t MOD_SCREEN_TX   = 0x50; // 5/0
static const uint8_t MOD_KEYBOARD_TX = 0x51; // 5/1
static const uint8_t MOD_MODEM_TX    = 0x52; // 5/2
static const uint8_t MOD_SOCKET_TX   = 0x53; // 5/3

static const uint8_t MOD_SCREEN_RX   = 0x58; // 5/8
static const uint8_t MOD_KEYBOARD_RX = 0x59; // 5/9
static const uint8_t MOD_MODEM_RX    = 0x5A; // 5/10
static const uint8_t MOD_SOCKET_RX   = 0x5B; // 5/11

static const uint8_t PRO3_CTRL_ON    = 0x61; // 6/1
static const uint8_t PRO3_CTRL_OFF   = 0x60; // 6/0

// ----------------------------------------------------------------------------
// Constructor / Setup
// ----------------------------------------------------------------------------

Minitel::Minitel() {
    memset(eventBuf_, 0, sizeof(eventBuf_));
}

void Minitel::begin(Stream* stream, uint8_t ptPin, uint8_t tpPin, Stream* debug) {
    stream_ = stream;
    ptPin_  = ptPin;
    tpPin_  = tpPin;
    debug_  = debug;

    if (ptPin_ != 255) {
        pinMode(ptPin_, OUTPUT);
        digitalWrite(ptPin_, LOW);
    }
    if (tpPin_ != 255) {
        pinMode(tpPin_, INPUT);
    }
}

// ----------------------------------------------------------------------------
// Print base (Print compatibility)
// ----------------------------------------------------------------------------

size_t Minitel::write(uint8_t b) {
    // Print API uses this; treat as plain G0 text
    putChar((char)b);
    return 1;
}

size_t Minitel::write(const uint8_t* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        write(buffer[i]);
    }
    return size;
}

// ----------------------------------------------------------------------------
// PT / TP / session
// ----------------------------------------------------------------------------

void Minitel::setPT(bool active) {
    if (ptPin_ == 255) return;

    if (active) {
        // drive transistor → PT low
        pinMode(ptPin_, OUTPUT);
        digitalWrite(ptPin_, HIGH);
    } else {
        // hi-Z → Minitel pulls PT high internally
        pinMode(ptPin_, INPUT);
    }
}

bool Minitel::startSession(uint16_t timeoutMs) {
    setPT(true);
    sessionState_       = SessionState::Opening;
    lastSessionEventMs_ = millis();

    if (timeoutMs == 0) {
        // Non-blocking: let SEP 5/4 update the state later
        return true;
    }

    // Blocking wait for SEP 5/4
    beginTransactionWaitSep(5, 4, timeoutMs);
    unsigned long start = millis();
    while (tx_.active && (uint16_t)(millis() - start) <= timeoutMs) {
        poll();
    }

    if (!tx_.active && tx_.success) {
        sessionState_ = SessionState::Open;
        return true;
    }

    // Failed or timed out
    sessionState_ = SessionState::Closed;
    setPT(false);
    return false;
}

void Minitel::endSession() {
    setPT(false);
    sessionState_       = SessionState::Closed;
    lastSessionEventMs_ = millis();
}

bool Minitel::isTerminalOn() const {
    if (tpPin_ == 255) return true;
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
    if (!stream_) return;
    uint8_t v = b & 0x7F;
    if (debug_) {
        debug_->print(F("TX "));
        if (v < 0x10) debug_->print('0');
        debug_->print(v, HEX);
        debug_->print(' ');
    }
    stream_->write(v);
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

// ----------------------------------------------------------------------------
// Internal Parsing and State Machines
// ----------------------------------------------------------------------------

bool Minitel::handleLineEditingControl(uint8_t c) {
    switch (c) {
        case C_HT:  // Horizontal Tab (Cursor Right)
        case C_VT:  // Vertical Tab (Cursor Up)
        case C_RS:  // Home Cursor
        case C_US:  // Cursor Position prefix
        case C_CAN: // Cancel/Clear Line
        case C_DEL: // Delete (7F)
            if (debug_) {
                debug_->print(F("CONTROL 0x"));
                debug_->print(c, HEX);
                debug_->println(F(" consumed."));
            }
            return true;
        default:
            return false;
    }
}

void Minitel::parseByte(uint8_t c) {
    c &= 0x7F; // Strip parity bit (7-bit data)

    // 1. ESC sequence state machine takes priority
    if (escState_ != ESC_IDLE) {
        handleEscByte(c);
        return;
    }

    // 2. SEP sequence
    if (waitingSepSecond_) {
        handleSep(c);
        waitingSepSecond_ = false;
        return;
    }

    // 3. Complex navigation/editing controls (consumed)
    if (handleLineEditingControl(c)) {
        return;
    }

    // 4. Start ESC or SEP sequence
    if (c == C_ESC) {
        escState_ = ESC_GOT_ESC;
        return;
    }
    if (c == C_SEP) {
        waitingSepSecond_ = true;
        return;
    }

    // 5. Explicitly classified C0 Controls (CR, LF, BS must be Event::CHAR for readLine)
    if (c == C_CR || c == C_LF || c == C_BS) {
        Event ev;
        ev.type = Event::CHAR;
        ev.code = c;
        pushEvent(ev);
        return;
    }

    // 6. Other C0 Controls (0x00 to 0x1F, excluding exceptions above)
    if (c < 0x20) {
        Event ev;
        ev.type = Event::CONTROL;
        ev.code = c;
        pushEvent(ev);
        return;
    }

    // 7. Printable Characters (0x20..0x7E)
    if (c >= 0x20 && c <= 0x7E) {
        Event ev;
        ev.type = Event::CHAR;
        ev.code = c;
        pushEvent(ev);
        return;
    }
}

// ----------------------------------------------------------------------------
// Transaction helpers
// ----------------------------------------------------------------------------

void Minitel::beginTransactionWaitSep(uint8_t row, uint8_t col, uint16_t timeoutMs) {
    tx_.active    = true;
    tx_.sepRow    = row;
    tx_.sepCol    = col;
    tx_.timeoutMs = timeoutMs;
    tx_.startTime = millis();
    tx_.success   = false;
}

void Minitel::onSepForTransaction(uint8_t row, uint8_t col) {
    if (!tx_.active) return;
    if (tx_.sepRow != row || tx_.sepCol != col) return;

    tx_.active  = false;
    tx_.success = true;
}

void Minitel::checkTransactionTimeout() {
    if (!tx_.active) return;
    if (tx_.timeoutMs == 0) return;
    unsigned long now = millis();
    if ((uint16_t)(now - tx_.startTime) > tx_.timeoutMs) {
        if (debug_) debug_->println(F("TX Timeout"));
        tx_.active  = false;
        tx_.success = false;
    }
}

// ----------------------------------------------------------------------------
// Core I/O and Polling
// ----------------------------------------------------------------------------

void Minitel::poll() {
    if (!stream_) return;

    while (stream_->available()) {
        uint8_t c = stream_->read();
        parseByte(c);
    }

    checkTransactionTimeout();
}

bool Minitel::waitEvent(Event& ev, uint16_t timeoutMs) {
    unsigned long start = millis();

    while (true) {
        // 1. Process new bytes
        poll();

        // 2. Check for events
        if (eventAvailable() && readEvent(ev)) {
            return true;
        }

        // 3. Check for timeout
        if (timeoutMs > 0) {
            if ((uint16_t)(millis() - start) > timeoutMs) {
                ev.type = Event::TIMEOUT;
                return false;
            }
        }
        // No delay for maximum responsiveness at 1200 bauds
    }
}

// ----------------------------------------------------------------------------
// Keyboard helpers
// ----------------------------------------------------------------------------

uint8_t Minitel::readChar(uint16_t timeoutMs) {
    Event ev;
    while (waitEvent(ev, timeoutMs)) {
        if (ev.type == Event::CHAR) {
            return ev.code;
        }
    }
    return 0; // timeout
}

bool Minitel::readLine(char* buf, size_t bufSize, bool echo,
                       bool stopOnEnvoi, uint16_t timeoutMs)
{
    unsigned long start = millis();
    size_t idx = 0;

    if (bufSize == 0) return false;
    bufSize--; // reserve space for null terminator

    while (true) {
        // Global timeout
        if (timeoutMs > 0 && (uint16_t)(millis() - start) > timeoutMs) {
            buf[idx] = '\0';
            return false;
        }

        Event ev;
        if (!waitEvent(ev, 100)) continue;

        if (ev.type == Event::CHAR) {
            uint8_t c = ev.code;

            // Line ending
            if (c == C_CR || c == C_LF) {
                if (echo) print("\r\n");
                buf[idx] = '\0';
                return true;
            }

            // Backspace
            if (c == C_BS) {
                if (idx > 0) {
                    idx--;
                    if (echo) print("\b \b");
                }
                continue;
            }

            // Printable chars
            if (idx < bufSize && c >= 0x20 && c <= 0x7E) {
                buf[idx++] = c;
                if (echo) putChar(c);
                continue;
            }
        }
        else if (ev.type == Event::SEP) {
            if (stopOnEnvoi && ev.code == SEP_SEND) {
                if (echo) print("\r\n");
                buf[idx] = '\0';
                return true;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Screen / text helpers
// ----------------------------------------------------------------------------
void Minitel::printOptimized(const char* s, size_t len) {
    // TEMP: simple, safe version – no REP optimization
    for (size_t i = 0; i < len; ++i) {
        writeRaw((uint8_t)s[i]);
    }
}
// void Minitel::printOptimized(const char* s, size_t len) {
//     size_t i = 0;
//     static const size_t MAX_REP_COUNT  = 95;
//     static const size_t REP_THRESHOLD  = 4;

//     while (i < len) {
//         uint8_t current_char = (uint8_t)s[i];
//         size_t j = i;
//         while (j < len && (uint8_t)s[j] == current_char) {
//             j++;
//         }
//         size_t run_length = j - i;

//         if (run_length >= REP_THRESHOLD) {
//             size_t reps_to_send = run_length;
//             while (reps_to_send > 0) {
//                 size_t current_reps = (reps_to_send > MAX_REP_COUNT)
//                                       ? MAX_REP_COUNT : reps_to_send;

//                 uint8_t count_byte = 0x1F + current_reps;
//                 writeRaw(C_REP);
//                 writeRaw(count_byte);
//                 writeRaw(current_char);

//                 reps_to_send -= current_reps;
//             }
//             i = j;
//         } else {
//             for (size_t k = 0; k < run_length; k++) {
//                 writeRaw(current_char);
//             }
//             i = j;
//         }
//     }
// }

void Minitel::clearScreen() {
    writeRaw(C_FF);
    currentSet_ = CharSet::G0_ALPHA;
}

void Minitel::home() {
    writeRaw(C_RS);
    currentSet_ = CharSet::G0_ALPHA;
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

    // STUM: US restores attributes → we are back in G0
    currentSet_ = CharSet::G0_ALPHA;
}

void Minitel::setCursorRow0(uint8_t col) {
    // Row 00 access: US 4/0, X/Y (Y = column number, X = 4..7)
    // STUM M1 – "Access to row 00"

    if (col < 1)  col = 1;
    if (col > 40) col = 40;

    uint8_t rowCode = 0x40;                   // 4/0 => row 00
    uint8_t colCode = 0x40 | (col & 0x3F);    // X/Y, X in 4..7, Y = column

    writeRaw(C_US);
    writeRaw(rowCode);
    writeRaw(colCode);

    // US restores attributes: we're now in G0 on row 00
    currentSet_ = CharSet::G0_ALPHA;
}

void Minitel::putChar(char c) {
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI);
        currentSet_ = CharSet::G0_ALPHA;
    }
    writeRaw((uint8_t)c);
}

// High-level print helpers (use our REP optimization)
void Minitel::printRow0(const char* s) {
    if (!s) s = "";

    // Jump to row 00, column 1
    setCursorRow0(1);

    // Print at most 40 characters, pad with spaces
    uint8_t count = 0;
    while (*s && count < 40) {
        char c = *s++;
        if (c == '\r' || c == '\n') break;  // stop on explicit newline
        writeRaw(uint8_t(c) & 0x7F);        // G0 text
        ++count;
    }
    while (count < 40) {
        writeRaw(' ');
        ++count;
    }

    // Leave row 00 and restore previous position & attributes:
    // STUM: "The only way to leave row 0 is by sending a unit or
    // sub-unit separator or a LF."
    writeRaw(C_LF);
    // After this, the Minitel restores previous row/col + attributes.
    // currentSet_ stays as whatever Minitel chose; GFX will re-enter
    // G1 when needed via beginSemiGraphics().
}


void Minitel::print(const char* s) {
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI);
        currentSet_ = CharSet::G0_ALPHA;
    }
    printOptimized(s, strlen(s));
}

void Minitel::println(const char* s) {
    print(s);
    print("\r\n");
}

void Minitel::println() {
    putChar('\r');
    putChar('\n');
}

void Minitel::println(uint8_t v, int base) {
    print(v, base);
    println();
}

void Minitel::println(int v, int base) {
    print(v, base);
    println();
}

void Minitel::println(unsigned int v, int base) {
    print(v, base);
    println();
}

void Minitel::println(long v, int base) {
    print(v, base);
    println();
}

void Minitel::println(unsigned long v, int base) {
    print(v, base);
    println();
}


void Minitel::print(char c) {
    putChar(c);
}

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
    ltoa(v, buf, base);
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
    if (currentSet_ != CharSet::G1_GRAPHIC) {
        writeRaw(C_SO);
        currentSet_ = CharSet::G1_GRAPHIC;
    }
}

void Minitel::endSemiGraphics() {
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI);
        currentSet_ = CharSet::G0_ALPHA;
    }
}

void Minitel::putSemiGraphic(uint8_t code) {
    beginSemiGraphics();
    writeRaw(code & 0x7F);
}

void Minitel::printSemiGraphics(const char* s) {
    if (currentSet_ != CharSet::G1_GRAPHIC) {
        writeRaw(C_SO);
        currentSet_ = CharSet::G1_GRAPHIC;
    }
    printOptimized(s, strlen(s));
}

void Minitel::putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code) {
    setCursor(row, col);
    beginSemiGraphics();
    writeRaw(code & 0x7F);
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
    seq[1] = 0x3B;
    seq[2] = control;
    seq[3] = rx;
    seq[4] = tx;
    m.writeRaw(seq, 5);
}

void Minitel::configureKeyboardToSocketOnly(bool useTransaction,
                                            uint16_t timeoutMs)
{
    if (useTransaction) {
        beginTransactionWaitSep(5, 4, timeoutMs);
    }

    // keyboard -> modem OFF
    sendPRO3(*this, PRO3_CTRL_OFF, MOD_MODEM_RX, MOD_KEYBOARD_TX);
    // modem -> screen OFF
    sendPRO3(*this, PRO3_CTRL_OFF, MOD_SCREEN_RX, MOD_MODEM_TX);
    // keyboard -> socket ON
    sendPRO3(*this, PRO3_CTRL_ON,  MOD_SOCKET_RX, MOD_KEYBOARD_TX);
}



void Minitel::setCharColor(Color c) {
    writeRaw(0x1B);
    writeRaw(0x40 | (static_cast<uint8_t>(c) & 0x07)); // 4/0..4/7
}

void Minitel::setBgColor(Color c) {
    writeRaw(0x1B);
    writeRaw(0x50 | (static_cast<uint8_t>(c) & 0x07)); // 5/0..5/7
}

void Minitel::setFlash(bool enable) {
    writeRaw(0x1B);
    writeRaw(enable ? 0x48 : 0x49); // 4/8 flash, 4/9 steady
}

void Minitel::setLining(bool enable) {
    writeRaw(0x1B);
    writeRaw(enable ? 0x4A : 0x59); // 4/A start, 5/9 stop (field-level stop)
}

void Minitel::setMaskReveal(bool reveal) {
    writeRaw(0x1B);
    writeRaw(reveal ? 0x5F : 0x58); // 5/F reveal, 5/8 conceal
}


void Minitel::fillSpaces(uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        writeRaw(' ');
    }
}

void Minitel::putCharAt(uint8_t row, uint8_t col, char c) {
    setCursor(row, col);
    putChar(c);
}

bool Minitel::requestCursorPosition(uint8_t& outRow, uint8_t& outCol, uint16_t timeoutMs) {
    // Send ESC 6/1
    writeRaw(C_ESC);
    writeRaw(0x61);

    Event ev;
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
        poll();
        if (!eventAvailable()) continue;

        if (readEvent(ev)) {
            if (ev.type == Event::CONTROL && ev.code == C_US) {
                // The next 2 CHAR events will be row & col
                // Wait for row
                if (!waitEvent(ev, 50)) return false;
                if (ev.type != Event::CHAR) return false;
                outRow = ev.code & 0x7F;

                // Wait for col
                if (!waitEvent(ev, 50)) return false;
                if (ev.type != Event::CHAR) return false;
                outCol = ev.code & 0x7F;

                return true;
            }
        }
    }
    return false; // timeout
}