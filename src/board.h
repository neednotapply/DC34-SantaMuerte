#pragma once

#include <Arduino.h>

// Anonymous booru-style message board, stored on LittleFS.
//
// Posts live in a fixed-size ring of slots inside one preallocated file. That
// choice does the pruning for us: the oldest post is simply the slot the next
// write lands on, so the board never grows, never fragments, and can never run
// the filesystem out of space mid-post. It also spreads erase wear evenly
// instead of hammering one region.
//
// Images live in a second, shorter ring. Pictures cost two hundred times what
// a line of text costs, so the board keeps a deep history of words and a
// shallow one of images: an old post keeps its text long after its picture has
// been overwritten by a newer one.

constexpr size_t BOARD_MAX_TEXT_LENGTH = 280;
constexpr size_t BOARD_MAX_LINK_LENGTH = 192;
constexpr size_t BOARD_MAX_TAGS_LENGTH = 96;

// Twelve kilobytes is about a 384-pixel JPEG at low quality. The badge never
// decodes or resizes an image: the posting browser scales it down first, and
// this is the ceiling it has to come in under.
constexpr size_t BOARD_MAX_IMAGE_BYTES = 12288;

constexpr uint16_t BOARD_NO_IMAGE = 0xFFFF;

// One post as handed to the web layer. Only ever one of these exists at a
// time; the board is never loaded into RAM as a whole.
struct BoardPost {
  uint32_t id;
  uint32_t createdAt;  // Client-supplied Unix seconds. 0 when not provided.
  String text;
  String link;
  String tags;  // Normalized, space separated, no surrounding spaces.
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

// Validates and appends a post. Pass a null image to post without one. The
// image must already be a JPEG within BOARD_MAX_IMAGE_BYTES; this does not
// transcode. Returns false with a human-readable reason in error.
bool addBoardPost(const String &text,
                  const String &link,
                  const String &tags,
                  uint32_t createdAt,
                  const uint8_t *image,
                  size_t imageLength,
                  String &error);

// Newest-first iteration. Pass 0 for beforeId to start at the newest post and
// the id of the last post you received to continue. tagFilter may be empty.
// Returns false once no further post matches.
bool readNextBoardPost(uint32_t &beforeId,
                       const String &tagFilter,
                       BoardPost &post);

// Copies a post's image into buffer. Returns the byte count, or 0 when the
// post has no image or its image has since been overwritten by a newer one.
size_t readBoardImage(uint32_t postId, uint8_t *buffer, size_t capacity);

// Empties every slot in both rings.
bool clearBoard();
