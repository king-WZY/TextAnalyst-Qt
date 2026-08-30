// =============================================================================
// MainController.cpp: 控制层实现
// 并发模型（DISPLAYDESIGN §5）：(确保) 所有耗时操作进 QThreadPool，
// 结果经 QMetaObject::invokeMethod(Qt::QueuedConnection) 回主线程。
// =============================================================================

#include "controller/MainController.h"

#include <QFile>
#include <QtDebug>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include "io/FileWatcher.h"
#include "io/SettingsManager.h"
#include "io/TatSerializer.h"

namespace tat {

namespace {

// Qt 正则编译注入（DISPLAYDESIGN §4.1/§4.1.1）：
// v1.0 输出以 pattern 为缓存键；匹配回调用 1-entry thread_local 缓存，
// 同一任务内 pattern/cs 恒定 → 每行只做 match()，摊薄编译开销。
CompileFn makeQtCompile() {
    return [](const std::string& pattern, bool cs, bool /*wholeWord*/,
              std::string* out, std::string* err) -> int {
        if (pattern.empty()) {  // 空模式显式拒绝（Qt 6.4 无 DontMatchEmptyString）
            *err = "empty pattern";
            return -1;
        }
        QRegularExpression::PatternOptions opts =
            QRegularExpression::NoPatternOption;
        if (!cs) opts |= QRegularExpression::CaseInsensitiveOption;
        const QRegularExpression re(QString::fromStdString(pattern), opts);
        if (!re.isValid()) {
            *err = re.errorString().toStdString();
            return -1;
        }
        *out = pattern;  // v1.1 改 PCRE2 C API 真正预编译（§13.1 L1）
        return 0;
    };
}

MatchFn makeQtMatch() {
    return [](std::string_view line, const std::string& /*compiled*/,
              const std::string& pattern, bool cs) -> bool {
        static thread_local std::string cachePattern;
        static thread_local bool        cacheCs = false;
        static thread_local std::unique_ptr<QRegularExpression> cacheRe;
        if (pattern.empty()) return false;  // 空模式不匹配
        if (!cacheRe || cachePattern != pattern || cacheCs != cs) {
            QRegularExpression::PatternOptions opts =
                QRegularExpression::NoPatternOption;
            if (!cs) opts |= QRegularExpression::CaseInsensitiveOption;
            cacheRe = std::make_unique<QRegularExpression>(
                QString::fromStdString(pattern), opts);
            cachePattern = pattern;
            cacheCs = cs;
        }
        return cacheRe->match(QString::fromUtf8(line.data(),
                                                static_cast<qsizetype>(line.size())))
            .hasMatch();
    };
}

}  // namespace

MainController::MainController(QObject* parent)
    : QObject(parent), m_compile(makeQtCompile()), m_match(makeQtMatch()) {
    qRegisterMetaType<Error>("tat::Error");
    qRegisterMetaType<SearchResult>("tat::SearchResult");
}

MainController::~MainController() {
    m_token.cancel();  // 让进行中的索引/匹配/搜索任务尽快自我终止
}

// ---------------------------------------------------------------------------
// 命令
// ---------------------------------------------------------------------------

void MainController::openFile(const QString& path) {
    cancelCurrentTask();
    const int gen = m_token.current();
    const TokenSnapshot ts{&m_token, gen};
    const QString p = path;

    (void)QtConcurrent::run([this, p, ts]() {
        Error err;
        EncodingInfo enc;
        auto tb = TextBuffer::create(p.toStdString(), &err, &enc, &ts, nullptr);
        QMetaObject::invokeMethod(
            this,
            [this, tb, err, p]() { onOpenFinished(tb, err, p); },
            Qt::QueuedConnection);
    });
}

void MainController::onOpenFinished(std::shared_ptr<TextBuffer> tb, Error err,
                                    const QString& path) {
    if (!err.ok() || !tb) {
        qWarning("open failed: %s (%s: %s)", qPrintable(path),
                 err.op.c_str(), err.message.c_str());
        emit fileOpenFailed(path, QString::fromStdString(err.op),
                            QString::fromStdString(err.message));
        return;
    }
    qInfo("file opened: %s rows=%d bytes=%zu", qPrintable(path),
          tb ? tb->rowCount() : 0, tb ? tb->totalBytes() : 0);
    m_buffer.publish(std::move(tb));
    m_resultStore.clear();
    m_markers.clear();
    m_currentPath = path;
    m_currentLine = 1;
    if (m_settings) m_settings->addRecentFile(path);
    if (m_watcher) m_watcher->watch(path);
    emit bufferChanged();
    emit fileOpened(path, rowCount());
}

void MainController::applyFilter(const std::vector<FilterRule>& rules) {
    // 1) 主线程：构造规则快照（编译/验证正则）
    std::shared_ptr<RuleSet> rs;
    const int gen = m_token.current();
    const Error berr = FilterEngine::buildRuleSet(rules, gen, m_compile, &rs);
    if (berr.hasError()) {
        emit filterApplied(false, QString::fromStdString(berr.message));
        return;
    }
    m_rules.publish(rs);
    m_currentRules = rules;

    // 2) 取消旧任务，取新世代
    cancelCurrentTask();
    const int newGen = m_token.current();
    const TokenSnapshot ts{&m_token, newGen};
    const int threads = m_settings ? m_settings->parallelThreads() : 0;
    auto buffer = m_buffer.snapshot();
    if (!buffer) {
        emit filterApplied(true, QString());
        return;
    }
    const MatchFn match = m_match;  // 拷贝（只读）
    auto result = std::make_shared<FilterResult>();
    auto applyTask = [buffer, rs, result, threads, ts, match, gen = rs->generation,
                      this]() {
        Error e = FilterEngine::applyParallel(*buffer, *rs, threads, result.get(),
                                              &ts, nullptr, match, nullptr);
        QMetaObject::invokeMethod(
            this,
            [this, rs, result, e, gen]() {
                if (e.ok()) m_resultStore.publish(result);
                if (e.ok())
                    qInfo("filter applied: rules=%zu rows=%zu gen=%d",
                          rs ? rs->rules.size() : 0,
                          result ? result->states.size() : 0, gen);
                else
                    qWarning("filter failed: %s", e.message.c_str());
                emit filterApplied(e.ok(),
                                   QString::fromStdString(e.message));
            },
            Qt::QueuedConnection);
    };
    (void)QtConcurrent::run(applyTask);
}

void MainController::onFilterFinished(std::shared_ptr<FilterResult> result,
                                      Error err, int generation) {
    (void)generation;
    if (err.ok() && result) m_resultStore.publish(result);
    emit filterApplied(err.ok(), QString::fromStdString(err.message));
}

bool MainController::toggleMarker(int line, int markerId) {
    const bool added = m_markers.toggle(line, markerId);
    emit markerToggled(line, markerId, added);
    return added;
}

bool MainController::jumpToLine(int lineNo) {
    auto b = m_buffer.snapshot();
    if (!b || lineNo < 1 || lineNo > b->rowCount()) return false;
    m_currentLine = lineNo;
    emit currentLineChanged(lineNo);
    return true;
}

bool MainController::jumpToNextMarker(int lineNo, int markerId) {
    const auto next = m_markers.next(lineNo, markerId, true);
    if (!next) {
        emit statusMessage(QStringLiteral("无更多标记"));
        return false;
    }
    m_currentLine = *next;
    emit currentLineChanged(*next);
    return true;
}

void MainController::search(const SearchOptions& opt) {
    auto buffer = m_buffer.snapshot();
    if (!buffer) return;
    cancelCurrentTask();
    const int newGen = m_token.current();
    const TokenSnapshot ts{&m_token, newGen};
    const CompileFn compile = m_compile;
    const MatchFn match = m_match;
    auto result = std::make_shared<SearchResult>();
    (void)QtConcurrent::run([buffer, opt, ts, compile, match, result, this]() {
        Error e = Searcher::search(*buffer, opt, compile, match, result.get(),
                                   &ts, nullptr);
        QMetaObject::invokeMethod(
            this,
            [this, result, e]() {
                (void)e;
                emit searchCompleted(*result);
            },
            Qt::QueuedConnection);
    });
}

Error MainController::exportLines(int startLine, int endLine,
                                  const QString& path, bool skipDimmed) {
    auto b = m_buffer.snapshot();
    if (!b) return {ErrCode::InvalidArgument, "exportLines", "no file open"};
    const int lo = std::max(1, startLine);
    const int hi = std::min(b->rowCount(), endLine);
    qInfo("export lines [%d..%d] -> %s", lo, hi, qPrintable(path));
    auto result = m_resultStore.snapshot();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {ErrCode::IoError, "exportLines",
                f.errorString().toStdString()};
    for (int i = lo; i <= hi; ++i) {
        RowState rs;
        if (result && i - 1 < static_cast<int>(result->states.size()))
            rs.raw = result->states[static_cast<size_t>(i - 1)];
        if (rs.isHidden()) continue;                     // 隐藏行不导出
        if (skipDimmed && rs.state() == ResultState::Dimmed)
            continue;                                    // 弱化行不导出
        const std::string line = b->toUtf8(i - 1);
        f.write(line.data(), static_cast<qint64>(line.size()));
        f.write("\n", 1);
    }
    return f.error() == QFileDevice::NoError
               ? Error::none()
               : Error{ErrCode::IoError, "exportLines", "write failed"};
}

Error MainController::exportMatching(const QString& path) {
    auto b = m_buffer.snapshot();
    if (!b) return {ErrCode::InvalidArgument, "exportMatching", "no file open"};
    auto result = m_resultStore.snapshot();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {ErrCode::IoError, "exportMatching",
                f.errorString().toStdString()};
    for (int i = 0; i < b->rowCount(); ++i) {
        RowState rs;
        if (result && i < static_cast<int>(result->states.size()))
            rs.raw = result->states[static_cast<size_t>(i)];
        if (rs.state() != ResultState::Highlighted) continue;
        const std::string line = b->toUtf8(i);
        f.write(line.data(), static_cast<qint64>(line.size()));
        f.write("\n", 1);
    }
    qInfo("export matched lines -> %s", qPrintable(path));
    return f.error() == QFileDevice::NoError
               ? Error::none()
               : Error{ErrCode::IoError, "exportMatching", "write failed"};
}

Error MainController::loadFilters(const QString& path,
                                  std::vector<FilterRule>* out) {
    return TatSerializer::read(path, out);
}

Error MainController::saveFilters(const QString& path,
                                  const std::vector<FilterRule>& rules) {
    return TatSerializer::write(path, rules);
}

void MainController::cancelCurrentTask() {
    m_token.cancel();
}

// ---------------------------------------------------------------------------
// 快照（无锁渲染路径）
// ---------------------------------------------------------------------------

std::shared_ptr<const TextBuffer> MainController::bufferSnapshot() const noexcept {
    return m_buffer.snapshot();
}
std::shared_ptr<const FilterResult> MainController::resultSnapshot() const noexcept {
    return m_resultStore.snapshot();
}
std::shared_ptr<const RuleSet> MainController::rulesSnapshot() const noexcept {
    return m_rules.snapshot();
}

int MainController::rowCount() const noexcept {
    auto b = m_buffer.snapshot();
    return b ? b->rowCount() : 0;
}

TextBuffer::MemoryFootprint MainController::memoryFootprint() const {
    auto b = m_buffer.snapshot();
    return b ? b->footprint() : TextBuffer::MemoryFootprint{};
}

// ---------------------------------------------------------------------------
// 会话
// ---------------------------------------------------------------------------

void MainController::saveSession() const {
    if (!m_settings) return;
    m_settings->setSessionMarkers([&] {
        std::vector<Marker> out;
        m_markers.save(&out);
        return out;
    }());
    if (!m_currentPath.isEmpty())
        m_settings->addRecentFile(m_currentPath);
    m_settings->sync();
}

void MainController::restoreSession() {
    if (!m_settings || !m_settings->sessionRestore()) return;
    m_markers.load(m_settings->sessionMarkers());
    const QStringList recent = m_settings->recentFiles();
    if (!recent.isEmpty() && QFile::exists(recent.first()))
        openFile(recent.first());
}

// ---------------------------------------------------------------------------
// 槽
// ---------------------------------------------------------------------------
// 双击行的处理位于 UI 层（MainWindow::onLineDoubleClicked → 预填过滤面板，
// v1.2 行为修订 R-14：控制层不再隐式把行转成规则并应用，历史实现删除）。

}  // namespace tat