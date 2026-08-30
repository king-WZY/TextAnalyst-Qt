// =============================================================================
// MemoryMappedFile.h: POSIX mmap 内存映射文件（DAL 层）
// 文档：ARCHITECTURE.md §3.1（v1.0.1 修订）；DISPLAYDESIGN.md §2.2
// 线程安全：
//   - 对象生命周期由 shared_ptr 管理；data() 指针在析构前稳定
//   - [A] 任意线程可调用 const 查询
// 约束：只使用 POSIX API（sys/mman.h、fcntl.h、sys/stat.h），禁止 QFile::map
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "models/Error.h"
#include "models/common.h"

namespace tat {

enum class MemoryMapFlags : uint8_t { ReadOnly = 0, ReadWrite = 1 };

class MemoryMappedFile {
public:
    MemoryMappedFile() = default;
    ~MemoryMappedFile();
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;
    MemoryMappedFile(MemoryMappedFile&& o) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& o) noexcept;

    // 打开并映射。失败时 isValid()==false 且 *error 描述原因。
    // 空文件也是合法结果：isValid()==true，data()==nullptr，size()==0。
    static MemoryMappedFile open(const std::string& path, MemoryMapFlags flags,
                                 Error* error);

    // 创建/截断文件并映射（导出模式，§6.2 exportLines 用）。
    static MemoryMappedFile createOrTruncate(const std::string& path,
                                             size_t preAllocBytes, Error* error);

    bool        isValid() const noexcept { return fd_ >= 0; }
    const char* data() const noexcept { return static_cast<const char*>(data_); }
    size_t      size() const noexcept { return size_; }
    uint64_t    mtimeNs() const noexcept { return mtimeNs_; }
    bool        isLocked() const noexcept { return locked_; }
    bool        isWritable() const noexcept { return writable_; }

    // 内核访问模式提示。返回 false 表示 madvise 不支持该 flag，不视为错误。
    bool adviseRandom() noexcept;      // MADV_RANDOM
    bool adviseNormal() noexcept;      // MADV_NORMAL
    bool adviseSequential() noexcept;  // MADV_SEQUENTIAL
    // 实验特性（--compact-memory），默认禁用：见 DISPLAYDESIGN §2.2.4
    bool adviseDontNeed(const char* range, size_t len) noexcept;

    // 外部变更检测（inotify 触发后由主线程调用，非阻塞）
    uint64_t statMtimeNs() const noexcept;
    bool     sizeChanged() const noexcept;  // 与加载时 size_ 比较

    // 显式关闭（幂等）。析构也会调用。
    void close() noexcept;

private:
    void closeNoThrow();

    void*    data_     = nullptr;
    size_t   size_     = 0;
    int      fd_       = -1;
    bool     writable_ = false;
    bool     locked_   = false;
    uint64_t mtimeNs_  = 0;
};

}  // namespace tat