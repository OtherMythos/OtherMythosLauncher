#include "Test.h"

#include "SafePath.h"
#include "Sha256.h"
#include "TimeFormat.h"

using namespace OML;

TEST(safePathAcceptsNormalNames){
    CHECK(isSafeRelativePath("a"));
    CHECK(isSafeRelativePath("ExampleGame-Release-143694b.AppImage"));
    CHECK(isSafeRelativePath("20261007-194830-143694b/linux-Release/x.AppImage"));
    CHECK(isSafeRelativePath("av.app/Contents/MacOS/av"));
    CHECK(isSafeName("linux-Release"));
}

TEST(safePathRejectsEscapes){
    const char* bad[] = {"", ".", "..", "../a", "a/../b", "a/..", "/a", "a//b", "a/", "C:/x", "c:x", "a\\b", "a.", "a ", "a/b.", "x\x01y", "a*b", "a|b"};
    for(const char* p : bad){
        bool rejected = !isSafeRelativePath(p);
        OMLTest::recordCheck(rejected, std::string("rejects \"") + p + "\"", __FILE__, __LINE__);
    }
    CHECK(!isSafeName("a/b"));
}

TEST(sha256MatchesStandardVectors){
    CHECK_EQUAL(Sha256::hashString(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQUAL(Sha256::hashString("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQUAL(Sha256::hashString("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    //A million 'a's, fed in uneven chunks, exercises the partial-block path.
    Sha256 h;
    std::string chunk(997, 'a');
    size_t left = 1000000;
    while(left > 0){
        size_t n = left < chunk.size() ? left : chunk.size();
        h.update(chunk.data(), n);
        left -= n;
    }
    CHECK_EQUAL(h.finish(), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(timeFormatting){
    int64_t t = 0;
    CHECK(parseIsoUtc("2026-10-07T19:48:30Z", t));
    CHECK_EQUAL(isoTimeUtc(t), std::string("2026-10-07T19:48:30Z"));
    CHECK_EQUAL(compactTimeUtc(t), std::string("20261007-194830"));
    CHECK(!parseIsoUtc("yesterday", t));
    CHECK_EQUAL(formatSize(224926200), std::string("215 MB"));
    CHECK_EQUAL(formatSize(15710712), std::string("15.0 MB"));
    CHECK_EQUAL(formatSize(2048), std::string("2 KB"));
    CHECK_EQUAL(formatDuration(42), std::string("42 s"));
    CHECK_EQUAL(formatDuration(4800), std::string("1 h 20 min"));
    CHECK_EQUAL(formatAge(7200), std::string("2 h ago"));
}
