// =============================================================================
// EncodingInfo.h: 编码识别结果
// 文档：DISPLAYDESIGN.md §1.8 / §2.3
// 线程安全：值类型
// =============================================================================
#pragma once

#include <cstdint>
#include <string>

#include "models/common.h"

namespace tat {

enum class Encoding : uint8_t {
    Unknown,
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16LeBom,
    Utf16Be,
    Utf16BeBom,
    Gbk,
    GB2312,
    GB18030,
    Cp932,
    Cp936,
    Cp950,
    Cp1252,
    Local8Bit,  // 由 LANG/LC_ALL 决定的单字节编码
};

// EncodingDetector 的产物。TextBuffer 持有；渲染时按需转码（§7.3）。
struct EncodingInfo {
    // 默认 Unknown 是安全默认：任何"未检测/检测失败"的路径都必须显式得到
    // 非 Unknown 结果才算成功（曾因默认 Utf8 导致 tryBom 失败被误判 UTF-8）。
    Encoding encoding = Encoding::Unknown;
    int      bomLen   = 0;            // BOM 字节数，索引时跳过
    std::string codecName = "UTF-8";  // iconv 使用的名称，如 "GB18030"
    double   confidence = 1.0;        // 0.0 - 1.0

    bool isUtf8ByteStream() const noexcept {
        return encoding == Encoding::Utf8 || encoding == Encoding::Utf8Bom;
    }
};

}  // namespace tat