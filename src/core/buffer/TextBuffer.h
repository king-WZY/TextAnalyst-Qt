// =============================================================================
// TextBuffer.h: 只读文本快照（DAL 与 BLL 的交接物）
// 文档：DISPLAYDESIGN.md §2.5（R-05 工厂签名 / R-06 空文件语义）
// 线程安全：
//   - create：[W] 工作线程；构造完成后内部数据不再变更
//   - 查询（textAt/meta/...）：[A] 任意线程无锁
//   - 发布：经 SharedSnapshot<const TextBuffer> 原子替换（见 §5.5）
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "models/EncodingInfo.h"
#include "models/Error.h"
#include "models/LineMeta.h"
#include "models/Task.h"
#include "models/common.h"

namespace tat {

class MemoryMappedFile;

class TextBuffer {
public:
    TextBuffer() = default;

    // 工厂（R-05）：成功返回非空 shared_ptr；失败返回 nullptr 并填充 *err。
    // 空文件也是合法结果：isValid()==true，rowCount()==0（R-06）。
    static std::shared_ptr<TextBuffer> create(const std::string& path, Error* err,
                                              EncodingInfo* enc,
                                              const TokenSnapshot* tok,
                                              Progress* progress);

    // 查询（无锁，可并发）
    bool            isValid()    const noexcept { return m_map != nullptr || m_size == 0; }
    int             rowCount()   const noexcept { return static_cast<int>(m_lines.size()); }
    size_t          totalBytes() const noexcept { return m_size; }
    const std::string& path()    const noexcept { return m_path; }
    const EncodingInfo& encoding() const noexcept { return m_encoding; }
    const LineMeta* meta(int i)  const noexcept;
    // textAt：零拷贝，直接指向 mmap。调用方不得在 TextBuffer 析构后持有。
    std::string_view textAt(int i) const noexcept;
    // toUtf8：仅当编码非 UTF-8 家族时转码；渲染层另有 LRU 缓存（§7.3.3）。
    std::string toUtf8(int i) const;
    const char* basePtr() const noexcept { return m_base; }
    uint64_t    mtimeNs()   const noexcept { return m_mtimeNs; }

    // 实验特性：受控卸载（--compact-memory，默认关闭，见 §2.2.4）。
    // v1.0 恒返回 true（未启用 MADV_DONTNEED）。
    bool ensureLoaded(size_t /*offset*/, size_t /*length*/) noexcept { return true; }

    struct MemoryFootprint {
        size_t mapped    = 0;  // mmap 大小（≈文件大小）
        size_t indexMeta = 0;  // LineMeta 数组
        size_t resident  = 0;  // /proc/self/smaps_rollup 的 Rss
    };
    MemoryFootprint footprint() const;

private:
    std::vector<LineMeta>          m_lines;
    const char*                    m_base   = nullptr;
    size_t                         m_size   = 0;
    std::string                    m_path;
    EncodingInfo                   m_encoding;
    uint64_t                       m_mtimeNs = 0;
    std::shared_ptr<MemoryMappedFile> m_map;  // 保活 mmap
};

}  // namespace tat