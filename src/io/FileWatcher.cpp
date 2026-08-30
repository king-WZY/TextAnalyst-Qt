// =============================================================================
// FileWatcher.cpp: inotify 实现
// 事件掩码：IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB（§2.6.2）
// 触发后不自动重载，仅发 externalChanged 信号（由 UI 提示"点击重载"）。
// =============================================================================

#include "io/FileWatcher.h"

#include <errno.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>

namespace tat {

FileWatcher::FileWatcher(QObject* parent) : QObject(parent) {}

FileWatcher::~FileWatcher() { unwatch(); }

bool FileWatcher::watch(const QString& path) {
    unwatch();

    m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) {
        emit watchFailed(path);
        return false;
    }
    // 每次仅监听 1 个文件（受 max_user_watches 限制约束，§2.6.2 MUST）
    m_wd = ::inotify_add_watch(m_fd, path.toUtf8().constData(),
                               IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO |
                                   IN_ATTRIB);
    if (m_wd < 0) {
        ::close(m_fd);
        m_fd = -1;
        emit watchFailed(path);
        return false;
    }
    m_path = path;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this,
            &FileWatcher::onReadable);
    return true;
}

void FileWatcher::unwatch() {
    if (m_wd >= 0) {
        ::inotify_rm_watch(m_fd, m_wd);
        m_wd = -1;
    }
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_path.clear();
}

void FileWatcher::onReadable() {
    std::array<char, 4096> buf{};
    for (;;) {
        const ssize_t n = ::read(m_fd, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EAGAIN) break;   // 已清空
            break;                        // 其他错误：保持监听，下轮可再读
        }
        if (n == 0) break;
        // 解析 inotify_event 链表；忽略 IN_ISDIR
        for (char* p = buf.data(); p < buf.data() + n;) {
            const auto* ev = reinterpret_cast<const inotify_event*>(p);
            const uint32_t mask = ev->mask;
            if (!(mask & IN_ISDIR)) {
                emit externalChanged(m_path);
                // 文件被 mv 走：原路径失效，停止监听（§2.6.2）
                if (mask & IN_MOVED_FROM) {
                    unwatch();
                    return;
                }
            }
            p += sizeof(inotify_event) + ev->len;
        }
    }
}

}  // namespace tat