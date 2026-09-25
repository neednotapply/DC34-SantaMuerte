#pragma once

#include <Arduino.h>

// Anonymous message board, stored on LittleFS.
//
// A post is a drawing, a line of text, or both. Nothing else: no names, no
// links, no tags.
//
// Posts and pictures have their own small files. This deliberately gives
// scripts priority over the wall: the wall grows only while space is available
// and its oldest entries can be reclaimed when a script needs room. There is
// no count-based post limit.

constexpr size_t BOARD_MAX_TEXT_LENGTH = 280;

// Twelve kilobytes is about a 384-pixel JPEG at low quality. The badge never
// decodes or resizes an image: the posting browser draws or scales it down
// first, and this is the ceiling it has to come in under.
constexpr size_t BOARD_MAX_IMAGE_BYTES = 12288;

// One 12 KB scratch shared by every loop-task subsystem that handles a single
// board image: the web server (serving or receiving a drawing) and the USB
// console preview. Each consumes an image synchronously within one loop() pass
// and none can run while another is mid-image, so they share this instead of
// each reserving its own 12 KB -- three separate copies together starved the
// heap the Wi-Fi portal needs to accept a connection. The read-only USB drive
// keeps a SEPARATE cache: it is served from the USB task and can overlap a
// loop-task image op, so it must not share this buffer.
extern uint8_t boardImageShared[BOARD_MAX_IMAGE_BYTES];

constexpr uint16_t BOARD_NO_IMAGE = 0xFFFF;

// Posts the badge made on its own, rather than a browser. Deliberately below
// the browser pseudonym range so they can never collide with one, and non-zero
// so they are distinguishable from a legacy post. These name the transport an
// note arrived on; the board shows them as "(NFC)" and "(USB)".
constexpr uint16_t NFC_CAPTURE_AUTHOR_ID = 1;
constexpr uint16_t USB_CONSOLE_AUTHOR_ID = 2;

// A browser picks its own pseudonym inside this range and keeps it in
// localStorage. Reserved transport ids sit below it.
constexpr uint16_t BOARD_FIRST_BROWSER_AUTHOR_ID = 1000;
constexpr uint16_t BOARD_LAST_BROWSER_AUTHOR_ID = 9999;

// True for every id the board will store as-is. Anything else becomes 0, which
// renders as an unattributed note.
constexpr bool isStorableAuthorId(uint16_t id) {
  return id == NFC_CAPTURE_AUTHOR_ID || id == USB_CONSOLE_AUTHOR_ID ||
         (id >= BOARD_FIRST_BROWSER_AUTHOR_ID && id <= BOARD_LAST_BROWSER_AUTHOR_ID);
}

// One post as handed to the web layer. Only ever one of these exists at a
// time; the board is never loaded into RAM as a whole.
struct BoardPost {
  uint32_t id;
  uint32_t createdAt;  // Client-supplied Unix seconds. 0 when not provided.
  uint16_t authorId; // Browser pseudonym 1000–9999, NFC_CAPTURE_AUTHOR_ID or
                     // USB_CONSOLE_AUTHOR_ID for a post the badge made itself,
                     // zero for legacy posts.
  bool textInImage;
  String text;
  bool hasImage;
  uint32_t imageLength;
};

// Opens or creates both ring files and recovers their write cursors. Safe to
// call when LittleFS is unavailable; the board then reports itself not ready.
bool setupBoard();
bool isBoardReady();

// Number of posts currently stored. There is no fixed board capacity.
uint16_t boardStoredCount();
uint32_t boardNewestId();

// Releases oldest Field Notes until at least bytesNeeded bytes are free. This
// is used before saving scripts so scripts always take precedence over notes.
bool reclaimBoardStorage(size_t bytesNeeded);

// Validates and appends a post. Pass a null image to post text alone, or empty
// text to post a picture alone. The image must already be a JPEG within
// BOARD_MAX_IMAGE_BYTES; this does not transcode. Returns false with a
// human-readable reason in error.
bool addBoardPost(const String &text,
                  uint32_t createdAt,
                  const uint8_t *image,
                  size_t imageLength,
                  String &error,
                  uint16_t authorId = 0,
                  bool textInImage = false);

// Newest-first iteration. Pass 0 for beforeId to start at the newest post and
// the id of the last post you received to continue. Returns false once no
// further post remains.
bool readNextBoardPost(uint32_t &beforeId, BoardPost &post);

// Copies a post's image into buffer. Returns the byte count, or 0 when the
// post has no image or its image has since been overwritten by a newer one.
size_t readBoardImage(uint32_t postId, uint8_t *buffer, size_t capacity);

// Retires one post: its slot is zeroed and the image slot it owns is removed.
// Returns false when the board is unavailable or no live post carries that id.
// The number is never handed out again, so a deleted post cannot come back.
bool deleteBoardPost(uint32_t id);

// Empties every slot in both rings.
bool clearBoard();
