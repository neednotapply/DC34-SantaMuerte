// An in-memory stand-in for the LittleFS file API that board.cpp uses. Files
// live in a map for the life of the test process, which lets a test simulate a
// reboot by closing everything and calling setupBoard() again.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"

namespace fsshim {

inline std::map<std::string, std::vector<uint8_t>> &files() {
  static std::map<std::string, std::vector<uint8_t>> store;
  return store;
}

}  // namespace fsshim

class File {
 public:
  File() = default;
  File(const std::string &path, bool truncate) : path_(path), open_(true) {
    if (truncate) fsshim::files()[path_].clear();
  }

  explicit operator bool() const { return open_; }

  bool seek(uint32_t position) {
    if (!open_) return false;
    position_ = position;
    return true;
  }

  size_t read(uint8_t *destination, size_t bytes) {
    if (!open_) return 0;
    auto &data = fsshim::files()[path_];
    if (position_ >= data.size()) return 0;
    const size_t available = std::min(bytes, data.size() - position_);
    std::memcpy(destination, data.data() + position_, available);
    position_ += available;
    return available;
  }

  size_t write(const uint8_t *source, size_t bytes) {
    if (!open_) return 0;
    auto &data = fsshim::files()[path_];
    if (position_ + bytes > data.size()) data.resize(position_ + bytes);
    std::memcpy(data.data() + position_, source, bytes);
    position_ += bytes;
    return bytes;
  }

  void flush() {}
  void close() { open_ = false; }

 private:
  std::string path_;
  bool open_ = false;
  size_t position_ = 0;
};

class LittleFSShim {
 public:
  bool exists(const char *path) {
    return fsshim::files().count(path) != 0;
  }

  File open(const char *path, const char *mode) {
    const bool truncate = mode[0] == 'w';
    if (!truncate && !exists(path)) return File();
    return File(path, truncate);
  }

  bool remove(const char *path) {
    return fsshim::files().erase(path) != 0;
  }
};

inline LittleFSShim LittleFS;
