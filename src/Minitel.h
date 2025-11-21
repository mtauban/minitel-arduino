#pragma once

#include <Arduino.h>

/**
 * @file Minitel.h
 *
 * Unified Minitel peri-informatique driver (STUM M1 oriented, Minitel 1).
 *
 * - Uses a generic Stream (HardwareSerial recommended: 1200 bauds, SERIAL_7E1).
 * - Provides:
 * - Unified event stream (chars, SEP, ESC sequences, controls).
 * - Simple async "transaction" system (C-style callbacks).
 * - Screen helpers: clear, cursor, text, semi-graphics.
 * - Keyboard helpers: readChar, readLine.
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
        uint8_t escLen;   ///< ESCSEQ: length of escData
        uint8_t escData[4]; ///< ESCSEQ: sequence payload (max 4 bytes)
    };

    /**
     * Internal state of the Minitel session (PT line).
     */
    enum class SessionState : uint8_t {
        Closed,  ///< PT is released, session is off.
        Opening, ///< PT asserted, waiting for 5/4 ack.
        Open     ///< Session is open (Minitel ready to receive/send).
    };

    /**
     * Minitel transaction structure.
     */
    struct Transaction {
        bool active = false;          ///< true if transaction is running
        uint8_t sepRow = 0;           ///< expected SEP row (4 or 5)
        uint8_t sepCol = 0;           ///< expected SEP col (0 to 15)
        uint16_t timeoutMs = 0;       ///< timeout in ms
        unsigned long startTime = 0;  ///< millis() when transaction started
        bool success = false;         ///< result of transaction
    };

    // ---------------------------------------------------------------------
    // Constructor / Setup
    // ---------------------------------------------------------------------
    Minitel();

    /**
     * Initialize the Minitel driver.
     *
     * @param stream  The Stream instance (e.g., &Serial1)
     * @param ptPin   Arduino pin driving the PT line (e.g., 2). Set to 255 to disable.
     * @param tpPin   Arduino pin reading the TP line (e.g., 3). Set to 255 to disable.
     * @param debug   Optional Stream for debug output (e.g., &Serial).
     */
    void begin(Stream* stream, uint8_t ptPin = 255, uint8_t tpPin = 255, Stream* debug = nullptr);

    // ---------------------------------------------------------------------
    // Session Management (PT/TP)
    // ---------------------------------------------------------------------

    /**
     * Asserts the PT line to start a Minitel session.
     * Starts an internal transaction to wait for the SEP 5/4 acknowledgment.
     *
     * @param timeoutMs  Max time to wait for acknowledgment (0 for no wait).
     * @return true on success (SEP 5/4 received before timeout).
     */
    bool startSession(uint16_t timeoutMs = 2000);

    /**
     * Releases the PT line to end a Minitel session.
     */
    void endSession();

    /**
     * True if the TP (Terminal Powered) line is asserted.
     */
    bool isTerminalOn() const;

    /**
     * Current state of the Minitel session.
     */
    SessionState sessionState() const { return sessionState_; }

    // ---------------------------------------------------------------------
    // Core I/O and Polling
    // ---------------------------------------------------------------------

    /**
     * Polls the serial stream and processes incoming bytes into Events.
     * This must be called frequently in the main loop (or from blocking calls).
     */
    void poll();

    /**
     * Sends raw bytes directly to the Minitel stream.
     */
    void writeRaw(const uint8_t* data, size_t len);
    void writeRaw(uint8_t c);

    // ---------------------------------------------------------------------
    // Event Queue Access
    // ---------------------------------------------------------------------

    /**
     * Returns true if there is at least one unread event.
     */
    bool eventAvailable() const;

    /**
     * Reads the next event from the FIFO.
     *
     * @param ev  The Event structure to populate.
     * @return true if an event was read, false otherwise.
     */
    bool readEvent(Event& ev);

    /**
     * Blocks until an Event is available or timeout is reached.
     * Optimized for 1200 bauds: removed delay(1) calls.
     *
     * @param ev  The Event structure to populate.
     * @param timeoutMs  Max time to wait (0 for infinite wait).
     * @return true if an event was read, false otherwise (timeout/error).
     */
    bool waitEvent(Event& ev, uint16_t timeoutMs = 0);

    // ---------------------------------------------------------------------
    // Keyboard helpers
    // ---------------------------------------------------------------------

    /**
     * Blocks until a character is received (or timeout).
     *
     * @param timeoutMs  Max time to wait (0 for infinite wait).
     * @return The received 7-bit character code (0 if timeout).
     */
    uint8_t readChar(uint16_t timeoutMs = 0);

    /**
     * Blocks until a full line is received (or timeout).
     * Stops on CR (Enter), LF, or SEP 4/13 (ENVOI key).
     * Optimized for 1200 bauds: removed delay(1) calls.
     *
     * @param buf          Character buffer to write to.
     * @param bufSize      Size of the buffer.
     * @param echo         If true, echoes characters back to screen.
     * @param stopOnEnvoi  If true, stops when the ENVOI key (SEP 4/13) is pressed.
     * @param timeoutMs    Max time to wait for the entire line (0 for infinite).
     * @return true if a full line was read (CR/LF/ENVOI received).
     */
    bool readLine(char* buf, size_t bufSize, bool echo = true,
                  bool stopOnEnvoi = true, uint16_t timeoutMs = 0);

    // ---------------------------------------------------------------------
    // Screen control and text output (G0 Set)
    // ---------------------------------------------------------------------

    void clearScreen();
    void home();
    void setCursor(uint8_t row, uint8_t col);

    /**
     * Prints a single character in the current character set (G0 or G1).
     * Automatically switches back to G0 if needed for alphanumeric characters.
     */
    void putChar(char c);

    /**
     * Prints a string of alphanumeric characters.
     */
    void print(const char* s);
    void println(const char* s);

    // ---------------------------------------------------------------------
    // Semi-Graphics Output (G1 Set)
    // ---------------------------------------------------------------------

    /**
     * Prints a single character in the G1 (Semi-Graphics) set.
     * Optimized: only sends C_SO if G1 is not already active.
     */
    void putSemiGraphic(uint8_t code);

    /**
     * Prints a string of semi-graphic codes, optimizing for C_REP sequences.
     */
    void printSemiGraphics(const char* s);

    /**
     * Prints a single semi-graphic character at a specific position.
     * Optimized to manage G0/G1 switching efficiently.
     */
    void putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code);

    /**
     * Sends the C_SO code (Shift Out) to switch to the G1 (Semi-Graphics) set.
     * Only sends the byte if the set is not already G1.
     */
    void beginSemiGraphics();

    /**
     * Sends the C_SI code (Shift In) to switch back to the G0 (Alphanumeric) set.
     * Only sends the byte if the set is not already G0.
     */
    void endSemiGraphics();

    // ---------------------------------------------------------------------
    // PRO3: keyboard/screen switching
    // ---------------------------------------------------------------------

    /**
     * Enables PRO3 and sets up the default connection (MODEM TX/RX to SCREEN RX/TX).
     * PRO3 is required to use the PT/TP lines and to configure keyboard routing.
     */
    void enablePRO3();

    /**
     * Configures the Minitel to send keyboard input directly to the socket
     * (peripheral port), and listen to the socket for screen input.
     *
     * This is useful for using the Minitel as a full-duplex terminal device.
     */
    void configureKeyboardToSocketOnly(bool useTransaction = false, uint16_t timeoutMs = 500);

    // ---------------------------------------------------------------------
    // Transaction engine
    // ---------------------------------------------------------------------

    /**
     * Starts a transaction waiting for a specific SEP sequence (e.g., 5/4).
     */
    void beginTransactionWaitSep(uint8_t sepRow, uint8_t sepCol, uint16_t timeoutMs);

    /**
     * Returns true if the last transaction was successful.
     */
    bool transactionSuccess() const { return tx_.success; }

private:
    // --- Hardware and Debug ---
    Stream* stream_ = nullptr;
    Stream* debug_  = nullptr;
    uint8_t ptPin_  = 255;
    uint8_t tpPin_  = 255;

    // --- State ---
    SessionState sessionState_ = SessionState::Closed;

    enum class CharSet : uint8_t {
        G0_ALPHA,       ///< Default (SI, Shift In)
        G1_GRAPHIC      ///< Semi-graphics (SO, Shift Out)
    };
    CharSet currentSet_ = CharSet::G0_ALPHA; ///< Tracks the active character set for output optimization.

    // --- Event FIFO ---
    static const uint8_t EVENTBUF_SIZE = 16;
    Event   eventBuf_[EVENTBUF_SIZE];
    uint8_t eventHead_ = 0;     ///< index of next free slot.
    uint8_t eventTail_ = 0;     ///< index of next unread event.

    // --- SEP and ESC parsing state ---
    bool waitingSepSecond_ = false;

    enum EscState : uint8_t {
        ESC_IDLE,
        ESC_GOT_ESC,
        ESC_3B
    };
    EscState escState_   = ESC_IDLE;
    uint8_t  escTmp_[4]  = {0};
    uint8_t  escTmpLen_  = 0;

    // --- Current transaction ---
    Transaction tx_;

    // ---------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------

    /**
     * Drive PT pin (if available).
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
     * Handles complex C0 controls (HT, VT, DEL, etc.) by consuming the byte
     * and performing internal line-editing/cursor logic, keeping the Event FIFO clean.
     *
     * @param c  The 7-bit control code.
     * @return true if the byte was handled and consumed (no event generated).
     */
    bool handleLineEditingControl(uint8_t c);

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
     * Prints a string with C_REP (Repeat) optimization for contiguous characters.
     */
    void printOptimized(const char* s, size_t len);
};