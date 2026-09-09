#pragma once

#include <Arduino.h>

// Anonymous message board, stored on LittleFS.
//
// A post is a drawing, a line of text, or both. Nothing else: no names, no
// links, no tags.
//
// Posts live in a fixed-size ring of slots inside one preallocated file. That
// choice does the pruning for us: the oldest post is simply the slot the next
// write lands on, so the board never grows, never fragments, and can never run
// the filesystem out of space mid-post. It also spreads erase wear evenly
// instead of hammering one region.
//
// Pictures live in a second, shorter ring. A picture costs two hundred times
// what a line of text costs, so the board keeps a deep history of words and a
// shallow one of images: an old post keeps its text long after its picture has
// been overwritten by a newer one.

constexpr size_t BOARD_MAX_TEXT_LENGTH = 280;

// Twelve kilobytes is about a 384-pixel JPEG at low quality. The badge never
// decodes or resizes an image: the posting browser draws or scales it down
// first, and this is the ceiling it has to come in under.
constexpr size_t BOARD_MAX_IMAGE_BYTES = 12288;

constexpr uint16_t BOARD_NO_IMAGE = 0xFFFF;

// Posts the NFC reader made on its own, rather than a browser. Deliberately
// below the browser pseudonym range so it can never collide with one, and
// non-zero so it is distinguishable from a legacy post.
constexpr uint16_t NFC_CAPTURE_AUTHOR_ID = 1;

// One post as handed to the web layer. Only ever one of these exists at a
// time; the board is never loaded into RAM as a whole.
struct BoardPost {
  uint32_t id;
  uint32_t createdAt;  // Client-supplied Unix seconds. 0 when not provided.
  uint16_t authorId; // Browser pseudonym 1000–9999, NFC_CAPTURE_AUTHOR_ID
                     // for a reader capture, zero for legacy posts.
  bool textInImage;
  String text;
  bool hasImage;
};

// Opens or creates both ring files and recovers their write cursors. Safe to
// call when LittleFS is unavailable; the board then reports itself not ready.
bool setupBoard();
bool isBoardReady();

// Number of slots currently holding a valid post, and the total each ring
// holds before its oldest entry is overwritten.
uint16_t boardStoredCount();
uint16_t boardCapacity();
uint16_t boardImageCapacity();
uint32_t boardNewestId();

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

// Empties every slot in both rings.
bool clearBoard();
