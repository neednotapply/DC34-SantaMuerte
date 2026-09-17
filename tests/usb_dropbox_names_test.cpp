// Host-side checks for the DROP BOX filename logic in src/usb_dropbox_names.*.
//
// Build and run:
//   g++ -std=gnu++17 -I tests/shim -I src tests/usb_dropbox_names_test.cpp
//       src/usb_dropbox_names.cpp -o /tmp/usb_dropbox_names_test
//       && /tmp/usb_dropbox_names_test
//
// A dropped file's name becomes a payload name, so these rules decide what the
// importer accepts and what it calls each script. They mirror usbHidValid-
// PayloadName(): letters, digits, '_', '-', '.' and space, <= 32 chars, not
// leading '.', at least one real character.

#include <cstdio>
#include <string>

#include "usb_dropbox_names.h"

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const std::string &what) {
  ++checks;
  if (condition) return;
  ++failures;
  std::printf("  FAIL: %s\n", what.c_str());
}

// Convenience: the derived name, or "<none>" when the file is not importable.
std::string named(const char *filename) {
  String out;
  if (!dropboxPayloadName(filename, out)) return "<none>";
  return out.c_str();
}
}  // namespace

int main() {
  // ---- Litter the importer must skip. ----
  check(!dropboxIsImportableFilename(""), "empty name is not importable");
  check(!dropboxIsImportableFilename("."), "'.' is not importable");
  check(!dropboxIsImportableFilename(".."), "'..' is not importable");
  check(!dropboxIsImportableFilename(".hidden"), "a dotfile is not importable");
  check(!dropboxIsImportableFilename("._payload.txt"),
        "an AppleDouble file is not importable");
  check(!dropboxIsImportableFilename("~$lock.txt"),
        "an Office lock file is not importable");
  check(!dropboxIsImportableFilename("System Volume Information"),
        "the Windows index folder is not importable");
  check(!dropboxIsImportableFilename("Thumbs.db"), "Thumbs.db is not importable");
  check(!dropboxIsImportableFilename("README.txt"),
        "our own seed readme is not re-imported");

  // ---- Files the importer accepts. ----
  check(dropboxIsImportableFilename("payload.txt"), "a plain .txt is importable");
  check(dropboxIsImportableFilename("no-extension"),
        "an extensionless file is importable");

  // ---- Extension stripping (only the known script extensions). ----
  check(named("hello.txt") == "hello", ".txt is stripped");
  check(named("hello.dd") == "hello", ".dd is stripped");
  check(named("hello.ducky") == "hello", ".ducky is stripped");
  check(named("payload.badusb") == "payload", ".badusb is stripped");
  check(named("HELLO.TXT") == "HELLO", "extension match is case-insensitive");
  check(named("payload.v2") == "payload.v2",
        "a non-script extension is preserved (dots are legal in names)");
  check(named("a.b.txt") == "a.b", "only the trailing extension is stripped");

  // ---- Sanitising to the allowed character set. ----
  check(named("my script (final).txt") == "my script _final_",
        "parentheses become underscores, spaces survive");
  check(named("rm -rf; reboot.txt") == "rm -rf_ reboot",
        "a semicolon becomes an underscore");
  check(named("cafe\xC3\xA9.txt").find("caf") == 0,
        "non-ASCII bytes are replaced, not passed through");

  // ---- Leading dots/spaces are trimmed; trailing spaces too. ----
  check(named("  spaced.txt") == "spaced", "leading spaces are dropped");
  check(named("...dots.txt") == "dots",
        "leading dots are dropped so the name is valid");
  check(named("trailing   .txt") == "trailing", "trailing spaces are trimmed");

  // ---- Length cap at 32 characters. ----
  check(named("0123456789012345678901234567890123456789.txt").size() == 32,
        "an over-long name is truncated to 32");

  // ---- Names with nothing usable are rejected. ----
  check(named("...") == "<none>", "a name that is only dots yields nothing");
  check(named("   .txt") == "<none>", "a name that is only spaces yields nothing");

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
