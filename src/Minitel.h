#pragma once

#include <Arduino.h>
#include <Print.h>

/**
 * @file Minitel.h
 *
 * Unified Minitel peri-informatique driver (STUM M1 oriented, Minitel 1).
 *
 * - Uses a generic Stream (HardwareSerial recommended: 1200 bauds, SERIAL_7E1).
 * - Provides:
 *   - Unified event stream (chars, SEP, ESC sequences, controls).
 *   - Simple async "transaction" system (wait for SEP).
 *   - Screen helpers: clear, cursor, text, semi-graphics.
 *   - Keyboard helpers: readChar, readLine.
 *
 * Designed for Arduino Mega 2560 + Serial1, but works with any Stream.
 */
class Minitel : public Print {
public:

    // ---------------------------------------------------------------------
    // Exposed SEP key codes (per STUM M1, verified from user terminal)
    // These allow user code (main.cpp) to compare SEP events cleanly.
    // ---------------------------------------------------------------------
    static constexpr uint8_t SEP_SEND      = 0x41; // 4/1  ENVOI / SEND
    static constexpr uint8_t SEP_PREVIOUS  = 0x42; // 4/2  RETOUR
    static constexpr uint8_t SEP_REPEAT    = 0x43; // 4/3
    static constexpr uint8_t SEP_GUIDE     = 0x44; // 4/4
    static constexpr uint8_t SEP_CANCEL    = 0x45; // 4/5  ANNULATION
    static constexpr uint8_t SEP_INDEX     = 0x46; // 4/6
    static constexpr uint8_t SEP_ERASE     = 0x47; // 4/7  CORRECTION
    static constexpr uint8_t SEP_NEXT      = 0x48; // 4/8  SUITE
    static constexpr uint8_t SEP_CONNECT   = 0x49; // 4/9  CONNECT / DISCONNECT

    // // Aliases for user convenience
    // static constexpr uint8_t SEP_ENVOI     = SEP_SEND;
    // static constexpr uint8_t SEP_RETOUR    = SEP_PREVIOUS;
    // static constexpr uint8_t SEP_ANNUL     = SEP_CANCEL;

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

        uint8_t code;       ///< CHAR: character; SEP: second byte; ESCSEQ: opcode (e.g. 0x3B)
        uint8_t row;        ///< SEP row (for type == SEP)
        uint8_t col;        ///< SEP col (for type == SEP)
        uint8_t escLen;     ///< ESCSEQ: length of escData
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

    void setDebug(Stream* debug) { debug_ = debug; }

    // ---------------------------------------------------------------------
    // Print base (Print compatibility)
    // ---------------------------------------------------------------------
    virtual size_t write(uint8_t b) override;
    virtual size_t write(const uint8_t* buffer, size_t size) override;

    // Convenience print overloads (these call into our optimized routines)
    void print(const char* s);
    void println(const char* s);
    void println();
    void println(uint8_t v, int base = 10);
    void println(int v, int base = 10);
    void println(unsigned int v, int base = 10);
    void println(long v, int base = 10);
    void println(unsigned long v, int base = 10);

    void print(char c);
    void print(uint8_t v, int base = 10);
    void print(int v, int base = 10);
    void print(unsigned int v, int base = 10);
    void print(long v, int base = 10);
    void print(unsigned long v, int base = 10);

    // ---------------------------------------------------------------------
    // Session Management (PT/TP)
    // ---------------------------------------------------------------------

    /**
     * Asserts the PT line to start a Minitel session.
     * Optionally waits synchronously for SEP 5/4 acknowledgment.
     *
     * @param timeoutMs  Max time to wait for acknowledgment (0 for no wait).
     * @return true on success (or if timeoutMs == 0), false if wait failed.
     */
    bool startSession(uint16_t timeoutMs = 0);

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
     * Optimized for 1200 bauds: no delay(1) calls.
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
     * Optimized for 1200 bauds: no delay(1) inside parsing.
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
 * Position the cursor on row 00 (status row), at the given column (1–40).
 * Uses the special US 4/0, X/Y sequence described in STUM.
 */
void setCursorRow0(uint8_t col);

/**
 * Print a full status line on row 00.
 * - Text is truncated to 40 chars
 * - The remaining part of the row is filled with spaces
 * - A LF is sent at the end to leave row 00 and restore previous position
 */
void printRow0(const char* s);
    
    /**
     * Prints a single character in the current character set (G0 or G1).
     * Automatically switches back to G0 if needed for alphanumeric characters.
     */
    void putChar(char c);

    // ---------------------------------------------------------------------
    // Semi-Graphics Output (G1 Set)
    // ---------------------------------------------------------------------

    void putSemiGraphic(uint8_t code);
    void printSemiGraphics(const char* s);
    void putSemiGraphicAt(uint8_t row, uint8_t col, uint8_t code);
    void beginSemiGraphics();
    void endSemiGraphics();

    // ---------------------------------------------------------------------
    // PRO3: keyboard/screen switching
    // ---------------------------------------------------------------------

    void enablePRO3();
    void configureKeyboardToSocketOnly(bool useTransaction = false,
                                       uint16_t timeoutMs = 500);

    // ---------------------------------------------------------------------
    // Transaction engine
    // ---------------------------------------------------------------------

    /**
     * Starts a transaction waiting for a specific SEP sequence (e.g., 5/4).
     * Non-blocking: result is updated internally when SEP arrives or timeout.
     */
    void beginTransactionWaitSep(uint8_t sepRow, uint8_t sepCol, uint16_t timeoutMs);

    /**
     * Returns true if the last transaction was successful.
     */
    bool transactionSuccess() const { return tx_.success; }

        enum class CharSet : uint8_t {
        G0_ALPHA,   ///< Default (SI, Shift In)
        G1_GRAPHIC  ///< Semi-graphics (SO, Shift Out)
    };
    CharSet currentSet_ = CharSet::G0_ALPHA;



    // Couleurs 0..7 dans l'ordre du STUM
enum class Color : uint8_t {
    Black   = 0,
    Red     = 1,
    Green   = 2,
    Yellow  = 3,
    Blue    = 4,
    Magenta = 5,
    Cyan    = 6,
    White   = 7
};

void setCharColor(Color c);    // ESC 4/x
void setBgColor(Color c);      // ESC 5/x

void setFlash(bool enable);    // ESC 4/8 ou 4/9
void setLining(bool enable);   // ESC 4/A (start) ou 5/9 (stop field-level)
void setPolarity(bool negative); // ESC 5/C / 5/D (attention : pas en G1)
void setSizeNormal();          // ESC 4/C
void setDoubleHeight(bool on); // ESC 4/D + gestion interne
void setDoubleWidth(bool on);  // ESC 4/E + gestion interne
void setDoubleSize(bool on);   // ESC 4/F + gestion interne

// Masquage global (full screen mask behaviour)
void setMaskReveal(bool reveal); // false => conceal (5/8), true => reveal (5/F)


// Screen geometry (default Minitel 1)
uint8_t rows() const { return 24; }
uint8_t cols() const { return 40; }

// Fill spaces starting at current cursor
void fillSpaces(uint8_t count);

// Convenience: position and print 1 char
void putCharAt(uint8_t row, uint8_t col, char c);

bool requestCursorPosition(uint8_t& outRow, uint8_t& outCol, uint16_t timeoutMs = 300);


private:
    // --- Hardware and Debug ---
    Stream* stream_ = nullptr;
    Stream* debug_  = nullptr;
    uint8_t ptPin_  = 255;
    uint8_t tpPin_  = 255;

    // --- State ---
    SessionState   sessionState_       = SessionState::Closed;
    unsigned long lastSessionEventMs_  = 0;



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

    void setPT(bool active);
    void pushEvent(const Event& ev);
    void parseByte(uint8_t c);
    bool handleLineEditingControl(uint8_t c);
    void handleSep(uint8_t secondByte);
    void handleEscByte(uint8_t c);
    void onSepForTransaction(uint8_t row, uint8_t col);
    void checkTransactionTimeout();

    void printOptimized(const char* s, size_t len);
};
