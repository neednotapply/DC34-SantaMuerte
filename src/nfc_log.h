#pragma once

#include <Arduino.h>

// The unified NFC log: one place that records every tag the reader meets --
// its identity (UID and card type) together with whatever content came off it.
// This is where NFC reads land now; they no longer post to the Field Notes
// board, which is for people again. Re-seeing the same UID does not add a row,
// it bumps that tag's hit count and freshens what was read, so the log reads
// as an encounter journal rather than a scroll of duplicates.
//
// Storage mirrors board.cpp: a fixed, preallocated LittleFS ring so it never
// grows the filesystem and the oldest tag is simply the slot the next new one
// overwrites. Only the Arduino loop task writes it; the reader task stages
// sightings through a queue, exactly as capture already does.

constexpr size_t NFC_LOG_CONTENT_MAX = 208;
constexpr size_t NFC_LOG_TYPE_MAX = 40;

struct NfcLogEntry {
  String uid;       // "04:69:CA:..." formatted
  String tagType;   // "MIFARE Classic", "NFC Forum Type 2 (NTAG / Ultralight)"…
  String content;   // decoded NDEF text/URL, a Classic summary, or empty
  uint32_t hitCount;
  uint32_t lastSeenId;
};

// Opens or creates the ring and recovers its cursor. Safe to call when
// LittleFS is unavailable; the board then reports itself not ready.
bool setupNfcLog();
bool isNfcLogReady();

// How many tags are stored, and the total the ring holds before its oldest
// entry is overwritten.
uint16_t nfcLogStoredCount();
uint16_t nfcLogCapacity();
uint32_t nfcLogNewestId();

// Records one sighting. De-dups by UID: a UID already present has its hit count
// raised and its content/type refreshed and is moved to newest; a new UID
// takes the next ring slot. Returns false only on a storage error.
bool nfcLogRecord(const uint8_t *uid, uint8_t uidLength, const String &tagType,
                  const String &content);

// Newest-first iteration. Pass 0 for beforeId to start at the most recently
// seen tag and the lastSeenId of the entry you received to continue. Returns
// false once no further entry remains.
bool nfcLogReadNext(uint32_t &beforeId, NfcLogEntry &entry);

// Retires one tag by the lastSeenId the web layer reports. Returns false when
// the log is unavailable or no live entry carries that id. Seeing the same UID
// again afterwards records it afresh, with its hit count back at one.
bool deleteNfcLogEntry(uint32_t lastSeenId);

// Empties the board.
bool clearNfcLog();
