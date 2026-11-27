#ifndef NEZHA_MOCK_ARDUINO_H
#define NEZHA_MOCK_ARDUINO_H

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef PROGMEM
#define PROGMEM
#endif

inline uint8_t pgm_read_byte(const void *addr) { return *static_cast<const uint8_t *>(addr); }
inline uint16_t pgm_read_word(const void *addr) { return *static_cast<const uint16_t *>(addr); }

// Minimal host-compatible Arduino String subset for compiling firmware headers/logic natively
class String {
public:
    String() = default;
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}

    size_t length() const { return _s.size(); }
    bool isEmpty() const { return _s.empty(); }
    const char *c_str() const { return _s.c_str(); }
    char operator[](size_t i) const { return i < _s.size() ? _s[i] : '\0'; }

    int indexOf(char c, size_t from = 0) const {
        auto pos = _s.find(c, from);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        auto pos = _s.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    String substring(size_t start) const {
        return start < _s.size() ? String(_s.substr(start)) : String("");
    }
    String substring(size_t start, size_t end) const {
        if (start >= _s.size() || end <= start) return String("");
        return String(_s.substr(start, end - start));
    }
    bool startsWith(const String &prefix) const { return _s.rfind(prefix._s, 0) == 0; }
    bool endsWith(const String &suffix) const {
        return _s.size() >= suffix._s.size() &&
               _s.compare(_s.size() - suffix._s.size(), suffix._s.size(), suffix._s) == 0;
    }
    void remove(size_t idx, size_t count = 1) {
        if (idx < _s.size()) _s.erase(idx, count);
    }
    String &operator+=(const String &rhs) {
        _s += rhs._s;
        return *this;
    }
    String &operator+=(char c) {
        _s.push_back(c);
        return *this;
    }
    friend String operator+(const String &a, const String &b) { return String(a._s + b._s); }
    bool operator==(const String &o) const { return _s == o._s; }
    bool operator==(const char *o) const { return _s == (o ? o : ""); }
    bool operator!=(const String &o) const { return _s != o._s; }

private:
    std::string _s;
};

#endif // NEZHA_MOCK_ARDUINO_H
