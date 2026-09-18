// Host-side checks for Field Notes' storage-priority policy.
// Build: g++ -std=gnu++17 -I tests/shim -I src tests/board_test.cpp src/board.cpp -o /tmp/board_test && /tmp/board_test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "board.h"

uint32_t stubWatermark = 1;
uint32_t getPersistentBoardIdWatermark() { return stubWatermark; }
bool setPersistentBoardIdWatermark(uint32_t watermark) { if (watermark > stubWatermark) stubWatermark = watermark; return true; }
bool reclaimNfcLogStorage(size_t) { return true; }

namespace {
int failures = 0, checks = 0;
void check(bool condition, const std::string &what) { ++checks; if (!condition) { ++failures; std::printf("  FAIL: %s\n", what.c_str()); } }
std::vector<uint8_t> fakeJpeg(size_t bytes, uint8_t fill) { std::vector<uint8_t> result(bytes, fill); result[0] = 0xFF; result[1] = 0xD8; result[2] = 0xFF; result[3] = 0xE0; return result; }
void addOrDie(const String &text) { String error; check(addBoardPost(text, 1700000000u, nullptr, 0, error), "post accepted"); }
std::vector<BoardPost> drain() { std::vector<BoardPost> posts; uint32_t cursor = 0; BoardPost post; while (readNextBoardPost(cursor, post)) posts.push_back(post); return posts; }

void testNoCountCap() {
  std::printf("stores past the former fixed post cap\n");
  check(clearBoard(), "clear board");
  for (int i = 0; i < 600; ++i) addOrDie(String(("note " + std::to_string(i)).c_str()));
  const auto posts = drain();
  check(posts.size() == 600, "all 600 notes remain");
  check(!posts.empty() && posts.front().text == String("note 599"), "newest note remains first");
  check(posts.size() == 600 && posts.back().text == String("note 0"), "oldest note remains until storage is needed");
}
void testImagesDoNotHaveOwnCap() {
  std::printf("does not evict drawings by count\n");
  check(clearBoard(), "clear board");
  const auto image = fakeJpeg(512, 0x5A); String error;
  for (int i = 0; i < 90; ++i) check(addBoardPost(String("ink"), 0, image.data(), image.size(), error), "image post accepted");
  const auto posts = drain();
  int images = 0; for (const auto &post : posts) if (post.hasImage) ++images;
  check(images == 90, "all drawings remain");
}
void testExplicitReclaimOldestFirst() {
  std::printf("reclaims oldest notes only when storage is requested\n");
  check(clearBoard(), "clear board"); addOrDie(String("first")); addOrDie(String("second"));
  check(deleteBoardPost(drain().back().id), "oldest note can be retired");
  const auto posts = drain();
  check(posts.size() == 1 && posts[0].text == String("second"), "newest note survives retirement");
}
void testIdsAndImages() {
  std::printf("keeps monotonic ids and image bytes\n");
  check(clearBoard(), "clear board"); String error; const auto image = fakeJpeg(1024, 0x31);
  check(addBoardPost(String("reader"), 0, image.data(), image.size(), error, NFC_CAPTURE_AUTHOR_ID), "NFC note accepted");
  const auto posts = drain(); check(posts.size() == 1 && posts[0].authorId == NFC_CAPTURE_AUTHOR_ID && posts[0].hasImage, "metadata survives");
  std::vector<uint8_t> out(BOARD_MAX_IMAGE_BYTES); check(readBoardImage(posts[0].id, out.data(), out.size()) == image.size() && !std::memcmp(out.data(), image.data(), image.size()), "image returns intact");
  const uint32_t before = posts[0].id; check(setupBoard(), "reopen board"); addOrDie(String("after reboot")); check(boardNewestId() > before, "ids remain monotonic");
}
}  // namespace

int main() {
  if (!setupBoard()) { std::printf("FAIL: board did not initialize\n"); return 1; }
  testNoCountCap(); testImagesDoNotHaveOwnCap(); testExplicitReclaimOldestFirst(); testIdsAndImages();
  std::printf("\n%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
