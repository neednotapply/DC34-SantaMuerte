// In-memory LittleFS stand-in for storage tests.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Arduino.h"

namespace fsshim {
inline std::map<std::string, std::vector<uint8_t>> &files() { static std::map<std::string, std::vector<uint8_t>> store; return store; }
inline std::set<std::string> &directories() { static std::set<std::string> store{"/"}; return store; }
inline size_t usedBytes() { size_t used = 0; for (const auto &item : files()) used += item.second.size(); return used; }
}

class File {
 public:
  File() = default;
  File(const std::string &path, bool truncate, bool directory = false)
      : path_(path), open_(true), directory_(directory) {
    if (truncate) fsshim::files()[path_].clear();
    if (directory_) {
      const std::string prefix = path_ == "/" ? "/" : path_ + "/";
      for (const auto &item : fsshim::files()) if (item.first.rfind(prefix, 0) == 0 && item.first.find('/', prefix.size()) == std::string::npos) entries_.push_back(item.first);
    }
  }
  explicit operator bool() const { return open_; }
  bool isDirectory() const { return open_ && directory_; }
  const char *name() const { return path_.c_str(); }
  File openNextFile() {
    if (!directory_ || entry_ >= entries_.size()) return File();
    return File(entries_[entry_++], false);
  }
  bool seek(uint32_t position) { if (!open_ || directory_) return false; position_ = position; return true; }
  size_t read(uint8_t *destination, size_t bytes) {
    if (!open_ || directory_) return 0;
    auto &data = fsshim::files()[path_]; if (position_ >= data.size()) return 0;
    const size_t available = std::min(bytes, data.size() - position_);
    std::memcpy(destination, data.data() + position_, available); position_ += available; return available;
  }
  size_t write(const uint8_t *source, size_t bytes) {
    if (!open_ || directory_) return 0;
    auto &data = fsshim::files()[path_]; if (position_ + bytes > data.size()) data.resize(position_ + bytes);
    std::memcpy(data.data() + position_, source, bytes); position_ += bytes; return bytes;
  }
  void flush() {}
  void close() { open_ = false; }
 private:
  std::string path_; bool open_ = false, directory_ = false; size_t position_ = 0, entry_ = 0; std::vector<std::string> entries_;
};

class LittleFSShim {
 public:
  bool exists(const char *path) { return fsshim::files().count(path) || fsshim::directories().count(path); }
  bool mkdir(const char *path) { return fsshim::directories().insert(path).second || fsshim::directories().count(path); }
  File open(const char *path, const char *mode = "r") {
    if (fsshim::directories().count(path)) return File(path, false, true);
    const bool truncate = mode[0] == 'w'; if (!truncate && !fsshim::files().count(path)) return File(); return File(path, truncate);
  }
  bool remove(const char *path) { return fsshim::files().erase(path) != 0; }
  size_t totalBytes() const { return 4 * 1024 * 1024; }
  size_t usedBytes() const { return fsshim::usedBytes(); }
};
inline LittleFSShim LittleFS;
