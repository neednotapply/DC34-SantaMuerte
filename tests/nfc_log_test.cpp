// Build: g++ -std=gnu++17 -I tests/shim -I src tests/nfc_log_test.cpp src/nfc_log.cpp src/board.cpp -o /tmp/nfc_log_test && /tmp/nfc_log_test
#include <cstdio>
#include <string>

#include "board.h"
#include "nfc_log.h"

uint32_t getPersistentBoardIdWatermark() { return 1; }
bool setPersistentBoardIdWatermark(uint32_t) { return true; }

namespace {
int failures = 0, checks = 0;
void check(bool condition, const char *message) { ++checks; if (!condition) { ++failures; std::printf("  FAIL: %s\n", message); } }
int countEntries() { uint32_t cursor = 0; NfcLogEntry entry; int count = 0; while (nfcLogReadNext(cursor, entry)) ++count; return count; }
}

int main() {
  if (!setupBoard() || !setupNfcLog()) return 1;
  check(clearNfcLog(), "clear log");
  for (int i = 0; i < 140; ++i) {
    const uint8_t uid[] = {static_cast<uint8_t>(i)};
    check(nfcLogRecord(uid, sizeof(uid), String("Type"), String("content")), "entry accepted");
  }
  check(countEntries() == 140, "entries continue past the old 128-entry cap");
  const uint8_t uid[] = {139};
  check(nfcLogRecord(uid, sizeof(uid), String("New type"), String("updated")), "duplicate accepted");
  check(countEntries() == 140, "duplicate refreshes rather than adding a row");
  check(setupNfcLog(), "reopen log");
  check(countEntries() == 140, "entries survive reboot recovery");
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
