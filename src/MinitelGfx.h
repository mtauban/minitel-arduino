#pragma once

#include <Arduino.h>
#include "Minitel.h"

class MinitelGfx
{
public:
    static constexpr uint8_t CELL_COLS = 40;
    static constexpr uint8_t CELL_ROWS = 24;
    static constexpr uint8_t PIXEL_COLS = CELL_COLS * 2; // 80
    static constexpr uint8_t PIXEL_ROWS = CELL_ROWS * 3; // 72

    static constexpr uint16_t NUM_CELLS = CELL_COLS * CELL_ROWS; // 960
    static constexpr uint16_t NUM_PIXELS = NUM_CELLS * 6;        // 5760

    explicit MinitelGfx(Minitel &dev);

    // Clear logical bitmap (and optionally clear screen).
    void clear(bool updateScreen = true);

    enum class FlushMode : uint8_t
    {
        FullRedraw,   ///< redraw every char, no diff logic
        OptimizedDiff ///< only update changed chars (with smart path)
    };

    void flush(FlushMode mode = FlushMode::OptimizedDiff);

    // ------------------- Drawing API in pixel space --------------------
    enum class DrawMode : uint8_t
    {
        BitmapOnly,
        Immediate
    };

    void setDrawMode(DrawMode mode) { drawMode_ = mode; }

    // ... déjà existant ...
    void drawPixel(int x, int y, bool on = true);
    void drawLine(int x0, int y0, int x1, int y1, bool on = true);
    void drawRect(int x, int y, int w, int h,
                  bool filled, bool on = true);

    // --- nouvelles primitives ---
    void drawPolyline(const int16_t *xs, const int16_t *ys,
                      uint8_t count, uint8_t thickness = 1, bool on = true);

    void drawPolygon(const int16_t *xs, const int16_t *ys, uint8_t count,
                     bool filled, uint8_t thickness = 1, bool on = true);

    void drawCircle(int cx, int cy, int radius,
                    bool filled, uint8_t thickness = 1, bool on = true);

    void drawTriangle(int x1, int y1,
                      int x2, int y2,
                      int x3, int y3,
                      bool filled, uint8_t thickness = 1, bool on = true);
    // Set the color used for subsequent drawing (when turning pixels ON).
    // This is stored per-cell and used by flush() to send setCharColor().
    void setDrawColor(Minitel::Color c) { drawColor_ = c; }

    // Optionally, let the user query it
    Minitel::Color drawColor() const { return drawColor_; }
    // ------------------------- SPRITE SUPPORT -------------------------
    //
    // Simple software sprites drawn at pixel level, with minimal state:
    // - frames: pointer to frame data (frameCount * height * width bytes, 0/1)
    // - width, height: in pixels (Minitel pixel grid: 80x72)
    // - frameCount: number of animation frames
    // - x, y: current top-left pixel position
    //
    // Usage pattern (in your sketch):
    //
    //   static const uint8_t mushroomFrames[...] = { ... };
    //
    //   MinitelGfx::Sprite mush;
    //   gfx.spriteInit(mush, mushroomFrames, 16, 16, 3);
    //   gfx.spriteSetPosition(mush, 10, 12);
    //   gfx.setDrawColor(Minitel::Color::White);
    //   gfx.spriteDraw(mush);
    //   gfx.flush(MinitelGfx::FlushMode::OptimizedDiff);
    //
    //   // on each frame:
    //   gfx.spriteSetPosition(mush, newX, newY);
    //   gfx.spriteNextFrame(mush);
    //   gfx.spriteDraw(mush);
    //   gfx.flush(OptimizedDiff);
    //
    struct Sprite
    {
        const uint8_t *frames = nullptr; // pointer to frame data (0/1 bytes)
        uint8_t width = 0;
        uint8_t height = 0;
        uint8_t frameCount = 0;

        int16_t x = 0; // current position
        int16_t y = 0;

        int16_t prevX = 0; // previous position (for erase)
        int16_t prevY = 0;
        uint8_t frame = 0;     // current frame index
        uint8_t prevFrame = 0; // previous frame index
        // Rotation (degrees)
        int16_t angleDeg = 0;     // current angle, in degrees
        int16_t prevAngleDeg = 0; // previous angle, for erase
    // NEW:
    uint8_t  scale       = 1;     // 1..N (integer magnification)
    uint8_t  prevScale   = 1;
    bool     flipX       = false; // mirror horizontally
    bool     flipY       = false; // flip vertically
    bool     prevFlipX   = false;
    bool     prevFlipY   = false;

        bool visible = true;
        bool firstDraw = true;
    };

    // Initialize a sprite with its frames and dimensions.
    // frames: pointer to frameCount*height*width bytes (0=off, !=0=on)
    void spriteInit(Sprite &spr,
                    const uint8_t *frames,
                    uint8_t width,
                    uint8_t height,
                    uint8_t frameCount);

    // Set current sprite position (top-left pixel)
    void spriteSetPosition(Sprite &spr, int16_t x, int16_t y);

    // Set current frame (0..frameCount-1)
    void spriteSetFrame(Sprite &spr, uint8_t frame);

    // Advance to next frame (looping)
    void spriteNextFrame(Sprite &spr);

    // Show/hide sprite. If hidden, spriteDraw() will do nothing until shown again.
    void spriteShow(Sprite &spr, bool visible);

    // Draw sprite:
    // - erase previous frame at previous position
    // - draw current frame at (x,y)
    // Uses current drawColor_ for ON pixels.
    // Does NOT call flush(), you must call gfx.flush() yourself.
    void spriteDraw(Sprite &spr);

    // Set current rotation angle (degrees, can be any integer; internally normalized)
    void spriteSetAngle(Sprite &spr, int16_t angleDeg);

    // Increment sprite angle by deltaDeg degrees
    void spriteRotateBy(Sprite &spr, int16_t deltaDeg);

    void spriteSetFlip(Sprite& spr, bool flipX, bool flipY);
    void spriteSetScale(Sprite& spr, uint8_t scale);   // scale>=1 (clamped)

private:
    Minitel &dev_;

    DrawMode drawMode_ = DrawMode::BitmapOnly;

    void updateCellOnScreen(uint8_t col, uint8_t row);

    uint8_t cellMask_[NUM_CELLS];
    uint8_t lastCellMask_[NUM_CELLS];
    // NEW: per-cell foreground color (index of Minitel::Color)
    uint8_t cellColor_[NUM_CELLS];
    uint8_t lastCellColor_[NUM_CELLS];

    // Color currently used for drawing new pixels
    Minitel::Color drawColor_ = Minitel::Color::White;

    // GFX's best guess of the current terminal FG color (for optimization)
    Minitel::Color termFgColor_ = Minitel::Color::White;

    // Current known cursor position (1-based Minitel coords)
    int curRow_ = 1;
    int curCol_ = 1;
    bool hasCursor_ = false; // false until first gotoCell() sets it

    // ---------- index helpers (as before) ----------
    static uint16_t charIndex(uint8_t col, uint8_t row);
    static uint16_t pixelBaseForChar(uint8_t col, uint8_t row);
    static uint16_t pixelIndexFromXY(uint8_t x, uint8_t y);
    static uint8_t subPixelIndexInChar(uint8_t xInChar, uint8_t yInChar);

    void setSubPixelByChar(uint8_t col, uint8_t row,
                           uint8_t subIndex, bool on);

    uint8_t maskToG1(uint8_t mask) const;

    // NEW: optimized move in alpha-cell space
    void gotoCell(uint8_t row, uint8_t col);
    void advanceCursorAfterPrint();

    void drawLineThick(int x0, int y0, int x1, int y1,
                       uint8_t thickness, bool on);

    // Helper to blit one sprite frame at (dstX,dstY) with rotation
void spriteBlitFrame(const Sprite& spr,
                     int16_t dstX,
                     int16_t dstY,
                     uint8_t frameIndex,
                     int16_t angleDeg,
                     uint8_t scale,
                     bool flipX,
                     bool flipY,
                     bool on);

};
