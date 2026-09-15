// Minimal host stand-ins for the Arduino APIs that board.cpp uses, so the
// board's ring, pruning and sanitizing can be tested with a normal compiler.
// This is not a general Arduino emulation and should not grow into one.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define F(x) (x)

class String {
 public:
  String() = default;
  String(const char *value) : value_(value ? value : "") {}
  String(const std::string &value) : value_(value) {}
  explicit String(int value) : value_(std::to_string(value)) {}
  explicit String(unsigned value) : value_(std::to_string(value)) {}
  explicit String(unsigned long value) : value_(std::to_string(value)) {}

  size_t length() const { return value_.size(); }
  const char *c_str() const { return value_.c_str(); }
  void reserve(size_t bytes) { value_.reserve(bytes); }

  char operator[](size_t index) const { return value_[index]; }

  String &operator+=(char character) {
    value_.push_back(character);
    return *this;
  }
  String &operator+=(const char *text) {
    value_ += text;
    return *this;
  }
  String &operator+=(const String &other) {
    value_ += other.value_;
    return *this;
  }

  void concat(const char *data, size_t bytes) { value_.append(data, bytes); }

  void trim() {
    const char *space = " \t\n\r\f\v";
    const size_t first = value_.find_first_not_of(space);
    if (first == std::string::npos) {
      value_.clear();
      return;
    }
    const size_t last = value_.find_last_not_of(space);
    value_ = value_.substr(first, last - first + 1);
  }

  void toLowerCase() {
    for (char &character : value_) {
      character = static_cast<char>(std::tolower(
          static_cast<unsigned char>(character)));
    }
  }

  void toUpperCase() {
    for (char &character : value_) {
      character = static_cast<char>(std::toupper(
          static_cast<unsigned char>(character)));
    }
  }

  long toInt() const { return std::strtol(value_.c_str(), nullptr, 10); }

  bool startsWith(const char *prefix) const {
    return value_.rfind(prefix, 0) == 0;
  }
  bool endsWith(const char *suffix) const {
    const size_t length = std::strlen(suffix);
    return value_.size() >= length &&
           value_.compare(value_.size() - length, length, suffix) == 0;
  }
  bool isEmpty() const { return value_.empty(); }

  int indexOf(const String &needle) const {
    const size_t at = value_.find(needle.value_);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  int indexOf(char needle) const {
    const size_t at = value_.find(needle);
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }

  String substring(size_t from) const { return String(value_.substr(from)); }
  String substring(size_t from, size_t to) const {
    return String(value_.substr(from, to - from));
  }

  bool operator==(const String &other) const { return value_ == other.value_; }

  const std::string &std_str() const { return value_; }

 private:
  std::string value_;
};

inline String operator+(const String &left, const String &right) {
  String joined(left);
  joined += right;
  return joined;
}

class SerialShim {
 public:
  void println(const char *text = "") { std::printf("%s\n", text); }
  void printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
  }
};

inline SerialShim Serial;

// usb_console.h renames Serial to this one; a host build needs something for
// the firmware's extern to bind to.
inline SerialShim UsbConsole;

using std::max;
using std::min;

// The drive stamps read activity with millis(); nothing under test reads the
// clock for timing, so a counter that only moves forward is enough.
inline uint32_t millis() {
  static uint32_t ticks = 0;
  return ++ticks;
}
