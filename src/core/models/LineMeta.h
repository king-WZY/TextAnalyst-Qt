// =============================================================================
// LineMeta.h: 行元数据（8 字节紧凑表示）
// 文档：DISPLAYDESIGN.md §1.3（R-03：不含 valid()/hash）
// 线程安全：值类型
// =============================================================================
#pragma once

#include <cstdint>
#include <cstddef>

#include "models/common.h"

namespace tat {

// 8 字节紧凑表示。1 亿行 = 800 MB。
// 不变式：offset + length <= mappedSize；length 不含行结束符。
// 空行（length == 0）是合法行（如 "\n\n"），用 empty() 判断。
// 索引器保证 offset/length 在界内，因此不提供 valid()（R-03）。
struct alignas(8) LineMeta {
    Offset offset = 0;
    Length length = 0;

    bool   empty() const noexcept { return length == 0; }
    size_t paddedSize() const noexcept { return sizeof(LineMeta); }
};

static_assert(sizeof(LineMeta) == 8, "LineMeta must stay cache-line friendly");

// 分段索引用：offset 提升到 64 位，覆盖 > 4 GiB 单段（§2.4.4，v1.1 完整实现）。
struct alignas(8) SegmentMeta {
    uint64_t offset = 0;
    Length   length = 0;
};

static_assert(sizeof(SegmentMeta) == 16, "SegmentMeta layout");

// 行结束类型（索引统计用）
enum class LineEnd : uint8_t { LF, CRLF, CR, LastNoEol };

}  // namespace tat