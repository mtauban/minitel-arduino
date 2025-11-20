#pragma once

#include <Arduino.h>

/**
 * @file Minitel.h
 *
 * Unified Minitel peri-informatique driver (STUM M1 oriented, Minitel 1).
 *
 * - Uses a generic Stream (HardwareSerial recommended: 1200 bauds, SERIAL_7E1).
 * - Provides:
 *   - Unified event stream (chars, SEP, ESC sequences, controls).
 *   - Simple async "transaction" system (C-style callbacks).
 *   - Screen helpers: clear, cursor, text, semi-graphics.
 *   - Keyboard helpers: readChar, readLine.
 *
 * Designed for Arduino Mega 2560 + Serial1, but works with any Stream.
 */
class Minitel {
public:
    // ---------------------------------------------------------------------
    // Event type (unified RX abstraction)
    // ---------------------------------------------------------------------
    struct Event {
        enum Type : uint8_t {
            CHAR,      ///< Printable char or CR/LF/BS
            SEP,       ///< SEP 4/x, 5/x, etc. (two-byte sequence)
            ESCSEQ,    ///< ESC-based sequence (C1 or ESC 3B a b c)
            CONTROL,   ///< Other C0 controls
            TIMEOUT    ///< Artificial event used by blocking helpers
        } type;

        uint8_t code;     ///< CHAR: character; SEP: second byte; ESCSEQ: opcode (e.g. 0x3B)
        uint8_t row;      ///< SEP row (for type == SEP)
        uint8_t col;      ///< SEP col (for type == SEP)

        uint8_t escLen;          ///< For ESCSEQ: number of data bytes
        uint8_t escData[4];      ///< For ESCSEQ: payload (e.g. 3 bytes for PRO3)
    };

    // ---------------------------------------------------------------------
    // Session state (power / PT line)
    // ---------------------------------------------------------------------
    enum class SessionState : uint8_t {
        Closed,
        Opening,
        Open,
        Closing
    };

    // ---------------------------------------------------------------------
    // Transaction structure (simple async engine)
    //
    // Typical use:
    //  - Begin transaction waiting for a specific SEP row/col, with timeout.
    //  - Poll regularly; when SEP seen or timeout, callback is invoked.
    // ---------------------------------------------------------------------
    struct Transaction {
        bool     active         = false;
        bool     waitSep        = false;
        uint8_t  sepRow         = 0;
        uint8_t  sepCol         = 0;
        uint16_t timeoutMs      = 0;
        unsigned long startMs   = 0;

        void (*onSuccess)(void*) = nullptr;  ///< Called when condition is met
        void (*onTimeout)(void*) = nullptr;  ///< Called when timeout is reached
        void* userData           = nullptr;  ///< Opaque user pointer passed to callbacks
    };

    // ---------------------------------------------------------------------
    // Ctor / setup
    // ---------------------------------------------------------------------

    /**
     * Default constructor.
     * Call begin() before using the instance.
     */
    Minitel();

    /**
     * Attach an already-configured stream (1200 bauds, SERIAL_7E1).
     *
     * @param s      HardwareSerial / SoftwareSerial / any Stream.
     * @param ptPin  Optional PT control pin (active LOW). -1 if not used.
     * @param tpPin  Optional TP sense pin (reads terminal power). -1 if not used.
     */
    void begin(Stream& s, int ptPin = -1, int tpPin = -1);

    /**
     * Set optional debug stream (e.g. Serial).
     * RX/TX bytes and parsed events can be logged here.
     */
    void setDebug(Stream* dbg) { debug_ = dbg; }

    /**
     * Main polling function.
     * Call often from loop():
     *  - Reads incoming bytes from Minitel.
     *  - Parses C0/SEP/ESC sequences into Events.
     *  - Pushes them to the internal FIFO.
     *  - Updates the transaction engine (timeouts, SEP acks).
     */
    void poll();

    // ---------------------------------------------------------------------
    // Basic session & power
    // ---------------------------------------------------------------------

    /**
     * Start a Minitel session:
     *  - pulls PT low (if ptPin configured),
     *  - moves internal state to Opening.
     */
    void startSession();

    /**
     * End a Minitel session:
     *  - releases PT (if ptPin configured),
     *  - moves internal state to Closing.
     */
    void endSession();

    /**
     * Get current session state.
     */
    SessionState sessionState() const { return sessionState_; }

    /**
     * TP low => terminal powered ON (if wired).
     *
     * @return true if terminal seems powered, false otherwise or if no TP pin.
     */
    bool isTerminalOn() const;

    // ---------------------------------------------------------------------
    // Unified event API
    // ---------------------------------------------------------------------

    /**
     * Check if there is at least one Event waiting in the FIFO.
     */
    bool eventAvailable() const;

    /**
     * Pop next event from FIFO.
     *
     * @param ev  Output Event.
     * @return true if an event was available, false otherwise.
     */
    bool readEvent(Event& ev);

    /**
     * Blocking wait for any event (used internally by readChar/readLine).
     *
     * @param ev         Output Event.
     * @param timeoutMs  Timeout in milliseconds.
     * @return true if an event was read, false on timeout (ev.type == TIMEOUT).
     */
    bool waitEvent(Event& ev, uint16_t timeoutMs);

    // ---------------------------------------------------------------------
    // Keyboard convenience
    // ---------------------------------------------------------------------

    /**
     * Blocking char read from unified stream.
     * Only returns when a CHAR event is received or timeout expires.
     *
     * @param c          Output character.
     * @param timeoutMs  Timeout in milliseconds.
     * @return true if a char was read, false on timeout.
     */
    bool readChar(char& c, uint16_t timeoutMs);

    /**
     * High-level text input:
     *  - reads chars until CR/LF or ENVOI (SEP 4/13) or timeout.
     *  - stores up to maxLen-1 chars into buf (null-terminated).
     *
     * @param buf           Output buffer.
     * @param maxLen        Buffer size in bytes.
     * @param timeoutMs     Timeout in milliseconds.
     * @param stopOnEnvoi   If true, stop on ENVOI (SEP 4/13).
     * @param echoLocally   If true, echoes chars to the Minitel itself.
     *
     * @return true if terminated by CR/LF/ENVOI, false on timeout.
     */
    bool readLine(char* buf,
                  size_t maxLen,
                  uint16_t timeoutMs,
                  bool stopOnEnvoi = true,
                  bool echoLocally = false);

    // ---------------------------------------------------------------------
    // Screen & text helpers
    // ---------------------------------------------------------------------

    /**
     * Clear full screen (FF).
     */
    void clearScreen();

    /**
     * Move cursor to home (RS).
     */
    void home();

    /**
     * Position cursor.
     *
     * @param row  1–24.
     * @param col  1–40.
     */
    void setCursor(uint8_t row, uint8_t col);

    /**
     * Low-level single character output.
     */
    void putChar(char c);

    // --- text printing (C-string) -------------------------------------------

    void print(const char* s);
    void println(const char* s);
    void println();  ///< Empty line (CR/LF or equivalent)

    // --- numeric printing (Arduino-like) ------------------------------------

    void print(char c);
    void print(uint8_t v, int base = 10);
    void print(int v, int base = 10);
    void print(unsigned int v, int base = 10);
    void print(long v, int base = 10);
    void print(unsigned long v, int base = 10);

    // ---------------------------------------------------------------------
    // Semi-graphics (G1)
    // ---------------------------------------------------------------------

    /**
     * Enter semi-graphics (SO -> G1 set).
     */
    void beginSemiGraphics();

    /**
     * Leave semi-graphics (SI -> G0 set).
     */
    void endSemiGraphics();

    /**
     * Output one semi-graphic code (0x40–0x7F range typically).
     */
    void putSemiGraphic(uint8_t code);

    /**
     * Position cursor and output a semi-graphic code.
     */
    void putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code);

    // ---------------------------------------------------------------------
    // PRO3 switching helpers
    // ---------------------------------------------------------------------

    /**
     * Configure keyboard to send only to socket:
     *  - keyboard -> modem OFF
     *  - modem    -> screen OFF
     *  - keyboard -> socket ON
     *
     * If useTransaction is true, a transaction is started to wait for the
     * corresponding SEP acknowledgement, with timeoutMs.
     */
    void configureKeyboardToSocketOnly(bool useTransaction = false,
                                       uint16_t timeoutMs = 200);

    /**
     * Explicitly enable PRO3 mode (socket keyboard routing).
     * Typically wraps configureKeyboardToSocketOnly() with sane defaults.
     */
    void enablePRO3();

    // ---------------------------------------------------------------------
    // Transaction API
    // ---------------------------------------------------------------------

    /**
     * Start a transaction that completes when a given SEP row/col is seen.
     *
     * @param row        Expected SEP row.
     * @param col        Expected SEP col.
     * @param timeoutMs  Timeout in milliseconds. If 0: no timeout.
     * @param onSuccess  Optional callback on success.
     * @param onTimeout  Optional callback on timeout (if timeoutMs > 0).
     * @param userData   Opaque user pointer passed to callbacks.
     *
     * @return false if a transaction is already active, true otherwise.
     */
    bool beginTransactionWaitSep(uint8_t row,
                                 uint8_t col,
                                 uint16_t timeoutMs,
                                 void (*onSuccess)(void*) = nullptr,
                                 void (*onTimeout)(void*) = nullptr,
                                 void* userData = nullptr);

    /**
     * Cancel current transaction (if any).
     */
    void cancelTransaction();

    /**
     * Check if a transaction is currently active.
     */
    bool transactionActive() const { return tx_.active; }

    /**
     * Access current transaction (read-only).
     */
    const Transaction& currentTransaction() const { return tx_; }

    // ---------------------------------------------------------------------
    // C0/C1 & ESC-based writers (public low-level access)
    // ---------------------------------------------------------------------
public:
    /**
     * Write one raw byte to the underlying stream.
     * No parsing, no translation.
     */
    void writeRaw(uint8_t b);

    /**
     * Write a raw byte buffer to the underlying stream.
     * No parsing, no translation.
     */
    void writeRaw(const uint8_t* data, size_t len);

private:
    // ---------------------------------------------------------------------
    // Internal members
    // ---------------------------------------------------------------------

    Stream* io_    = nullptr;   ///< Main Minitel I/O stream.
    Stream* debug_ = nullptr;   ///< Optional debug stream.

    int ptPin_ = -1;            ///< PT control pin (active LOW). -1 if unused.
    int tpPin_ = -1;            ///< TP sense pin. -1 if unused.

    SessionState   sessionState_        = SessionState::Closed;
    unsigned long  lastSessionEventMs_  = 0;

    // Unified event FIFO
    static const uint8_t EVENTBUF_SIZE = 32;
    Event   eventBuf_[EVENTBUF_SIZE];
    uint8_t eventHead_ = 0;     ///< index of next free slot.
    uint8_t eventTail_ = 0;     ///< index of next unread event.

    // SEP and ESC parsing state
    bool waitingSepSecond_ = false;

    enum EscState : uint8_t {
        ESC_IDLE,
        ESC_GOT_ESC,
        ESC_3B
    };
    EscState escState_   = ESC_IDLE;
    uint8_t  escTmp_[4]  = {0};
    uint8_t  escTmpLen_  = 0;

    // Current transaction
    Transaction tx_;

    // ---------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------

    /**
     * Drive PT pin (if available).
     *
     * @param active  true => PT asserted (session ON), false => released.
     */
    void setPT(bool active);

    /**
     * Push a parsed Event into FIFO (drops event if FIFO full).
     */
    void pushEvent(const Event& ev);

    /**
     * Core byte parser: builds Events from incoming bytes.
     */
    void parseByte(uint8_t c);

    /**
     * Handle two-byte SEP sequences.
     */
    void handleSep(uint8_t secondByte);

    /**
     * Handle ESC-prefixed sequences.
     */
    void handleEscByte(uint8_t c);

    /**
     * Notify transaction engine that a SEP(row,col) was just received.
     */
    void onSepForTransaction(uint8_t row, uint8_t col);

    /**
     * Check for transaction timeout and call onTimeout if needed.
     */
    void checkTransactionTimeout();
};
