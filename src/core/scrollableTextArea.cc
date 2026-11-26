#include "scrollableTextArea.h"
#include "i18n.h"
#define _scrollBuffer tft
ScrollableTextArea::ScrollableTextArea(const String &title)
    : firstVisibleLine{0}, _redraw{true}, _title(title), _fontSize(FP), _startX(BORDER_PAD_X),
      _startY(BORDER_PAD_Y), _width(tftWidth - 2 * BORDER_PAD_X),
      _height(tftHeight - BORDER_OFFSET_FROM_SCREEN_EDGE - BORDER_PAD_X - BORDER_PAD_Y) {
    drawMainBorder();

    if (!_title.isEmpty()) {
        printTitle(_title);
        _startY = tft.getCursorY();
        _height -= (_startY - BORDER_PAD_Y);
    }

    setup();
}

ScrollableTextArea::ScrollableTextArea(
    uint8_t fontSize, int16_t startX, int16_t startY, int32_t width, int32_t height, bool drawBorders,
    bool indentWrappedLines
)
    : firstVisibleLine{0}, _redraw{true}, _title(""), _fontSize(fontSize), _startX(startX), _startY(startY),
      _width(width), _height(height), _indentWrappedLines(indentWrappedLines) {
    if (drawBorders) { drawMainBorder(); }
    setup();
}

ScrollableTextArea::~ScrollableTextArea() {
    // We don't use Sprites for big things, unfortunetly theres no much RAM in all devices
}

void ScrollableTextArea::setup() {
    _scrollBuffer.setTextColor(uiTextBrightColor(), bruceConfig.bgColor);
    _scrollBuffer.setTextSize(_fontSize);
    _scrollBuffer.fillRect(_startX, _startY, _width, _height, bruceConfig.bgColor);

    int32_t textW = max((int32_t)16, _width - 6); // reserve slim 2px right-edge scrollbar gutter
    _maxCharactersPerLine = max(1, (int)floor(textW / _scrollBuffer.textWidth("w", _fontSize)));
    _pixelsPerLine = (_fontSize == 1 && bruce_i18n::isChinese()) ? 14 : (_scrollBuffer.fontHeight() + 3);
    _maxVisibleLines = max(1, (int)floor(_height / _pixelsPerLine));
}

void ScrollableTextArea::scrollUp() {
    if (firstVisibleLine > 0) {
        firstVisibleLine--;
        _redraw = true;
    }
}

void ScrollableTextArea::scrollDown() {
    if (firstVisibleLine + _maxVisibleLines < linesBuffer.size()) {
        firstVisibleLine++;
        _redraw = true;
    }
}

void ScrollableTextArea::scrollToLine(size_t lineNumber) {
    if (linesBuffer.empty()) return; // Ensure there's content to scroll

    if (lineNumber > linesBuffer.size() - _maxVisibleLines) {
        firstVisibleLine =
            (linesBuffer.size() > _maxVisibleLines) ? linesBuffer.size() - _maxVisibleLines : 0;
    } else {
        firstVisibleLine = lineNumber;
    }
}

String ScrollableTextArea::getLine(size_t lineNumber) {
    return linesBuffer[(lineNumber >= linesBuffer.size()) ? linesBuffer.size() : lineNumber];
}

size_t ScrollableTextArea::getMaxLines() { return linesBuffer.size(); }

void ScrollableTextArea::show(bool force) {
    draw(force);

    while (check(SelPress) || check(EscPress)) {
        update();
        yield();
    }
    while (!check(SelPress) && !check(EscPress)) {
        update();
        yield();
    }
}

uint32_t ScrollableTextArea::getMaxVisibleTextLength() { return _maxVisibleLines * _maxCharactersPerLine; }

void ScrollableTextArea::update(bool force) {
#ifdef HAS_ENCODER
    int32_t rotarySteps = drainRotarySteps();
    if (rotarySteps != 0) {
        check(PrevPress);
        check(NextPress);
        check(UpPress);
        check(DownPress);
        while (rotarySteps > 0) {
            scrollUp();
            rotarySteps--;
        }
        while (rotarySteps < 0) {
            scrollDown();
            rotarySteps++;
        }
        vTaskDelay(4 / portTICK_PERIOD_MS);
        PrevPress = false;
        NextPress = false;
        UpPress = false;
        DownPress = false;
    }
#endif

    if (check(PrevPress) || check(UpPress)) scrollUp();
    else if (check(NextPress) || check(DownPress)) scrollDown();

    draw(force);
}

void ScrollableTextArea::fromFile(File file) {
    linesBuffer.clear();
    while (file.available()) addLine(file.readStringUntil('\n'));

    draw(true);
    delay(100);
    draw(true);
}

void ScrollableTextArea::clear() {
    firstVisibleLine = 0;
    linesBuffer.clear();
}

void ScrollableTextArea::fromString(const String &text) {
    clear();
    int startIdx = 0;
    int endIdx = 0;

    while (endIdx < text.length()) {
        if (text[endIdx] == '\n') {
            addLine(text.substring(startIdx, endIdx));
            startIdx = endIdx + 1;
        }

        endIdx++;
    }

    // Add the last line if there's remaining text (text does not ends with \n)
    if (startIdx < text.length()) { addLine(text.substring(startIdx, endIdx)); }
}

// for devices it will act as a scrollable text area
void ScrollableTextArea::addLine(const String &text) {
    if (text.isEmpty()) {
        linesBuffer.emplace_back("");
        return;
    }

    String trText = bruce_i18n::tr(text);
    if (bruce_i18n::containsNonAscii(trText)) {
        String clean = trText;
        if (clean.endsWith("\r")) clean.remove(clean.length() - 1);
        std::vector<String> wrapped = bruce_i18n::utf8WrapToWidth(clean, _width, _fontSize);
        if (wrapped.empty()) {
            linesBuffer.emplace_back("");
        } else {
            for (size_t i = 0; i < wrapped.size(); ++i) {
                if (i > 0 && _indentWrappedLines) {
                    linesBuffer.emplace_back(" " + wrapped[i]);
                } else {
                    linesBuffer.emplace_back(wrapped[i]);
                }
            }
        }
        _redraw = true;
        return;
    }

    String buff;
    size_t start = 0;
    bool firstLine = true;

    // Automatically split into multiple lines
    while (start < text.length()) {
        size_t len = _maxCharactersPerLine;

        if (!firstLine && _indentWrappedLines) {
            buff = " " + text.substring(start, start + len - 1); // Reduce length for space
            start += len - 1;
        } else {
            buff = text.substring(start, start + len);
            start += len;
        }
        if (buff.endsWith("\r")) buff.remove(buff.length() - 1);

        linesBuffer.emplace_back(buff);
        firstLine = false;
    }

    _redraw = true;
}

void ScrollableTextArea::draw(bool force) {
    if (!_redraw && !force) return;

    uint16_t bg = bruceConfig.bgColor;
    uint16_t pri = bruceConfig.priColor;
    uint16_t bodyCol = blendColors(bg, uiTextBrightColor(), 220);
    bool hasScroll = linesBuffer.size() > _maxVisibleLines;
    int32_t textW = hasScroll ? max((int32_t)16, _width - 6) : _width;

    uint8_t _fSize = tft.getTextSize();
    uint8_t oldDatum = tft.getTextDatum();
    tft.setTextSize(_fontSize);
    tft.setTextDatum(TL_DATUM);

    auto drawRow = [&](const String &text, int32_t y) {
        bool isHeading = text.length() >= 3 && text[0] == '[' && text[text.length() - 1] == ']';
        _scrollBuffer.setTextColor(isHeading ? pri : bodyCol, bg);
        int32_t width = _scrollBuffer.drawString(text, _startX, y);
        if (width < textW) {
            _scrollBuffer.fillRect(_startX + width, y, textW - width, _pixelsPerLine, bg);
        }
    };

    uint16_t yOffset = 0;
    size_t lines = 0;
    size_t idx = firstVisibleLine;
    while (yOffset + _pixelsPerLine <= _height && lines < _maxVisibleLines && idx < linesBuffer.size()) {
        drawRow(linesBuffer[idx], _startY + yOffset);
        yOffset += _pixelsPerLine;
        lines++;
        idx++;
    }

    if (yOffset < _height) {
        _scrollBuffer.fillRect(_startX, _startY + yOffset, textW, _height - yOffset, bg);
    }

    if (hasScroll) {
        int32_t barX = _startX + _width - 3;
        int32_t trackY = _startY + 2;
        int32_t trackH = max((int32_t)8, _height - 4);
        _scrollBuffer.fillRect(barX, trackY, 2, trackH, uiBorderDimColor());
        int32_t thumbH = max((int32_t)8, (int32_t)(trackH * _maxVisibleLines / linesBuffer.size()));
        size_t maxFirst = linesBuffer.size() - _maxVisibleLines;
        int32_t thumbY = trackY + (maxFirst > 0 ? (int32_t)((trackH - thumbH) * firstVisibleLine / maxFirst) : 0);
        _scrollBuffer.fillRect(barX, thumbY, 2, thumbH, uiTextBrightColor());
    }

    lastVisibleLine = firstVisibleLine + lines;
    _scrollBuffer.setTextColor(pri, bg);
    tft.setTextSize(_fSize);
    tft.setTextDatum(oldDatum);

    _redraw = false;
}
