// Host-side checks for the message board's ring storage.
//
// Build and run:
//   g++ -std=gnu++17 -I tests/shim -I src tests/board_test.cpp src/board.cpp
//       -o /tmp/board_test && /tmp/board_test
//
// The interesting behaviour is what happens at and after the wrap: the oldest
// post has to be the one that goes, ordering has to stay newest-first, and a
// reboot has to land the write cursor back in the right slot.

#include <cstdio>
#include <string>
#include <cstring>
#include <vector>

#include "board.h"

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

void addOrDie(const String &text, const String &link, const String &tags) {
  String error;
  if (!addBoardPost(text, link, tags, 1700000000u, nullptr, 0, error)) {
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
  if (!addBoardPost(text, String(""), String(""), 1700000000u, image.data(),
                    image.size(), error)) {
    ++failures;
    std::printf("  FAIL: image post rejected: %s\n", error.c_str());
  }
}

// Collects the board newest-first, exactly as the web layer pages through it.
std::vector<BoardPost> drain(const String &tag = String()) {
  std::vector<BoardPost> found;
  uint32_t cursor = 0;
  BoardPost item;
  while (readNextBoardPost(cursor, tag, item)) found.push_back(item);
  return found;
}

void testRejectsEmptyAndBadLinks() {
  std::printf("rejects empty posts and non-http links\n");
  String error;
  check(!addBoardPost(String(""), String(""), String(""), 0, nullptr, 0, error),
        "an empty post is refused");
  check(!addBoardPost(String("hi"), String("javascript:alert(1)"), String(""),
                      0, nullptr, 0, error),
        "a javascript: link is refused");
  check(!addBoardPost(String("hi"), String("data:text/html,x"), String(""), 0,
                      nullptr, 0, error),
        "a data: link is refused");
  check(addBoardPost(String("hi"), String("https://example.com/a.png"),
                     String(""), 0, nullptr, 0, error),
        "an https link is accepted");
}

void testNewestFirstOrdering() {
  std::printf("returns posts newest first\n");
  check(clearBoard(), "the board clears");
  for (int i = 1; i <= 5; ++i) addOrDie(post(i), String(""), String(""));

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
  for (int i = 1; i <= capacity; ++i) addOrDie(post(i), String(""), String(""));
  check(boardStoredCount() == capacity, "the board fills to capacity");

  // Ten more posts than the ring holds: the first ten must be gone, the count
  // must not have grown, and nothing in between may be lost.
  for (int i = capacity + 1; i <= capacity + 10; ++i) {
    addOrDie(post(i), String(""), String(""));
  }
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

  addOrDie(String("after reboot"), String(""), String(""));
  const auto found = drain();
  check(!found.empty() && found[0].text == String("after reboot"),
        "a post after the reboot lands at the front");
  check(!found.empty() && found[0].id == newestBefore + 1,
        "ids continue from where they left off");
}

void testTagFilter() {
  std::printf("filters by whole tag\n");
  check(clearBoard(), "the board clears");
  addOrDie(String("one"), String(""), String("skull altar"));
  addOrDie(String("two"), String(""), String("bonepile"));
  addOrDie(String("three"), String(""), String("bone"));

  check(drain(String("skull")).size() == 1, "one post is tagged skull");
  check(drain(String("bone")).size() == 1,
        "bone does not also match bonepile");
  check(drain(String("altar")).size() == 1, "one post is tagged altar");
  check(drain(String("nothing")).empty(), "an unused tag matches nothing");
  check(drain().size() == 3, "no filter returns everything");
}

void testTagNormalization() {
  std::printf("normalizes tags\n");
  check(clearBoard(), "the board clears");
  addOrDie(String("x"), String(""), String("  SKULL,, Altar   bone_pile  "));

  const auto found = drain();
  check(found.size() == 1, "the post stored");
  if (found.size() == 1) {
    check(found[0].tags == String("skull altar bone_pile"),
          "tags are lowercased, split and single spaced");
  }

  check(clearBoard(), "the board clears");
  addOrDie(String("y"), String(""),
           String("a b c d e f g h i j k l"));
  const auto capped = drain();
  if (capped.size() == 1) {
    int spaces = 0;
    for (size_t i = 0; i < capped[0].tags.length(); ++i) {
      if (capped[0].tags[i] == ' ') ++spaces;
    }
    check(spaces == 7, "no more than eight tags are kept");
  }
}

void testControlCharactersStripped() {
  std::printf("strips control characters from text\n");
  check(clearBoard(), "the board clears");
  addOrDie(String("line\x01one\nline\ttwo"), String(""), String(""));
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
  addImageOrDie(String("with a picture"), image);

  const auto found = drain();
  check(found.size() == 1, "the post stored");
  check(!found.empty() && found[0].hasImage, "the post reports a picture");

  std::vector<uint8_t> out(BOARD_MAX_IMAGE_BYTES);
  const size_t length =
      readBoardImage(found[0].id, out.data(), out.size());
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
  check(!addBoardPost(String("x"), String(""), String(""), 0, notJpeg.data(),
                      notJpeg.size(), error),
        "a file that is not JPEG is refused");

  const auto tooBig = fakeJpeg(BOARD_MAX_IMAGE_BYTES + 1, 0x11);
  check(!addBoardPost(String("x"), String(""), String(""), 0, tooBig.data(),
                      tooBig.size(), error),
        "a picture over the ceiling is refused");

  const auto atLimit = fakeJpeg(BOARD_MAX_IMAGE_BYTES, 0x22);
  check(addBoardPost(String("x"), String(""), String(""), 0, atLimit.data(),
                     atLimit.size(), error),
        "a picture exactly at the ceiling is accepted");

  // A picture on its own, with no words and no link, is still a post.
  check(addBoardPost(String(""), String(""), String(""), 0, atLimit.data(),
                     atLimit.size(), error),
        "a picture alone is a valid post");
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

  testRejectsEmptyAndBadLinks();
  testNewestFirstOrdering();
  testRingPrunesOldest();
  testCursorSurvivesReboot();
  testTagFilter();
  testTagNormalization();
  testControlCharactersStripped();
  testImageRoundTrip();
  testImageValidation();
  testImagesPruneBeforePosts();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
