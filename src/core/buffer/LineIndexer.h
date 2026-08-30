// =============================================================================
// LineIndexer.h: 后台线程偏移量索引（"延迟解析 + 偏移量索引"策略）
// 文档：ARCHITECTURE.md §3.2；DISPLAYDESIGN.md §2.4
// 线程安全：[W] 工作线程调用；输入只读，输出写入调用方独占的 vector
// =============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "models/EncodingInfo.h"
#include "models/Error.h"
#include "models/LineMeta.h"
#include "models/Task.h"

namespace tat {

struct IndexStats {
    int      rowCount     = 0;
    size_t   maxLength    = 0;        // 最长行字节数
    LineEnd  dominantEnd  = LineEnd::LF;
    uint64_t totalBytes   = 0;
    double   elapsedMs    = 0;
    bool     hitOffsetLimit = false;  // 触发 > 4 GiB 降级
};

class LineIndexer {
public:
    // 单次调用。data 必须覆盖 bomLen 之后的全文；offset 相对"跳过 BOM 后"的基址。
    // 进度与取消可选（tok 为 nullptr 则不做检查）。
    static Error build(const char* data, size_t size, const EncodingInfo& enc,
                       std::vector<LineMeta>* out, IndexStats* stats,
                       const TokenSnapshot* tok, Progress* progress);

    // 预估行数（用于进度条分母与 reserve），基于前 1 MiB 的换行符密度。
    static int estimateRows(const char* data, size_t size);
};

}  // namespace tat