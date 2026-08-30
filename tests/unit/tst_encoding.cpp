// =============================================================================
// tst_encoding.cpp: 编码检测单元测试（DISPLAYDESIGN.md §2.3）
// 覆盖：BOM 四级、严格 UTF-8 校验、iconv 试转、convert 转码
// =============================================================================

#include <cstring>
#include <string>

#include "buffer/EncodingDetector.h"
#include "minitest.h"

using namespace tat;

TEST(bomUtf8) {
    const unsigned char d[] = {0xEF, 0xBB, 0xBF, 'h', 'i'};
    auto r = EncodingDetector::detectFast(reinterpret_cast<const char*>(d),
                                          sizeof(d));
    CHECK(r.encoding == Encoding::Utf8Bom);
    CHECK_EQ(r.bomLen, 3);
}

TEST(bomUtf16Le) {
    const unsigned char d[] = {0xFF, 0xFE, 0x41, 0x00};
    auto r = EncodingDetector::detectFast(reinterpret_cast<const char*>(d),
                                          sizeof(d));
    CHECK(r.encoding == Encoding::Utf16LeBom);
}

TEST(bomUtf16Be) {
    const unsigned char d[] = {0xFE, 0xFF, 0x00, 0x41};
    auto r = EncodingDetector::detectFast(reinterpret_cast<const char*>(d),
                                          sizeof(d));
    CHECK(r.encoding == Encoding::Utf16BeBom);
}

TEST(strictUtf8Ascii) {
    const char* d = "plain ascii 123";
    auto r = EncodingDetector::detectFast(d, strlen(d));
    CHECK(r.encoding == Encoding::Utf8);
    CHECK(r.confidence == 1.0);
}

TEST(strictUtf8Cjk) {
    const char* d = "你好，世界！";
    auto r = EncodingDetector::detectFast(d, strlen(d));
    CHECK(r.encoding == Encoding::Utf8);
}

TEST(strictUtf8Emoji) {  // 4 字节 UTF-8
    const char* d = "a\xF0\x9F\x98\x80z";
    auto r = EncodingDetector::detectFast(d, strlen(d));
    CHECK(r.encoding == Encoding::Utf8);
}

TEST(invalidUtf8Detected) {
    // 0xC4 0xE3：过短编码的续字节非法
    const unsigned char d[] = {0xC4, 0xE3, 0xBA, 0xC3};
    auto r = EncodingDetector::detectFast(reinterpret_cast<const char*>(d),
                                          sizeof(d));
    CHECK(r.encoding == Encoding::Unknown);
}

TEST(surrogateRejected) {
    // 0xED 0xA0 0x80：UTF-16 surrogate 编码，严格校验应拒绝
    const unsigned char d[] = {0xED, 0xA0, 0x80};
    auto r = EncodingDetector::detectFast(reinterpret_cast<const char*>(d),
                                          sizeof(d));
    CHECK(r.encoding == Encoding::Unknown);
}

TEST(gb18030ViaIconv) {
    // "你好" GB18030（兼 GBK）编码：C4E3 BAC3。
    // 注意：不能用 D6D0CEC4（"中文"）——它恰好全部落在合法 UTF-8 序列范围内，
    // L2 严格校验会先判 UTF-8（经典假阳性）。
    const unsigned char d[] = {0xC4, 0xE3, 0xBA, 0xC3};
    auto r = EncodingDetector::detect(reinterpret_cast<const char*>(d),
                                      sizeof(d));
    CHECK(r.encoding == Encoding::GB18030 || r.encoding == Encoding::Gbk);
    CHECK(r.confidence == 1.0);
}

TEST(localeFallback) {
    // 全 0x81 0x00 这类字节：UTF-8 非法、iconv 有替换 → 至少落到 Local8Bit
    const unsigned char d[] = {0x81, 0x82, 0x83};
    auto r = EncodingDetector::detect(reinterpret_cast<const char*>(d),
                                      sizeof(d));
    CHECK(r.encoding != Encoding::Unknown);  // Local8Bit 兜底
}

TEST(convertUtf8Passthrough) {
    std::string out;
    bool replaced = false;
    const char* in = "abc\344\275\240\345\245\275";  // abc你好
    int rc = EncodingDetector::convert(in, strlen(in), Encoding::Utf8,
                                       Encoding::Utf8, &out, &replaced);
    CHECK_EQ(rc, 0);
    CHECK(!replaced);
    CHECK_EQ(out, std::string(in));
}

TEST(convertGbkToUtf8) {
    // "中文" GBK → UTF-8（D6D0CEC4 → E4B8ADE69687）
    const unsigned char in[] = {0xD6, 0xD0, 0xCE, 0xC4};
    std::string out;
    bool replaced = false;
    int rc = EncodingDetector::convert(reinterpret_cast<const char*>(in),
                                       sizeof(in), Encoding::Gbk, Encoding::Utf8,
                                       &out, &replaced);
    CHECK_EQ(rc, 0);
    CHECK(!replaced);
    CHECK_EQ(out, "\xE4\xB8\xAD\xE6\x96\x87");
}

TEST(convertUtf16LeWithoutBom) {
    const unsigned char in[] = {0x41, 0x00, 0x42, 0x00};
    std::string out;
    bool replaced = false;
    int rc = EncodingDetector::convert(reinterpret_cast<const char*>(in),
                                       sizeof(in), Encoding::Utf16Le,
                                       Encoding::Utf8, &out, &replaced);
    CHECK_EQ(rc, 0);
    CHECK_EQ(out, "AB");
}

TEST(recommendSampleSize) {
    CHECK_EQ(EncodingDetector::recommendSampleSize(0), 0u);
    CHECK_EQ(EncodingDetector::recommendSampleSize(100), 100u);
    CHECK_EQ(EncodingDetector::recommendSampleSize(64u * 1024u), 64u * 1024u);
    CHECK_EQ(EncodingDetector::recommendSampleSize(1024u * 1024u), 1024u * 1024u);
    CHECK_EQ(EncodingDetector::recommendSampleSize(4u * 1024u * 1024u + 1u),
              4u * 1024u * 1024u);
    CHECK_EQ(EncodingDetector::recommendSampleSize(1024u * 1024u * 1024u),
              4u * 1024u * 1024u);
}

int main() { return minitest::runAll(); }