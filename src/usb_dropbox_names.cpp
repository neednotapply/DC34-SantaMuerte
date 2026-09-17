#include "usb_dropbox_names.h"

#include <cctype>
#include <cstring>
#include <strings.h>  // strcasecmp

namespace {
constexpr size_t MAX_NAME = 32;  // == USB_HID/BADUSB_MAX_NAME_LENGTH

bool endsWithNoCase(const char *text, size_t length, const char *suffix) {
  const size_t suffixLength = strlen(suffix);
  if (suffixLength >= length) return false;  // needs a stem before the dot
  const char *tail = text + length - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i) {
    if (tolower(static_cast<unsigned char>(tail[i])) !=
        tolower(static_cast<unsigned char>(suffix[i])))
      return false;
  }
  return true;
}

// A character a payload name is allowed to carry.
bool allowedNameChar(char c) {
  return isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
         c == '.' || c == ' ';
}
}  // namespace

bool dropboxIsImportableFilename(const char *filename) {
  if (!filename || !filename[0]) return false;
  // "." and ".." and every dotfile: a leading dot is both an invalid payload
  // name and the shape of macOS' "._name" resource forks and other hidden
  // litter. usbHidValidPayloadName rejects a leading dot too, so this is the
  // same boundary drawn one step earlier.
  if (filename[0] == '.') return false;
  // Windows lock and system artifacts.
  if (filename[0] == '~' && filename[1] == '$') return false;
  if (strcasecmp(filename, "System Volume Information") == 0) return false;
  if (strcasecmp(filename, "Thumbs.db") == 0) return false;
  if (strcasecmp(filename, "desktop.ini") == 0) return false;
  if (strcasecmp(filename, "autorun.inf") == 0) return false;
  if (strcasecmp(filename, "readme.txt") == 0) return false;  // our own seed
  return true;
}

bool dropboxPayloadName(const char *filename, String &out) {
  if (!filename) return false;
  // Basename only; a desktop never hands us a path here, but be defensive.
  const char *base = filename;
  for (const char *p = filename; *p; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;

  size_t length = strlen(base);
  // Strip one trailing script extension. Only these known ones, so a dotted
  // stem that is not an extension ("payload.v2") is preserved intact.
  static const char *kExtensions[] = {".badusb", ".ducky", ".txt", ".dd"};
  for (const char *extension : kExtensions) {
    if (endsWithNoCase(base, length, extension)) {
      length -= strlen(extension);
      break;
    }
  }

  char buffer[MAX_NAME + 1] = {};
  size_t out_length = 0;
  bool sawReal = false;
  for (size_t i = 0; i < length && out_length < MAX_NAME; ++i) {
    char c = base[i];
    if (!allowedNameChar(c)) c = '_';  // forgive parentheses, commas, etc.
    // A leading '.' or ' ' is not allowed to open the name; drop them until a
    // real character has been placed.
    if (out_length == 0 && (c == '.' || c == ' ')) continue;
    buffer[out_length++] = c;
    if (c != '.' && c != ' ') sawReal = true;
  }
  // Trim trailing spaces, which read as accidental padding.
  while (out_length > 0 && buffer[out_length - 1] == ' ') buffer[--out_length] = '\0';
  if (!sawReal || out_length == 0) return false;
  out = buffer;
  return true;
}
