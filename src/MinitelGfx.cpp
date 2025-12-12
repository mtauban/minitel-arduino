#include "MinitelGfx.h"
#include <string.h>

static const uint8_t C_REP = 0x12;       // repetition control code
static const uint8_t REP_THRESHOLD = 4;  // use REP only if run >= 4
static const uint8_t MAX_REP_COUNT = 95; // 0x20..0x7E range

static int16_t normalizeAngleDeg(int16_t a)
{
    // Keep angle in range [0,360)
    int16_t r = a % 360;
    if (r < 0)
        r += 360;
    return r;
}

MinitelGfx::MinitelGfx(Minitel &dev)
    : dev_(dev)
{
    clear(false);
    // Force full refresh on first flush
    memset(lastCellMask_, 0xFF, sizeof(lastCellMask_));

    // Initialize colors to White (7)
    memset(cellColor_, static_cast<uint8_t>(Minitel::Color::White), sizeof(cellColor_));
    memset(lastCellColor_, static_cast<uint8_t>(Minitel::Color::White), sizeof(lastCellColor_));
    drawColor_ = Minitel::Color::White;
    termFgColor_ = Minitel::Color::White;
}

// ---------------------- Index helpers -------------------------

uint16_t MinitelGfx::charIndex(uint8_t col, uint8_t row)
{
    return (uint16_t)row * CELL_COLS + (uint16_t)col;
}

uint16_t MinitelGfx::pixelBaseForChar(uint8_t col, uint8_t row)
{
    return charIndex(col, row) * 6u;
}

uint8_t MinitelGfx::subPixelIndexInChar(uint8_t xInChar, uint8_t yInChar)
{
    // p1..p6 as:
    // (0,0)->0, (1,0)->1, (0,1)->2, (1,1)->3, (0,2)->4, (1,2)->5
    return (uint8_t)(yInChar * 2 + xInChar);
}

uint16_t MinitelGfx::pixelIndexFromXY(uint8_t x, uint8_t y)
{
    uint8_t col = x / 2; // char column
    uint8_t row = y / 3; // char row

    uint8_t xInChar = x % 2;
    uint8_t yInChar = y % 3;
    uint8_t subIdx = subPixelIndexInChar(xInChar, yInChar);

    uint16_t k = charIndex(col, row);
    return (uint16_t)6 * k + subIdx;
}

// ---------------------- Bitmap management -------------------------

void MinitelGfx::clear(bool updateScreen)
{
    // Clear logical bitmap
    memset(cellMask_, 0, sizeof(cellMask_));
    memset(lastCellMask_, 0, sizeof(lastCellMask_));

    // Keep a consistent color state (all white by default)
    memset(cellColor_, static_cast<uint8_t>(Minitel::Color::White), sizeof(cellColor_));
    memset(lastCellColor_, static_cast<uint8_t>(Minitel::Color::White), sizeof(lastCellColor_));

    hasCursor_ = false;
    curRow_ = 1;
    curCol_ = 1;

    if (updateScreen)
    {
        // Fast clear on the terminal
        dev_.clearScreen();
        dev_.home();
        termFgColor_ = Minitel::Color::White;
    }
}

// ---------------------- Pixel set helper -------------------------

void MinitelGfx::setSubPixelByChar(uint8_t col, uint8_t row,
                                   uint8_t subIndex, bool on)
{
    if (col >= CELL_COLS || row >= CELL_ROWS || subIndex >= 6)
        return;

    uint16_t k = charIndex(col, row);
    uint8_t bit = (1u << subIndex);

    if (on)
    {
        cellMask_[k] |= bit;
        // Stamp the cell with the current drawing color
        cellColor_[k] = static_cast<uint8_t>(drawColor_);
    }
    else
    {
        cellMask_[k] &= ~bit;
        // When turning bits off we keep the color as-is, so that if the cell
        // is still partially ON, the color is preserved.
    }
}

// ---------------------- Drawing primitives -------------------------

void MinitelGfx::drawPixel(int x, int y, bool on)
{
    if (x < 0 || x >= PIXEL_COLS ||
        y < 0 || y >= PIXEL_ROWS)
    {
        return;
    }

    uint8_t col = x / 2;
    uint8_t row = y / 3;
    uint8_t xInChar = x % 2;
    uint8_t yInChar = y % 3;
    uint8_t subIdx = subPixelIndexInChar(xInChar, yInChar);

    setSubPixelByChar(col, row, subIdx, on);

    // Si on est en mode immédiat, on met à jour la cellule à l'écran
    if (drawMode_ == DrawMode::Immediate)
    {
        updateCellOnScreen(col, row);
    }
}

void MinitelGfx::drawLine(int x0, int y0, int x1, int y1, bool on)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        drawPixel(x0, y0, on);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void MinitelGfx::drawRect(int x, int y, int w, int h,
                          bool filled, bool on)
{
    if (w <= 0 || h <= 0)
        return;

    int x2 = x + w - 1;
    int y2 = y + h - 1;

    if (filled)
    {
        for (int yy = y; yy <= y2; ++yy)
        {
            drawLine(x, yy, x2, yy, on);
        }
    }
    else
    {
        drawLine(x, y, x2, y, on);
        drawLine(x, y2, x2, y2, on);
        drawLine(x, y, x, y2, on);
        drawLine(x2, y, x2, y2, on);
    }
}

// ---------------------- mask -> G1 code -------------------------

uint8_t MinitelGfx::maskToG1(uint8_t mask) const
{
    mask &= 0x3F; // 6 bits

    if (mask == 0)
    {
        return 0x20; // all background -> 2/0
    }
    if (mask == 0x3F)
    {
        return 0x5F; // all foreground -> 5/15 (STUM trap)
    }
    if (mask < 0x20)
    {
        return (uint8_t)(0x20 + mask); // 1..31 -> 0x21..0x3F
    }
    return (uint8_t)(0x60 + (mask - 0x20)); // 32..62 -> 0x60..0x7E
}

void MinitelGfx::flush(FlushMode mode)
{
    // We don't rely on previous cursor state for now
    hasCursor_ = false;

    // Helper: update our snapshot of the screen state
    auto syncLastBuffers = [&]()
    {
        memcpy(lastCellMask_, cellMask_, sizeof(cellMask_));
        memcpy(lastCellColor_, cellColor_, sizeof(cellColor_));
    };

    if (mode == FlushMode::FullRedraw)
    {
        // -------- FULL REDRAW: redraw ALL cells, row by row --------
        termFgColor_ = drawColor_; // not strictly true, but we'll fix as we go

        for (uint8_t row = 0; row < CELL_ROWS; ++row)
        {
            uint8_t termRow = row + 1;

            // Move to start of row once
            dev_.setCursor(termRow, 1);
            dev_.beginSemiGraphics();

            uint8_t runCode = 0;
            uint8_t runLen = 0;
            uint8_t runColor = static_cast<uint8_t>(termFgColor_);

            auto flushRun = [&](uint8_t code, uint8_t len, uint8_t colorIdx)
            {
                if (len == 0)
                    return;

                // Ensure terminal uses the right color for this run
                Minitel::Color c = static_cast<Minitel::Color>(colorIdx);
                if (c != termFgColor_)
                {
                    dev_.setCharColor(c);
                    termFgColor_ = c;
                }

                // REP optimization on this run
                while (len > 0)
                {
                    uint8_t chunk = (len > 64) ? 64 : len;

                    if (chunk < REP_THRESHOLD)
                    {
                        // Too short, send chars directly
                        for (uint8_t i = 0; i < chunk; ++i)
                        {
                            dev_.putSemiGraphic(code);
                        }
                    }
                    else
                    {
                        // chunk >= REP_THRESHOLD
                        uint8_t repeats = (uint8_t)(chunk - 1);
                        dev_.putSemiGraphic(code);
                        uint8_t countByte = 0x40 + repeats;
                        dev_.writeRaw(C_REP);
                        dev_.writeRaw(countByte);
                    }

                    len -= chunk;
                }
            };

            for (uint8_t col = 0; col < CELL_COLS; ++col)
            {
                uint16_t k = charIndex(col, row);
                uint8_t mask = cellMask_[k];
                uint8_t code = maskToG1(mask);
                uint8_t clr = cellColor_[k];

                if (runLen == 0)
                {
                    runCode = code;
                    runLen = 1;
                    runColor = clr;
                }
                else if (code == runCode && clr == runColor && runLen < 64)
                {
                    ++runLen;
                }
                else
                {
                    flushRun(runCode, runLen, runColor);
                    runCode = code;
                    runLen = 1;
                    runColor = clr;
                }
            }

            // End of row
            flushRun(runCode, runLen, runColor);
            dev_.endSemiGraphics();
        }

        syncLastBuffers();
        return;
    }

    // -------- OPTIMIZED DIFF: only changed cells, grouped by segments --------
    bool anyChange = false;

    // We'll stay mostly in G0 and enter G1 only for segments
    // termFgColor_ tracks our current guess of semi-graphic FG color.
    // We'll correct it on demand on each run.
    for (uint8_t row = 0; row < CELL_ROWS; ++row)
    {
        uint8_t termRow = row + 1;

        bool inSegment = false;
        uint8_t segStartCol = 0;
        uint8_t runCode = 0;
        uint8_t runLen = 0;
        uint8_t runColorIdx = static_cast<uint8_t>(termFgColor_);

        auto flushSegment = [&]()
        {
            if (!inSegment || runLen == 0)
                return;

            anyChange = true;

            // Move cursor at the beginning of the segment
            uint8_t termCol = segStartCol + 1;
            dev_.setCursor(termRow, termCol);
            dev_.beginSemiGraphics();

            // Ensure correct color for this run
            Minitel::Color c = static_cast<Minitel::Color>(runColorIdx);
            if (c != termFgColor_)
            {
                dev_.setCharColor(c);
                termFgColor_ = c;
            }

            // Emit the run with REP optimization (same as in full redraw)
            uint8_t code = runCode;
            uint8_t len = runLen;

            while (len > 0)
            {
                uint8_t chunk = (len > 64) ? 64 : len;

                if (chunk < REP_THRESHOLD)
                {
                    for (uint8_t i = 0; i < chunk; ++i)
                    {
                        dev_.putSemiGraphic(code);
                    }
                }
                else
                {
                    uint8_t repeats = (uint8_t)(chunk - 1);
                    dev_.putSemiGraphic(code);
                    uint8_t countByte = 0x40 + repeats;
                    dev_.writeRaw(C_REP);
                    dev_.writeRaw(countByte);
                }

                len -= chunk;
            }

            inSegment = false;
            runLen = 0;
        };

        for (uint8_t col = 0; col < CELL_COLS; ++col)
        {
            uint16_t k = charIndex(col, row);
            uint8_t curMask = cellMask_[k];
            uint8_t prevMask = lastCellMask_[k];
            uint8_t curCol = cellColor_[k];
            uint8_t prevCol = lastCellColor_[k];

            bool changed = (curMask != prevMask) || (curCol != prevCol);

            if (!changed)
            {
                // If we were in a segment, close it before this gap
                if (inSegment)
                {
                    flushSegment();
                }
                continue;
            }

            // Cell changed: get its char & color
            uint8_t code = maskToG1(curMask);

            if (!inSegment)
            {
                // Start new segment at this col
                inSegment = true;
                segStartCol = col;
                runCode = code;
                runLen = 1;
                runColorIdx = curCol;
            }
            else
            {
                // Extend or restart run inside the segment
                if (code == runCode && curCol == runColorIdx && runLen < 64)
                {
                    ++runLen;
                }
                else
                {
                    // Flush current run, start another at this col
                    flushSegment();
                    inSegment = true;
                    segStartCol = col;
                    runCode = code;
                    runLen = 1;
                    runColorIdx = curCol;
                }
            }
        }

        // End of row: flush trailing segment if any
        flushSegment();
    }

    if (anyChange)
    {
        dev_.endSemiGraphics();
    }

    syncLastBuffers();
}

void MinitelGfx::advanceCursorAfterPrint()
{
    // Minitel behaviour (simplified model):
    // - Each printed char moves cursor one step right.
    // - At col 40, next print goes to col 1 of next row (scrolling details ignored).
    curCol_++;
    if (curCol_ > CELL_COLS)
    {
        curCol_ = 1;
        if (curRow_ < CELL_ROWS)
        {
            curRow_++;
        }
        // If curRow_ == CELL_ROWS and we print more, terminal may scroll;
        // for path estimation it's "good enough" that we cap here.
    }
}

void MinitelGfx::gotoCell(uint8_t row, uint8_t col)
{
    // Clamp to 1..CELL_ROWS / 1..CELL_COLS
    if (row < 1)
        row = 1;
    if (row > CELL_ROWS)
        row = CELL_ROWS;
    if (col < 1)
        col = 1;
    if (col > CELL_COLS)
        col = CELL_COLS;

    // First move: we may not trust curRow_/curCol_ yet
    // If this is the first use after startup, we force a US jump.
    if (!hasCursor_)
    {
        dev_.setCursor(row, col); // US + row/col, resets attributes
        dev_.beginSemiGraphics(); // back to G1
        curRow_ = row;
        curCol_ = col;
        hasCursor_ = true;
        return;
    }

    int dr = (int)row - curRow_;
    int dc = (int)col - curCol_;

    // Relative movement cost:
    // BS/HT/LF/VT are 1 byte each and keep G1.
    int costRelative = abs(dr) + abs(dc);

    // Absolute move cost:
    // US (0x1F) + row + col + SO (0x0E) = 4 bytes
    const int costUS = 4;

    if (costRelative <= costUS)
    {
        // Use relative moves only (no attribute change)
        // Vertical first
        while (dr > 0)
        {
            dev_.writeRaw(0x0A); // LF: down
            dr--;
            if (curRow_ < CELL_ROWS)
                curRow_++;
        }
        while (dr < 0)
        {
            dev_.writeRaw(0x0B); // VT: up
            dr++;
            if (curRow_ > 1)
                curRow_--;
        }

        // Then horizontal
        while (dc > 0)
        {
            dev_.writeRaw(0x09); // HT: right
            dc--;
            if (curCol_ < CELL_COLS)
                curCol_++;
        }
        while (dc < 0)
        {
            dev_.writeRaw(0x08); // BS: left
            dc++;
            if (curCol_ > 1)
                curCol_--;
        }
    }
    else
    {
        // Use US + SO
        dev_.setCursor(row, col); // STUM: resets attributes, back to G0
        dev_.beginSemiGraphics(); // re-enter G1
        curRow_ = row;
        curCol_ = col;
    }

    curRow_ = row;
    curCol_ = col;
}

void MinitelGfx::updateCellOnScreen(uint8_t col, uint8_t row)
{
    if (drawMode_ != DrawMode::Immediate)
        return;
    if (col >= CELL_COLS || row >= CELL_ROWS)
        return;

    uint16_t k = charIndex(col, row);
    uint8_t mask = cellMask_[k];

    // Si pas de changement vs dernier flush / update, on ne fait rien
    if (mask == lastCellMask_[k])
        return;

    uint8_t termRow = row + 1;
    uint8_t termCol = col + 1;

    // Chemin de curseur "smart" (relatif ou US) déjà géré ici
    gotoCell(termRow, termCol);

    dev_.beginSemiGraphics();
    uint8_t code = maskToG1(mask);
    dev_.putSemiGraphic(code);
    advanceCursorAfterPrint();

    lastCellMask_[k] = mask;
}

void MinitelGfx::drawLineThick(int x0, int y0, int x1, int y1,
                               uint8_t thickness, bool on)
{
    if (thickness <= 1)
    {
        drawLine(x0, y0, x1, y1, on);
        return;
    }

    int dx = x1 - x0;
    int dy = y1 - y0;

    // Approximation: si ligne plus horizontale que verticale,
    // on épaissit verticalement, sinon horizontalement
    if (abs(dx) >= abs(dy))
    {
        // plus horizontale -> offsets verticaux
        int half = thickness / 2;
        for (int o = -half; o <= half; ++o)
        {
            drawLine(x0, y0 + o, x1, y1 + o, on);
        }
    }
    else
    {
        // plus verticale -> offsets horizontaux
        int half = thickness / 2;
        for (int o = -half; o <= half; ++o)
        {
            drawLine(x0 + o, y0, x1 + o, y1, on);
        }
    }
}

void MinitelGfx::spriteInit(Sprite &spr,
                            const uint8_t *frames,
                            uint8_t width,
                            uint8_t height,
                            uint8_t frameCount)
{
    spr.frames = frames;
    spr.width = width;
    spr.height = height;
    spr.frameCount = frameCount;

    spr.x = spr.y = 0;
    spr.prevX = spr.prevY = 0;
    spr.frame = spr.prevFrame = 0;

    spr.angleDeg = 0;
    spr.prevAngleDeg = 0;

spr.scale = spr.prevScale = 1;
spr.flipX = spr.prevFlipX = false;
spr.flipY = spr.prevFlipY = false;

    spr.visible = true;
    spr.firstDraw = true;
}

void MinitelGfx::spriteSetPosition(Sprite &spr, int16_t x, int16_t y)
{
    spr.x = x;
    spr.y = y;
}

void MinitelGfx::spriteSetFrame(Sprite &spr, uint8_t frame)
{
    if (spr.frameCount == 0)
    {
        spr.frame = 0;
        return;
    }
    if (frame >= spr.frameCount)
    {
        frame = spr.frameCount - 1;
    }
    spr.frame = frame;
}

void MinitelGfx::spriteNextFrame(Sprite &spr)
{
    if (spr.frameCount == 0)
        return;
    spr.frame = (spr.frame + 1) % spr.frameCount;
}

void MinitelGfx::spriteShow(Sprite &spr, bool visible)
{
    spr.visible = visible;
}

void MinitelGfx::spriteBlitFrame(const Sprite& spr,
                                 int16_t dstX,
                                 int16_t dstY,
                                 uint8_t frameIndex,
                                 int16_t angleDeg,
                                 uint8_t scale,
                                 bool flipX,
                                 bool flipY,
                                 bool on)
{
    if (!spr.frames) return;
    if (spr.width == 0 || spr.height == 0) return;
    if (spr.frameCount == 0) return;

    if (scale < 1) scale = 1;
    if (scale > 6) scale = 6;

    frameIndex %= spr.frameCount;

    const uint8_t* base = spr.frames +
                          (uint32_t)frameIndex * spr.width * spr.height;

    angleDeg = normalizeAngleDeg(angleDeg);

    const int16_t outW = (int16_t)spr.width  * (int16_t)scale;
    const int16_t outH = (int16_t)spr.height * (int16_t)scale;

    // Fast path: no rotation
    if (angleDeg == 0) {
        for (int16_t oy = 0; oy < outH; ++oy) {
            int16_t y = dstY + oy;
            if (y < 0 || y >= (int16_t)PIXEL_ROWS) continue;

            // Map output y -> source y (nearest)
            int16_t sy = oy / scale;
            if (flipY) sy = (int16_t)spr.height - 1 - sy;

            for (int16_t ox = 0; ox < outW; ++ox) {
                int16_t x = dstX + ox;
                if (x < 0 || x >= (int16_t)PIXEL_COLS) continue;

                int16_t sx = ox / scale;
                if (flipX) sx = (int16_t)spr.width - 1 - sx;

                // Safe bounds (should already be safe)
                if (sx < 0 || sy < 0 || sx >= spr.width || sy >= spr.height) continue;

                uint8_t v = base[sy * spr.width + sx];
                if (v) {
                    drawPixel((uint8_t)x, (uint8_t)y, on);
                }
            }
        }
        return;
    }

    // General case: rotation around scaled sprite center (then inverse-map)
    float angleRad = (float)angleDeg * (3.14159265f / 180.0f);
    float ca = cosf(angleRad);
    float sa = sinf(angleRad);

    float cx = outW * 0.5f;
    float cy = outH * 0.5f;
    float centerX = dstX + cx;
    float centerY = dstY + cy;

    // Bounding circle radius of the scaled sprite box
    float r = sqrtf(cx * cx + cy * cy);

    int16_t minX = (int16_t)floorf(centerX - r);
    int16_t maxX = (int16_t)ceilf (centerX + r);
    int16_t minY = (int16_t)floorf(centerY - r);
    int16_t maxY = (int16_t)ceilf (centerY + r);

    // Clamp to screen
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= (int16_t)PIXEL_COLS) maxX = PIXEL_COLS - 1;
    if (maxY >= (int16_t)PIXEL_ROWS) maxY = PIXEL_ROWS - 1;

    for (int16_t y = minY; y <= maxY; ++y) {
        for (int16_t x = minX; x <= maxX; ++x) {
            float dx = (float)x - centerX;
            float dy = (float)y - centerY;

            // inverse rotate into sprite output space
            float ox =  ca * dx + sa * dy + cx;
            float oy = -sa * dx + ca * dy + cy;

            // outside output sprite box => transparent
            if (ox < 0.0f || oy < 0.0f || ox >= (float)outW || oy >= (float)outH) continue;

            // downscale to source pixel coords
            int16_t sx = (int16_t)floorf(ox / (float)scale);
            int16_t sy = (int16_t)floorf(oy / (float)scale);

            // flips in source space
            if (flipX) sx = (int16_t)spr.width  - 1 - sx;
            if (flipY) sy = (int16_t)spr.height - 1 - sy;

            if (sx < 0 || sy < 0 || sx >= spr.width || sy >= spr.height) continue;

            uint8_t v = base[sy * spr.width + sx];
            if (v) {
                drawPixel((uint8_t)x, (uint8_t)y, on);
            }
        }
    }
}


void MinitelGfx::spriteDraw(Sprite& spr) {
    if (!spr.visible) return;

    if (!spr.firstDraw) {
        spriteBlitFrame(spr,
                        spr.prevX, spr.prevY,
                        spr.prevFrame,
                        spr.prevAngleDeg,
                        spr.prevScale,
                        spr.prevFlipX,
                        spr.prevFlipY,
                        false);
    }

    spriteBlitFrame(spr,
                    spr.x, spr.y,
                    spr.frame,
                    spr.angleDeg,
                    spr.scale,
                    spr.flipX,
                    spr.flipY,
                    true);

    spr.prevX        = spr.x;
    spr.prevY        = spr.y;
    spr.prevFrame    = spr.frame;
    spr.prevAngleDeg = spr.angleDeg;
    spr.prevScale    = spr.scale;
    spr.prevFlipX    = spr.flipX;
    spr.prevFlipY    = spr.flipY;
    spr.firstDraw    = false;
}




void MinitelGfx::spriteSetAngle(Sprite &spr, int16_t angleDeg)
{
    spr.angleDeg = normalizeAngleDeg(angleDeg);
}

void MinitelGfx::spriteRotateBy(Sprite &spr, int16_t deltaDeg)
{
    spr.angleDeg = normalizeAngleDeg(spr.angleDeg + deltaDeg);
}

void MinitelGfx::spriteSetFlip(Sprite& spr, bool flipX, bool flipY) {
    spr.flipX = flipX;
    spr.flipY = flipY;
}

void MinitelGfx::spriteSetScale(Sprite& spr, uint8_t scale) {
    if (scale < 1) scale = 1;
    // optional clamp to keep it sane on Mega
    if (scale > 6) scale = 6;
    spr.scale = scale;
}