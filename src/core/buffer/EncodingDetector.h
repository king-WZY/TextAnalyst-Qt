// =============================================================================
// EncodingDetector.h: 编码识别（纯函数，无状态，可并发调用）
// 文档：DISPLAYDESIGN.md §2.3（R-04：detectFast 用于索引构建）
// 线程安全：[A] 任意线程
// =============================================================================
#pragma once

#include <cstddef>
#include <string>

#include "models/EncodingInfo.h"

namespace tat {

class EncodingDetector {
public:
    // 完整四级检测：L1 BOM → L2 严格 UTF-8 → L3 iconv 试转 → L4 locale 兜底。
    // 仅在 UI 层提示"编码疑似错误"时使用；索引构建 MUST 用 detectFast。
    static EncodingInfo detect(const char* buffer, size_t len);
    static EncodingInfo detect(const char* buffer, size_t len, size_t sampleLimit);

    // 快速路径：仅 L1+L2（BOM + 严格 UTF-8 校验），O(sample)，SIMD 友好。
    // 索引构建阶段使用，避免大文件上的 iconv 试转开销。
    static EncodingInfo detectFast(const char* buffer, size_t len);

    // clamp(64 KiB, fileSize, 4 MiB)
    static size_t recommendSampleSize(size_t fileSize);

    // 将输入字节流转换为目标编码（渲染时使用）。
    // 返回 0 成功；-1 遇到非法序列（已替换为 U+FFFD 并继续，*replaced=true）；
    // -2 内存不足；-3 编码不受支持（out 为空）。
    static int convert(const char* input, size_t inLen, Encoding from, Encoding to,
                       std::string* out, bool* replaced);

private:
    static EncodingInfo tryBom(const char* b, size_t len);
    static EncodingInfo tryStrictUtf8(const char* b, size_t len);
    static EncodingInfo tryIconv(const char* b, size_t len);
    static EncodingInfo tryLocal8Bit(const char* b, size_t len);

    // 编码 → iconv 名称；未知编码返回 nullptr
    static const char* iconvName(Encoding e);
};

}  // namespace tat