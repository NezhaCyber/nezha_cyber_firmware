#include "Arduino.h"
#include "unity_lite.h"

#include <vector>

// Pure logic model matching ScrollableTextArea bounds & wrapping semantics
struct TextAreaModel {
    std::vector<String> linesBuffer;
    size_t firstVisibleLine = 0;
    size_t lastVisibleLine = 0;
    size_t maxVisibleLines = 6;
    uint16_t maxCharsPerLine = 20;
    bool indentWrappedLines = false;
    bool redraw = true;

    void scrollToLine(size_t lineNumber) {
        if (linesBuffer.empty()) return;
        size_t maxFirst = (linesBuffer.size() > maxVisibleLines) ? (linesBuffer.size() - maxVisibleLines) : 0;
        size_t target = (lineNumber > maxFirst) ? maxFirst : lineNumber;
        if (target != firstVisibleLine) {
            firstVisibleLine = target;
            redraw = true;
        }
    }

    String getLine(size_t lineNumber) const {
        if (linesBuffer.empty()) return "";
        return linesBuffer[(lineNumber >= linesBuffer.size()) ? (linesBuffer.size() - 1) : lineNumber];
    }

    void addWrappedAscii(const String &text) {
        if (text.isEmpty()) {
            linesBuffer.emplace_back("");
            return;
        }
        size_t start = 0;
        bool firstLine = true;
        while (start < text.length()) {
            size_t len = (maxCharsPerLine > 0) ? maxCharsPerLine : 1;
            String buff;
            if (!firstLine && indentWrappedLines) {
                size_t step = (len > 1) ? (len - 1) : 1;
                buff = String(" ") + text.substring(start, start + step);
                start += step;
            } else {
                buff = text.substring(start, start + len);
                start += len;
            }
            if (buff.endsWith("\r")) buff.remove(buff.length() - 1);
            linesBuffer.emplace_back(buff);
            firstLine = false;
        }
        redraw = true;
    }

    void clear() {
        firstVisibleLine = 0;
        lastVisibleLine = 0;
        linesBuffer.clear();
        linesBuffer.shrink_to_fit();
        redraw = true;
    }
};

TEST_CASE(get_line_handles_empty_and_oob) {
    TextAreaModel area;
    ASSERT_STREQ(area.getLine(0).c_str(), "");
    ASSERT_STREQ(area.getLine(999).c_str(), "");

    area.addWrappedAscii("line0");
    area.addWrappedAscii("line1");
    ASSERT_STREQ(area.getLine(0).c_str(), "line0");
    ASSERT_STREQ(area.getLine(1).c_str(), "line1");
    ASSERT_STREQ(area.getLine(2).c_str(), "line1");
    ASSERT_STREQ(area.getLine(10000).c_str(), "line1");
}

TEST_CASE(scroll_to_line_no_unsigned_underflow_when_fewer_lines_than_viewport) {
    TextAreaModel area;
    area.maxVisibleLines = 8;
    area.addWrappedAscii("only");
    area.addWrappedAscii("two");
    area.scrollToLine(5);
    ASSERT_EQ(area.firstVisibleLine, (size_t)0);
    area.scrollToLine(1000);
    ASSERT_EQ(area.firstVisibleLine, (size_t)0);
}

TEST_CASE(scroll_to_line_clamps_to_max_first) {
    TextAreaModel area;
    area.maxVisibleLines = 4;
    for (int i = 0; i < 10; ++i) area.addWrappedAscii("row");
    area.scrollToLine(3);
    ASSERT_EQ(area.firstVisibleLine, (size_t)3);
    area.scrollToLine(999);
    ASSERT_EQ(area.firstVisibleLine, (size_t)6); // 10 - 4
}

TEST_CASE(wrap_indent_with_width_one_does_not_hang) {
    TextAreaModel area;
    area.maxCharsPerLine = 1;
    area.indentWrappedLines = true;
    area.addWrappedAscii("ABC");
    ASSERT_EQ(area.linesBuffer.size(), (size_t)3);
    ASSERT_STREQ(area.linesBuffer[0].c_str(), "A");
    ASSERT_STREQ(area.linesBuffer[1].c_str(), " B");
    ASSERT_STREQ(area.linesBuffer[2].c_str(), " C");
}

TEST_CASE(clear_resets_state) {
    TextAreaModel area;
    for (int i = 0; i < 20; ++i) area.addWrappedAscii("hello\r");
    ASSERT_STREQ(area.getLine(0).c_str(), "hello");
    area.scrollToLine(10);
    area.clear();
    ASSERT_EQ(area.linesBuffer.size(), (size_t)0);
    ASSERT_EQ(area.firstVisibleLine, (size_t)0);
    ASSERT_EQ(area.lastVisibleLine, (size_t)0);
}

int main() { return nezha_test::runAll("ScrollableTextArea Bounds & Wrapping"); }
