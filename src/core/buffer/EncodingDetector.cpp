// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 TextAnalyst-Qt contributors
// =============================================================================
// EncodingDetector.cpp: 编码识别实现
// 检测流程见 DISPLAYDESIGN.md §2.3.2（L1 BOM → L2 UTF-8 → L3 iconv → L4 locale）
// =============================================================================

#include "buffer/EncodingDetector.h"

#include <cstdint>
#include <cstring>

#include <cerrno>
#include <iconv.h>

namespace tat {

namespace {

// ---- L2：严格 UTF-8 校验（O(n) 单趟）----
// 校验规则：首字节分类 + 续字节范围检查（防过短编码/surrogate/超界）
bool isStrictUtf8(const char* b, size_t len, size_t* invalidOut) {
    size_t invalid = 0;
    size_t i = 0;
    while (i < len) {
        const uint8_t c = static_cast<uint8_t>(b[i]);
        if (c < 0x80) { ++i; continue; }
        size_t need;
        if (c >= 0xC2 && c <= 0xDF)      need = 1;
        else if (c >= 0xE0 && c <= 0xEF) need = 2;
        else if (c >= 0xF0 && c <= 0xF4) need = 3;
        else { ++invalid; ++i; continue; }  // 0x80-0xC1 / 0xF5-0xFF 非法首字节

        if (i + need >= len) { ++invalid; break; }  // 尾部不完整
        bool ok = true;
        for (size_t k = 1; k <= need && ok; ++k) {
            const uint8_t cc = static_cast<uint8_t>(b[i + k]);
            if (!(cc >= 0x80 && cc <= 0xBF)) ok = false;
        }
        if (ok) {  // 边界规则
            const uint8_t c1 = static_cast<uint8_t>(b[i + 1]);
            if (need == 2 && c == 0xE0 && c1 < 0xA0) ok = false;  // 过短编码
            if (need == 2 && c == 0xED && c1 >= 0xA0) ok = false;  // surrogate
            if (need == 3 && c == 0xF0 && c1 < 0x90) ok = false;   // > U+10FFFF
            if (need == 3 && c == 0xF4 && c1 > 0x8F) ok = false;
        }
        if (!ok) { ++invalid; ++i; continue; }
        i += need + 1;
    }
    *invalidOut = invalid;
    return invalid == 0;
}

// ---- L3：iconv 试转候选集（顺序敏感，GB18030 优先于 GBK）----
const char* kIconvCandidates[] = {
    "GB18030", "GBK", "CP936", "CP932", "CP950", "ISO-8859-1",
};

constexpr size_t kIconvCandidateCount = sizeof(kIconvCandidates) / sizeof(char*);

// 用 iconv 把样本从 srcName 转成 UTF-8；返回替换（非法序列）字节数。
// SIZE_MAX 表示该编码名不可用（iconv_open 失败）。
size_t iconvProbe(const char* src, size_t len, const char* srcName,
                  std::string* out) {
    iconv_t cd = iconv_open("UTF-8", srcName);
    if (cd == reinterpret_cast<iconv_t>(-1)) return SIZE_MAX;
    out->clear();
    out->reserve(len * 2 + 16);
    char* in = const_cast<char*>(src);
    size_t inLeft = len;
    size_t replaced = 0;
    char buf[4096];

    while (inLeft > 0) {
        char* op = buf;
        size_t opLeft = sizeof(buf);
        size_t rc = ::iconv(cd, &in, &inLeft, &op, &opLeft);
        out->append(buf, sizeof(buf) - opLeft);
        if (rc == static_cast<size_t>(-1)) {
            if (errno == E2BIG) continue;  // 输出缓冲满，继续
            if (errno == EILSEQ) {         // 非法序列：替换后前进 1 字节
                ++in; --inLeft;
                out->append("\xEF\xBF\xBD", 3);
                ++replaced;
                continue;
            }
            if (errno == EINVAL) {         // 尾部不完整序列
                out->append("\xEF\xBF\xBD", 3);
                ++replaced;
                break;
            }
            break;
        }
    }
    iconv_close(cd);
    return replaced;
}

// iconv 名称 → Encoding 枚举（仅 L3 候选集内的名称）
Encoding encodingFromIconvName(const char* name) {
    if (strcmp(name, "GB18030") == 0) return Encoding::GB18030;
    if (strcmp(name, "GBK") == 0 || strcmp(name, "CP936") == 0)
        return Encoding::Gbk;
    if (strcmp(name, "CP932") == 0) return Encoding::Cp932;
    if (strcmp(name, "CP950") == 0) return Encoding::Cp950;
    if (strcmp(name, "ISO-8859-1") == 0) return Encoding::Cp1252;
    return Encoding::Unknown;
}

}  // namespace

EncodingInfo EncodingDetector::tryBom(const char* b, size_t len) {
    EncodingInfo r;
    if (len >= 3 && (uint8_t)b[0] == 0xEF && (uint8_t)b[1] == 0xBB &&
        (uint8_t)b[2] == 0xBF) {
        r.encoding = Encoding::Utf8Bom; r.bomLen = 3; r.codecName = "UTF-8";
        r.confidence = 1.0;
    } else if (len >= 2 && (uint8_t)b[0] == 0xFF && (uint8_t)b[1] == 0xFE) {
        r.encoding = Encoding::Utf16LeBom; r.bomLen = 2; r.codecName = "UTF-16LE";
        r.confidence = 1.0;
    } else if (len >= 2 && (uint8_t)b[0] == 0xFE && (uint8_t)b[1] == 0xFF) {
        r.encoding = Encoding::Utf16BeBom; r.bomLen = 2; r.codecName = "UTF-16BE";
        r.confidence = 1.0;
    } else {
        r.encoding = Encoding::Unknown;  // 失败路径显式 Unknown（防御）
    }
    return r;  // Unknown → 继续下一级
}

EncodingInfo EncodingDetector::tryStrictUtf8(const char* b, size_t len) {
    EncodingInfo r;
    size_t invalid = 0;
    if (isStrictUtf8(b, len, &invalid)) {
        r.encoding = Encoding::Utf8;
        r.codecName = "UTF-8";
        r.confidence = 1.0;
    } else if (len > 0 && invalid * 100 < len / 4 + 1) {
        // 非法字节占比很低（约 <0.25%），仍判定 UTF-8，置信度随非法率下降
        r.encoding = Encoding::Utf8;
        r.codecName = "UTF-8";
        r.confidence = 1.0 - static_cast<double>(invalid) * 4.0 / len;
    }
    return r;
}

EncodingInfo EncodingDetector::tryIconv(const char* b, size_t len) {
    EncodingInfo best;
    const char* bestName = nullptr;
    size_t bestReplaced = SIZE_MAX;
    std::string out;

    for (const char* name : kIconvCandidates) {
        size_t replaced = iconvProbe(b, len, name, &out);
        if (replaced == SIZE_MAX) continue;  // 该编码名在本系统不可用
        if (replaced == 0) {                 // 无任何非法序列：直接采信
            best.encoding = encodingFromIconvName(name);
            best.codecName = name;
            best.confidence = 1.0;
            return best;
        }
        if (replaced < bestReplaced) {       // 记录替换最少的候选
            bestReplaced = replaced;
            bestName = name;
        }
    }

    if (bestName && len > 0) {
        double ratio = static_cast<double>(bestReplaced) / len;
        best.encoding = encodingFromIconvName(bestName);
        best.codecName = bestName;
        best.confidence = 1.0 - ratio * 4.0;  // 非法序列占比低 → 高置信
        if (best.confidence < 0.1) best.confidence = 0.1;
    }
    return best;  // Unknown → 继续下一级（locale 兜底）
}

EncodingInfo EncodingDetector::tryLocal8Bit(const char*, size_t) {
    EncodingInfo r;
    r.encoding = Encoding::Local8Bit;
    r.codecName = "LOCALE";
    r.confidence = 0.3;
    return r;
}

const char* EncodingDetector::iconvName(Encoding e) {
    switch (e) {
        case Encoding::Utf8:
        case Encoding::Utf8Bom:     return "UTF-8";
        case Encoding::Utf16Le:
        case Encoding::Utf16LeBom:  return "UTF-16LE";
        case Encoding::Utf16Be:
        case Encoding::Utf16BeBom:  return "UTF-16BE";
        case Encoding::Gbk:         return "GBK";
        case Encoding::GB2312:      return "GB2312";
        case Encoding::GB18030:     return "GB18030";
        case Encoding::Cp932:       return "CP932";
        case Encoding::Cp936:       return "CP936";
        case Encoding::Cp950:       return "CP950";
        case Encoding::Cp1252:      return "CP1252";
        case Encoding::Local8Bit:   return "";  // 空 → locale 默认
        default:                    return nullptr;
    }
}

size_t EncodingDetector::recommendSampleSize(size_t fileSize) {
    const size_t kMin = 64 * 1024;
    const size_t kMax = 4 * 1024 * 1024;
    if (fileSize <= kMin) return fileSize;
    if (fileSize >= kMax) return kMax;
    return fileSize;
}

EncodingInfo EncodingDetector::detectFast(const char* buffer, size_t len) {
    if (!buffer || len == 0) return EncodingInfo{};
    const size_t sample = recommendSampleSize(len);
    EncodingInfo r = tryBom(buffer, sample);
    if (r.encoding != Encoding::Unknown) return r;
    return tryStrictUtf8(buffer, sample);
}

EncodingInfo EncodingDetector::detect(const char* buffer, size_t len) {
    return detect(buffer, len, recommendSampleSize(len));
}

EncodingInfo EncodingDetector::detect(const char* buffer, size_t len,
                                      size_t sampleLimit) {
    if (!buffer || len == 0) return EncodingInfo{};
    size_t sample = sampleLimit > len ? len : sampleLimit;

    EncodingInfo r = tryBom(buffer, sample);
    if (r.encoding != Encoding::Unknown) return r;

    r = tryStrictUtf8(buffer, sample);
    if (r.encoding != Encoding::Unknown) return r;

    r = tryIconv(buffer, sample);
    if (r.encoding != Encoding::Unknown) return r;

    return tryLocal8Bit(buffer, sample);
}

int EncodingDetector::convert(const char* input, size_t inLen, Encoding from,
                              Encoding to, std::string* out, bool* replaced) {
    out->clear();
    *replaced = false;

    // UTF-8 → UTF-8：直通（渲染路径热点，不经过 iconv）
    if ((from == Encoding::Utf8 || from == Encoding::Utf8Bom) &&
        (to == Encoding::Utf8 || to == Encoding::Utf8Bom)) {
        out->assign(input, inLen);
        return 0;
    }

    const char* fromName = (from == Encoding::Local8Bit) ? "" : iconvName(from);
    if (iconvName(from) == nullptr) return -3;  // 不支持的源编码

    std::string tmp;
    size_t replacedCount = iconvProbe(input, inLen, fromName, &tmp);
    if (replacedCount == SIZE_MAX) return -3;
    out->swap(tmp);
    if (replacedCount > 0) *replaced = true;
    return replacedCount > 0 ? -1 : 0;
}

}  // namespace tat