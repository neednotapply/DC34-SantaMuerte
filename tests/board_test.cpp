// Host-side checks for the message board's ring storage.
//
// Build and run:
//   g++ -std=gnu++17 -I tests/shim -I src tests/board_test.cpp src/board.cpp
//       -o /tmp/board_test && /tmp/board_test
//
// The interesting behaviour is what happens at and after the wrap: the oldest
// entry has to be the one that goes, ordering has to stay newest-first, a
// reboot has to land the write cursor back in the right slot, and a picture
// whose slot was reclaimed must not be served to the post that lost it.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "board.h"

// board.cpp keeps its id high-water mark in NVS through badge_settings, which
// is not part of this host build. In-memory stubs stand in for it so the ring
// logic stays testable and the monotonic-id promise can be asserted directly.
uint32_t stubWatermark = 1;
uint32_t getPersistentBoardIdWatermark() { return stubWatermark; }
bool setPersistentBoardIdWatermark(uint32_t watermark) {
  if (watermark > stubWatermark) stubWatermark = watermark;
  return true;
}


namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &what) {
  ++checks;
  if (condition) return;
  ++failures;
  std::printf("  FAIL: %s\n", what.c_str());
}

String post(int index) {
  return String(("post " + std::to_string(index)).c_str());
}

void addOrDie(const String &text) {
  String error;
  if (!addBoardPost(text, 1700000000u, nullptr, 0, error)) {
    ++failures;
    std::printf("  FAIL: post rejected: %s\n", error.c_str());
  }
}

// A JPEG only has to start like one to be accepted; the badge never decodes it.
std::vector<uint8_t> fakeJpeg(size_t bytes, uint8_t fill) {
  std::vector<uint8_t> data(bytes, fill);
  data[0] = 0xFF;
  data[1] = 0xD8;
  data[2] = 0xFF;
  data[3] = 0xE0;
  return data;
}

void addImageOrDie(const String &text, const std::vector<uint8_t> &image) {
  String error;
  if (!addBoardPost(text, 1700000000u, image.data(), image.size(), error)) {
    ++failures;
    std::printf("  FAIL: image post rejected: %s\n", error.c_str());
  }
}

// Collects the board newest-first, exactly as the web layer pages through it.
std::vector<BoardPost> drain() {
  std::vector<BoardPost> found;
  uint32_t cursor = 0;
  BoardPost item;
  while (readNextBoardPost(cursor, item)) found.push_back(item);
  return found;
}

// The reserved transport ids are what makes the board show "(NFC)" and "(USB)"
// instead of a bare "Anonymous". They live below the browser range, and an
// earlier range check accepted only 1000-9999 -- so every reader capture was
// silently stored unattributed and the NFC tag never appeared once.
void testTransportAuthorIds() {
  std::printf("keeps the reserved transport ids and drops unknown ones\n");
  check(clearBoard(), "the board clears");
  String error;

  check(addBoardPost(String("reader capture"), 0, nullptr, 0, error, NFC_CAPTURE_AUTHOR_ID),
        "an NFC capture posts");
  check(addBoardPost(String("console note"), 0, nullptr, 0, error, USB_CONSOLE_AUTHOR_ID),
        "a USB note posts");
  check(addBoardPost(String("browser note"), 0, nullptr, 0, error, 4242),
        "a browser note posts");
  check(addBoardPost(String("bogus author"), 0, nullptr, 0, error, 500),
        "an id outside every known range still posts");

  const std::vector<BoardPost> found = drain();
  check(found.size() == 4, "all four stored");
  if (found.size() == 4) {
    check(found[3].authorId == NFC_CAPTURE_AUTHOR_ID, "the NFC id survives storage");
    check(found[2].authorId == USB_CONSOLE_AUTHOR_ID, "the USB id survives storage");
    check(found[1].authorId == 4242, "a browser pseudonym survives storage");
    check(found[0].authorId == 0, "an unknown id is stored unattributed");
  }
}

// A field note number is never reused. Clearing the wall, a reboot, or a
// LittleFS re-flash all leave the ring empty, and each used to restart from 1.
void testIdsNeverRepeat() {
  std::printf("field note numbers only ever count upward\n");
  check(clearBoard(), "the board clears");
  String error;

  addBoardPost(String("one"), 0, nullptr, 0, error);
  addBoardPost(String("two"), 0, nullptr, 0, error);
  const uint32_t beforeClear = boardNewestId();
  check(beforeClear >= 2, "two notes numbered");

  check(clearBoard(), "the board clears again");
  addBoardPost(String("after the clear"), 0, nullptr, 0, error);
  std::vector<BoardPost> found = drain();
  check(found.size() == 1, "one note after the clear");
  if (!found.empty()) {
    check(found[0].id > beforeClear,
          "a number handed out after a clear is higher than one before it");
  }

  // A reboot rebuilds state from the ring alone, which knows nothing of the
  // numbers already spent.
  const uint32_t beforeReboot = boardNewestId();
  check(clearBoard(), "the board clears once more");
  setupBoard();
  addBoardPost(String("after the reboot"), 0, nullptr, 0, error);
  found = drain();
  if (!found.empty()) {
    check(found[0].id > beforeReboot,
          "a number survives a wipe followed by a restart");
  }
}

void testWhatCountsAsAPost() {
  std::printf("accepts a drawing, text, or both, and nothing else\n");
  check(clearBoard(), "the board clears");
  String error;

  check(!addBoardPost(String(""), 0, nullptr, 0, error),
        "an empty post is refused");

  const auto drawing = fakeJpeg(1024, 0x33);
  check(addBoardPost(String("words alone"), 0, nullptr, 0, error),
        "text alone is a post");
  check(addBoardPost(String(""), 0, drawing.data(), drawing.size(), error),
        "a drawing alone is a post");
  check(addBoardPost(String("both"), 0, drawing.data(), drawing.size(), error),
        "a drawing with text is a post");

  const auto found = drain();
  check(found.size() == 3, "all three stored");
  if (found.size() == 3) {
    check(found[0].text == String("both") && found[0].hasImage,
          "the post with both keeps both");
    check(found[1].text.length() == 0 && found[1].hasImage,
          "the drawing-only post has no text");
    check(found[2].text == String("words alone") && !found[2].hasImage,
          "the text-only post has no picture");
  }
}

void testNewestFirstOrdering() {
  std::printf("returns posts newest first\n");
  check(clearBoard(), "the board clears");
  for (int i = 1; i <= 5; ++i) addOrDie(post(i));

  const auto found = drain();
  check(found.size() == 5, "all five posts come back");
  if (found.size() == 5) {
    check(found[0].text == post(5), "the newest post is first");
    check(found[4].text == post(1), "the oldest post is last");
    check(found[0].id > found[4].id, "ids descend through the page");
  }
}

void testRingPrunesOldest() {
  std::printf("overwrites the oldest post once full\n");
  check(clearBoard(), "the board clears");

  const uint16_t capacity = boardCapacity();
  for (int i = 1; i <= capacity; ++i) addOrDie(post(i));
  check(boardStoredCount() == capacity, "the board fills to capacity");

  // Ten more posts than the ring holds: the first ten must be gone, the count
  // must not have grown, and nothing in between may be lost.
  for (int i = capacity + 1; i <= capacity + 10; ++i) addOrDie(post(i));
  check(boardStoredCount() == capacity, "the board stays at capacity");

  const auto found = drain();
  check(found.size() == capacity, "a full board pages out every slot");
  if (found.size() == capacity) {
    check(found[0].text == post(capacity + 10), "the newest post survives");
    check(found[capacity - 1].text == post(11),
          "the eleventh post is now the oldest");
    bool descending = true;
    for (size_t i = 1; i < found.size(); ++i) {
      if (found[i].id >= found[i - 1].id) descending = false;
    }
    check(descending, "ids still descend across the wrap");
  }
}

void testCursorSurvivesReboot() {
  std::printf("recovers the write cursor after a reboot\n");
  const uint32_t newestBefore = boardNewestId();
  const uint16_t storedBefore = boardStoredCount();

  check(setupBoard(), "the board reopens");
  check(boardNewestId() == newestBefore, "the newest id is unchanged");
  check(boardStoredCount() == storedBefore, "the stored count is unchanged");

  addOrDie(String("after reboot"));
  const auto found = drain();
  check(!found.empty() && found[0].text == String("after reboot"),
        "a post after the reboot lands at the front");
  check(!found.empty() && found[0].id == newestBefore + 1,
        "ids continue from where they left off");
}

void testControlCharactersStripped() {
  std::printf("strips control characters from text\n");
  check(clearBoard(), "the board clears");
  addOrDie(String("line\x01one\nline\ttwo"));
  const auto found = drain();
  check(found.size() == 1, "the post stored");
  if (found.size() == 1) {
    check(found[0].text.indexOf('\x01') < 0, "the control byte is gone");
    check(found[0].text.indexOf('\n') >= 0, "the newline is kept");
  }
}

void testImageRoundTrip() {
  std::printf("stores and returns a picture\n");
  check(clearBoard(), "the board clears");

  const auto image = fakeJpeg(2048, 0x5A);
  addImageOrDie(String("with a drawing"), image);

  const auto found = drain();
  check(found.size() == 1, "the post stored");
  check(!found.empty() && found[0].hasImage, "the post reports a picture");

  std::vector<uint8_t> out(BOARD_MAX_IMAGE_BYTES);
  const size_t length = readBoardImage(found[0].id, out.data(), out.size());
  check(length == image.size(), "the picture comes back at its stored size");
  check(length == image.size() &&
            std::memcmp(out.data(), image.data(), length) == 0,
        "the picture comes back byte for byte");

  check(readBoardImage(found[0].id + 999, out.data(), out.size()) == 0,
        "an unknown post has no picture");
}

void testImageValidation() {
  std::printf("refuses pictures it cannot store\n");
  check(clearBoard(), "the board clears");
  String error;

  std::vector<uint8_t> notJpeg(64, 0x00);
  check(!addBoardPost(String("x"), 0, notJpeg.data(), notJpeg.size(), error),
        "a file that is not JPEG is refused");

  const auto tooBig = fakeJpeg(BOARD_MAX_IMAGE_BYTES + 1, 0x11);
  check(!addBoardPost(String("x"), 0, tooBig.data(), tooBig.size(), error),
        "a picture over the ceiling is refused");

  const auto atLimit = fakeJpeg(BOARD_MAX_IMAGE_BYTES, 0x22);
  check(addBoardPost(String("x"), 0, atLimit.data(), atLimit.size(), error),
        "a picture exactly at the ceiling is accepted");
}

// The point of the two rings: words outlive pictures.
void testImagesPruneBeforePosts() {
  std::printf("prunes pictures long before it prunes posts\n");
  check(clearBoard(), "the board clears");

  const uint16_t imageCapacity = boardImageCapacity();
  check(imageCapacity < boardCapacity(),
        "the image ring is shorter than the post ring");

  for (int i = 1; i <= imageCapacity; ++i) {
    addImageOrDie(post(i), fakeJpeg(512, static_cast<uint8_t>(i)));
  }

  auto found = drain();
  check(found.size() == imageCapacity, "every post stored");
  bool allHaveImages = true;
  for (const auto &item : found) {
    if (!item.hasImage) allHaveImages = false;
  }
  check(allHaveImages, "every post still has its picture");

  // Five more pictures must displace the five oldest, and only those.
  for (int i = imageCapacity + 1; i <= imageCapacity + 5; ++i) {
    addImageOrDie(post(i), fakeJpeg(512, static_cast<uint8_t>(i)));
  }

  found = drain();
  check(found.size() == imageCapacity + 5u, "no post was lost");

  int withImage = 0;
  for (const auto &item : found) {
    if (item.hasImage) ++withImage;
  }
  check(withImage == imageCapacity,
        "exactly the newest posts keep their pictures");

  const auto &oldest = found.back();
  check(!oldest.hasImage, "the oldest post lost its picture");
  check(oldest.text == post(1), "the oldest post kept its words");

  std::vector<uint8_t> out(BOARD_MAX_IMAGE_BYTES);
  check(readBoardImage(oldest.id, out.data(), out.size()) == 0,
        "a pruned picture is not served from its reused slot");

  // And the newest post's picture is its own, not a neighbour's.
  const size_t length = readBoardImage(found[0].id, out.data(), out.size());
  check(length == 512 &&
            out[4] == static_cast<uint8_t>(imageCapacity + 5),
        "the newest picture is the one that was stored for it");
}

}  // namespace

int main() {
  if (!setupBoard()) {
    std::printf("FAIL: the board would not initialize\n");
    return 1;
  }

  testWhatCountsAsAPost();
  testTransportAuthorIds();
  testIdsNeverRepeat();
  testNewestFirstOrdering();
  testRingPrunesOldest();
  testCursorSurvivesReboot();
  testControlCharactersStripped();
  testImageRoundTrip();
  testImageValidation();
  testImagesPruneBeforePosts();
  {
    String error;
    addBoardPost("device one", 1, nullptr, 0, error, 1234);
    addBoardPost("device two", 2, nullptr, 0, error, 5678);
    setupBoard();
    uint32_t cursor = 0;
    BoardPost post;
    if (!readNextBoardPost(cursor, post) || post.authorId != 5678) ++failures;
    if (!readNextBoardPost(cursor, post) || post.authorId != 1234) ++failures;
    checks += 2;
    auto ink = fakeJpeg(64, 0x31);
    addBoardPost("ink text", 3, ink.data(), ink.size(), error, 1234, true);
    setupBoard();
    cursor = 0;
    if (!readNextBoardPost(cursor, post) || post.authorId != 1234 || !post.textInImage || !post.hasImage) ++failures;
    ++checks;
  }

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
