// =============================================================================
// MemoryMappedFile.cpp: POSIX mmap 实现
// 打开算法见 DISPLAYDESIGN.md §2.2.2（MAP_PRIVATE 快照语义、fcntl 采样锁）
// =============================================================================

#include "buffer/MemoryMappedFile.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tat {

namespace {

// 计算 fstat st_mtim 的纳秒值（glibc 2.35 起 st_mtim 为标准字段）
uint64_t mtimeNsFromStat(const struct stat& st) {
#ifdef __APPLE__
    return static_cast<uint64_t>(st.st_mtimespec.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(st.st_mtimespec.tv_nsec);
#else
    return static_cast<uint64_t>(st.st_mtim.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(st.st_mtim.tv_nsec);
#endif
}

}  // namespace

MemoryMappedFile::~MemoryMappedFile() { closeNoThrow(); }

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& o) noexcept
    : data_(o.data_), size_(o.size_), fd_(o.fd_), writable_(o.writable_),
      locked_(o.locked_), mtimeNs_(o.mtimeNs_) {
    o.data_ = nullptr;
    o.size_ = 0;
    o.fd_   = -1;
    o.writable_ = false;
    o.locked_   = false;
    o.mtimeNs_  = 0;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& o) noexcept {
    if (this != &o) {
        closeNoThrow();
        data_ = o.data_; size_ = o.size_; fd_ = o.fd_;
        writable_ = o.writable_; locked_ = o.locked_; mtimeNs_ = o.mtimeNs_;
        o.data_ = nullptr; o.size_ = 0; o.fd_ = -1;
        o.writable_ = false; o.locked_ = false; o.mtimeNs_ = 0;
    }
    return *this;
}

// 采样式读锁：open 时尝试 F_SETLK 一次；成功则 locked_=true，
// 失败仅记录提示，不阻断打开（与 TAT 快照策略一致，§2.2.3）。
// 注意：本实现按 DISPLAYDESIGN 决策，不加"持锁直到关闭"（会与 tail -f 互斥），
// 锁在 closeNoThrow 中随 fd 释放。
MemoryMappedFile MemoryMappedFile::open(const std::string& path,
                                        MemoryMapFlags flags, Error* error) {
    MemoryMappedFile mm;
    const int oflags = (flags == MemoryMapFlags::ReadWrite) ? O_RDWR : O_RDONLY;

    int fd = ::open(path.c_str(), oflags | O_CLOEXEC);
    if (fd < 0) {
        *error = {ErrCode::PermissionDenied, "open",
                  "cannot open file: " + std::string(strerror(errno))};
        return mm;
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        *error = {ErrCode::IoError, "fstat", strerror(errno)};
        ::close(fd);
        return mm;
    }

    mm.fd_ = fd;
    mm.writable_ = (flags == MemoryMapFlags::ReadWrite);
    mm.mtimeNs_ = mtimeNsFromStat(st);

    // 空文件特例：mmap(len=0) 在部分内核返回错误，显式走空映射分支。
    if (st.st_size == 0) {
        *error = Error::none();
        return mm;  // isValid()==true, data()==nullptr
    }

    // 采样读锁（非阻塞）
    struct flock fl{};
    fl.l_type = F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = st.st_size;
    mm.locked_ = (::fcntl(fd, F_SETLK, &fl) == 0);

    const int prot = (flags == MemoryMapFlags::ReadWrite) ? PROT_READ | PROT_WRITE
                                                          : PROT_READ;
    void* mapped = ::mmap(nullptr, static_cast<size_t>(st.st_size), prot,
                          MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        *error = {ErrCode::MapFailed, "mmap",
                  "mmap failed: " + std::string(strerror(errno))};
        ::close(fd);
        mm.fd_ = -1;
        return mm;
    }

    mm.data_ = mapped;
    mm.size_ = static_cast<size_t>(st.st_size);

    // MADV_RANDOM：日志是随机访问模式（索引构建期亦然），避免内核预读浪费。
    ::madvise(mapped, mm.size_, MADV_RANDOM);

    // 映射后关闭 fd：mmap 持有独立引用；释放 fd 表配额（DISPLAYDESIGN §2.2.2 步骤 8）
    ::close(fd);
    mm.fd_ = -1;

    *error = Error::none();
    return mm;
}

MemoryMappedFile MemoryMappedFile::createOrTruncate(const std::string& path,
                                                    size_t preAllocBytes,
                                                    Error* error) {
    MemoryMappedFile mm;
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        *error = {ErrCode::IoError, "open(O_CREAT)", strerror(errno)};
        return mm;
    }
    if (preAllocBytes > 0) {
        // 预分配 + 落盘，避免写时分配抖动（ARCH-UB §3.1.4）
        if (::ftruncate(fd, static_cast<off_t>(preAllocBytes)) != 0 ||
            ::posix_fallocate(fd, 0, preAllocBytes) != 0) {
            *error = {ErrCode::IoError, "ftruncate/posix_fallocate", strerror(errno)};
            ::close(fd);
            return mm;
        }
    }
    void* mapped = ::mmap(nullptr, preAllocBytes, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    if (preAllocBytes > 0 && mapped == MAP_FAILED) {
        *error = {ErrCode::MapFailed, "mmap", strerror(errno)};
        ::close(fd);
        return mm;
    }
    mm.data_ = (preAllocBytes == 0) ? nullptr : mapped;
    mm.size_ = preAllocBytes;
    mm.fd_ = fd;  // 写入模式保留 fd，供导出完成后 msync/truncate
    mm.writable_ = true;
    *error = Error::none();
    return mm;
}

bool MemoryMappedFile::adviseRandom() noexcept {
    if (data_ && size_ > 0) return ::madvise(data_, size_, MADV_RANDOM) == 0;
    return false;
}
bool MemoryMappedFile::adviseNormal() noexcept {
    if (data_ && size_ > 0) return ::madvise(data_, size_, MADV_NORMAL) == 0;
    return false;
}
bool MemoryMappedFile::adviseSequential() noexcept {
    if (data_ && size_ > 0) return ::madvise(data_, size_, MADV_SEQUENTIAL) == 0;
    return false;
}
bool MemoryMappedFile::adviseDontNeed(const char* range, size_t len) noexcept {
    if (!range || len == 0) return false;
    return ::madvise(const_cast<char*>(range), len, MADV_DONTNEED) == 0;
}

uint64_t MemoryMappedFile::statMtimeNs() const noexcept {
    if (fd_ < 0) return 0;
    struct stat st{};
    if (::fstat(fd_, &st) != 0) return 0;
    return mtimeNsFromStat(st);
}

bool MemoryMappedFile::sizeChanged() const noexcept {
    if (fd_ < 0) return false;
    struct stat st{};
    if (::fstat(fd_, &st) != 0) return false;
    return static_cast<uint64_t>(st.st_size) != static_cast<uint64_t>(size_);
}

void MemoryMappedFile::close() noexcept { closeNoThrow(); }

void MemoryMappedFile::closeNoThrow() {
    if (data_) {
        ::munmap(data_, size_);
        data_ = nullptr;
        size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    writable_ = false;
    locked_ = false;
    mtimeNs_ = 0;
}

}  // namespace tat