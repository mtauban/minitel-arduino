#include "Minitel.h"

// ---- C0 / control codes (Readability improvements) --------------------------
static const uint8_t C_NUL = 0x00;
static const uint8_t C_BS  = 0x08;
static const uint8_t C_HT  = 0x09; // Horizontal Tab
static const uint8_t C_LF  = 0x0A;
static const uint8_t C_VT  = 0x0B; // Vertical Tab
static const uint8_t C_FF  = 0x0C; // clear screen
static const uint8_t C_CR  = 0x0D;
static const uint8_t C_SO  = 0x0E; // shift-out  (G1)
static const uint8_t C_SI  = 0x0F; // shift-in   (G0)
static const uint8_t C_SEP = 0x13; // SEP
static const uint8_t C_REP = 0x12; // REP
static const uint8_t C_ESC = 0x1B; // ESC
static const uint8_t C_CAN = 0x18; // CANCEL (Clear Line)
static const uint8_t C_RS  = 0x1E; // home
static const uint8_t C_US  = 0x1F; // cursor position
static const uint8_t C_DEL = 0x7F; // DELETE

// ---- STUM M1 SEP codes (Second Byte - Readability improvements) -------------
static const uint8_t SEP_SEND_KEY      = 0x41; // 4/1
static const uint8_t SEP_PREVIOUS_KEY  = 0x42; // 4/2
static const uint8_t SEP_REPEAT_KEY    = 0x43; // 4/3
static const uint8_t SEP_GUIDE_KEY     = 0x44; // 4/4
static const uint8_t SEP_CANCEL_KEY    = 0x45; // 4/5
static const uint8_t SEP_INDEX_KEY     = 0x46; // 4/6
static const uint8_t SEP_ERASE_KEY     = 0x47; // 4/7
static const uint8_t SEP_NEXT_KEY      = 0x48; // 4/8
static const uint8_t SEP_CONNECT_KEY   = 0x49; // 4/9
static const uint8_t SEP_ECP_ACT_REQ   = 0x4A; // 4/10
static const uint8_t SEP_ECP_INH_REQ   = 0x4B; // 4/11
static const uint8_t SEP_MOD_INV_REQ   = 0x4C; // 4/12
static const uint8_t SEP_ENVOI_KEY     = 0x4D; // 4/13 (Modem return to normal req, used as Enter/Send)

static const uint8_t SEP_STATUS_CS     = 0x50; // 5/0 (Connection Status)
static const uint8_t SEP_STATUS_PT     = 0x54; // 5/4 (PT status change)

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

// ----------------------------------------------------------------------------
// Constructor / Setup
// ----------------------------------------------------------------------------

Minitel::Minitel() {
    memset(eventBuf_, 0, sizeof(eventBuf_));
}

void Minitel::begin(Stream* stream, uint8_t ptPin, uint8_t tpPin, Stream* debug) {
    stream_ = stream;
    ptPin_ = ptPin;
    tpPin_ = tpPin;
    debug_ = debug;

    if (ptPin_ != 255) {
        pinMode(ptPin_, OUTPUT);
        digitalWrite(ptPin_, LOW);
    }
    if (tpPin_ != 255) {
        pinMode(tpPin_, INPUT);
    }
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
// ----------------------------------------------------------------------------
// Internal Parsing and State Machines (handleLineEditingControl and parseByte modified)
// ----------------------------------------------------------------------------

bool Minitel::handleLineEditingControl(uint8_t c) {
    // These controls are often used for local line editing (cursor movement, delete)
    // and would complicate the simple Event FIFO. We consume them here.
    switch (c) {
        case C_HT:  // Horizontal Tab (Cursor Right)
        case C_VT:  // Vertical Tab (Cursor Up)
        case C_RS:  // Home Cursor (Cursor to 1/1)
        case C_US:  // Cursor Position (Prefix)
        case C_CAN: // Cancel/Clear Line
        case C_DEL: // Delete (7F)
            // A full implementation would update an internal buffer/echo the action,
            // but for simplicity, we just consume the control code.
            if (debug_) {
                debug_->print(F("CONTROL 0x"));
                debug_->print(c, HEX);
                debug_->println(F(" consumed."));
            }
            return true; // Consume the byte
        default:
            return false; // Not a complex editing control
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

    // 3. Complex navigation/editing controls (Consumed)
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

    // 6. Other C0 Controls (0x00 to 0x1F, excluding the exceptions above)
    if (c < 0x20) {
        // This captures NUL, BEL, FF, RS, US (which is consumed by handleLineEditingControl)
        Event ev;
        ev.type = Event::CONTROL;
        ev.code = c;
        pushEvent(ev);
        return;
    }
    
    // 7. Printable Characters (G0/G1 Sets: 0x20 Space to 0x7E Tilde)
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
// Core I/O and Polling (waitEvent modified)
// ----------------------------------------------------------------------------

void Minitel::poll() {
    if (!stream_) return;

    while (stream_->available()) {
        uint8_t c = stream_->read();
        parseByte(c);
    }

    // Check transaction timeout
    if (tx_.active && tx_.timeoutMs > 0) {
        if ((uint16_t)(millis() - tx_.startTime) > tx_.timeoutMs) {
            if (debug_) debug_->println(F("TX Timeout"));
            tx_.active = false;
            tx_.success = false;
        }
    }
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
            // Note: Use (uint16_t) subtraction to handle overflow
            if ((uint16_t)(millis() - start) > timeoutMs) {
                ev.type = Event::TIMEOUT;
                return false;
            }
        }
        // NOTE: No delay(1) here for maximum efficiency at 1200 bauds.
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

bool Minitel::readLine(char* buf, size_t bufSize, bool echo,
                       bool stopOnEnvoi, uint16_t timeoutMs)
{
    unsigned long start = millis();
    size_t idx = 0;

    if (bufSize == 0) return false;
    bufSize--; // Reserve space for null terminator

    while (true) {
        // Handle timeout for the entire line
        if (timeoutMs > 0 && (uint16_t)(millis() - start) > timeoutMs) {
            buf[idx] = '\0';
            return false;
        }

        Event ev;
        // Use a small, repeated internal timeout to be responsive
        if (!waitEvent(ev, 100)) continue; 

        if (ev.type == Event::CHAR) {
            uint8_t c = ev.code;

            // Line Ending
            if (c == C_CR || c == C_LF) {
                if (echo) print("\r\n");
                buf[idx] = '\0';
                return true;
            }

            // Backspace
            if (c == C_BS) {
                if (idx > 0) {
                    idx--;
                    if (echo) print("\b \b"); // Backspace, Space, Backspace
                }
                continue;
            }

            // Printable characters
            if (idx < bufSize && c >= 0x20 && c <= 0x7E) {
                buf[idx++] = c;
                if (echo) putChar(c);
                continue;
            }
        }
        else if (ev.type == Event::SEP) {
            // Check for SEP 4/13 (ENVOI key)
            if (stopOnEnvoi && ev.code == SEP_ENVOI_KEY) { 
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
    size_t i = 0;
    // Maximum repetition count is 95, based on the max count character 0x7E - 0x1F
    static const size_t MAX_REP_COUNT = 95;
    // Minimum repetition length to save bytes (REP sequence is 3 bytes, so save for 4+)
    static const size_t REP_THRESHOLD = 4; 

    while (i < len) {
        uint8_t current_char = (uint8_t)s[i];
        size_t j = i;
        
        // Find the run length
        while (j < len && (uint8_t)s[j] == current_char) {
            j++;
        }
        size_t run_length = j - i;
        
        // Use C_REP sequence if run is long enough
        if (run_length >= REP_THRESHOLD) {
            size_t reps_to_send = run_length;
            
            // Handle runs longer than the maximum REP count
            while (reps_to_send > 0) {
                size_t current_reps = (reps_to_send > MAX_REP_COUNT) ? MAX_REP_COUNT : reps_to_send;

                // Send C_REP sequence: [C_REP] [Count] [Char]
                uint8_t count_byte = 0x1F + current_reps; // Count byte (0x20 for 1 rep, 0x7E for 95 reps)
                
                writeRaw(C_REP);
                writeRaw(count_byte);
                writeRaw(current_char);
                
                reps_to_send -= current_reps;
            }
            i = j; // Move index past the entire run
        } else {
            // Send characters raw (no repetition or too short run)
            for (size_t k = 0; k < run_length; k++) {
                writeRaw(current_char);
            }
            i = j; // Move index past the run
        }
    }
}


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
    // Optimization: Ensure G0 (alphanumeric) mode is active.
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI); // SI (Shift In)
        currentSet_ = CharSet::G0_ALPHA;
    }
    writeRaw((uint8_t)c);
}

void Minitel::print(const char* s) {
    // Optimization: Ensure G0 (alphanumeric) mode is active once.
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI); // SI (Shift In)
        currentSet_ = CharSet::G0_ALPHA;
    }
    // Now call the C_REP optimized printer
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
    // Optimization: only send SO if the state is not already G1
    if (currentSet_ != CharSet::G1_GRAPHIC) {
        writeRaw(C_SO); // SO (Shift Out)
        currentSet_ = CharSet::G1_GRAPHIC;
    }
}

void Minitel::endSemiGraphics() {
    // Optimization: only send SI if the state is not already G0
    if (currentSet_ != CharSet::G0_ALPHA) {
        writeRaw(C_SI); // SI (Shift In)
        currentSet_ = CharSet::G0_ALPHA;
    }
}

void Minitel::putSemiGraphic(uint8_t code) {
    // Optimization: uses beginSemiGraphics to handle state
    beginSemiGraphics();
    writeRaw(code & 0x7F); 
}

void Minitel::printSemiGraphics(const char* s) {
    // Optimization: Ensure G1 (semi-graphic) mode is active once.
    if (currentSet_ != CharSet::G1_GRAPHIC) {
        writeRaw(C_SO); // SO (Shift Out)
        currentSet_ = CharSet::G1_GRAPHIC;
    }
    // Now call the C_REP optimized printer
    printOptimized(s, strlen(s));
}
void Minitel::putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code) {
    // Optimized: Set cursor, switch set *once*, print, and switch back (if needed)
    setCursor(row, col);
    
    // Switch to G1
    beginSemiGraphics();
    writeRaw(code & 0x7F);
    
    // Switch back to G0 for safety/next print job
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

