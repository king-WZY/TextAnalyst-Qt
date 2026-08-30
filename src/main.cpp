// =============================================================================
// main.cpp: 装配入口（§6.5 启动流程 / §6.6 信号处理与优雅退出）
// 信号处理采用"自管道"模式（v1.0.1 修订：async-signal-safe）：
//   处理器内只 write() 到 socketpair 写端；主线程 QSocketNotifier 读到后 quit。
// =============================================================================

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <unistd.h>

#include "io/LogRotator.h"

#include "controller/CommandLineParser.h"
#include "controller/MainController.h"
#include "io/FileWatcher.h"
#include "io/SettingsManager.h"
#include "ui/mainwindow/MainWindow.h"

namespace {

int g_sigPipe[2] = {-1, -1};

void sigHandler(int /*sig*/) {
    // async-signal-safe：只做 write（§6.6 v1.0.1）
    const char byte = 'x';
    const ssize_t r = ::write(g_sigPipe[1], &byte, 1);
    (void)r;
}

void installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = sigHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

// 日志落盘：Info 级及以上写入 ~/.cache/textanalyst-qt/textanalyst-qt.log
// （LogRotator 轮转：4 MiB × 4 历史档，最多 20 MiB，DISPLAYDESIGN §11.2 R-12）。
// 消息处理器可在任意线程触发（Qt 保证上下文），LogRotator 内部互斥。
void crashLogger(QtMsgType type, const QMessageLogContext& ctx,
                 const QString& msg) {
    std::fprintf(stderr, "%s\n", msg.toUtf8().constData());
    if (type < QtInfoMsg) return;  // Debug 只进 stderr

    static std::once_flag once;
    static std::unique_ptr<tat::LogRotator> rotator;
    std::call_once(once, [] {
        // GenericCacheLocation 不叠加 org/app 段，避免 CacheLocation 的
        // "~/.cache/<org>/<app>" 双层级嵌套（日志路径见 DISPLAYDESIGN §11.2）
        const QString dir =
            QDir(QStandardPaths::writableLocation(
                     QStandardPaths::GenericCacheLocation))
                .filePath(QStringLiteral("textanalyst-qt"));
        QDir().mkpath(dir);
        rotator = std::make_unique<tat::LogRotator>(
            dir + QStringLiteral("/textanalyst-qt.log"),
            4 * 1024 * 1024 /* maxSize */, 4 /* maxBackups */);
    });
    const char lvl = type == QtInfoMsg      ? 'I'
                     : type == QtWarningMsg ? 'W'
                     : type == QtCriticalMsg ? 'C'
                                             : 'F';
    rotator->append(QStringLiteral("%1 [%2] %3:%4 %5")
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                        .arg(QLatin1Char(lvl))
                        .arg(QString::fromUtf8(ctx.file ? ctx.file : ""))
                        .arg(ctx.line)
                        .arg(msg));
}

void runGrepAction(tat::MainController* ctl, const QString& pattern) {
    tat::FilterRule r;
    r.id = 1;
    r.action = tat::FilterAction::Include;
    r.mode = tat::MatchMode::Substring;
    r.pattern = pattern.toStdString();
    ctl->applyFilter({r});
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("textanalyst-qt"));
    QCoreApplication::setApplicationName(QStringLiteral("textanalyst-qt"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1.0"));
    qInstallMessageHandler(&crashLogger);

    bool parseOk = false;
    const auto opts = tat::CommandLineParser::parse(app.arguments(), &parseOk);
    if (!parseOk) return 1;

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigPipe);
    installSignalHandlers();
    QSocketNotifier sigNotifier(g_sigPipe[0], QSocketNotifier::Read);
    QObject::connect(&sigNotifier, &QSocketNotifier::activated,
                     [&app] { app.quit(); });  // 优雅退出 → MainWindow::closeEvent

    tat::SettingsManager settings;
    tat::FileWatcher watcher;
    tat::MainController controller;
    controller.setSettingsManager(&settings);
    controller.setFileWatcher(&watcher);

    tat::MainWindow window(&controller, &settings);
    window.show();

    // 命令行动作（§6.5）
    if (!opts.file.isEmpty()) {
        window.openFile(opts.file);
        if (opts.line > 0) {
            QObject::connect(&controller, &tat::MainController::fileOpened,
                             &window, [&controller, opts](const QString&, int) {
                                 controller.jumpToLine(opts.line);
                             });
        }
        if (!opts.grep.isEmpty()) {
            QObject::connect(&controller, &tat::MainController::fileOpened,
                             &window, [&controller, opts](const QString&, int) {
                                 runGrepAction(&controller, opts.grep);
                             });
        }
        if (!opts.exportPath.isEmpty()) {
            if (!opts.grep.isEmpty()) {
                // --grep + --export：导出匹配行（grep 直觉语义）
                bool* done = new bool{false};
                QObject::connect(&controller, &tat::MainController::filterApplied,
                                 &window,
                                 [&controller, opts, done](bool ok,
                                                           const QString&) {
                                     if (!ok || *done) return;
                                     auto rs = controller.rulesSnapshot();
                                     if (!rs || !rs->anyRule) return;
                                     *done = true;
                                     controller.exportMatching(opts.exportPath);
                                 });
            } else if (opts.load.isEmpty()) {
                // 无异步规则注入：文件打开后即可导出
                QObject::connect(
                    &controller, &tat::MainController::fileOpened, &window,
                    [&controller, opts](const QString&, int) {
                        controller.exportLines(1, controller.rowCount(),
                                               opts.exportPath,
                                               /*skipDimmed=*/true);
                    },
                    Qt::SingleShotConnection);
            } else {
                // 有 --grep：过滤是异步任务，须等"规则快照非空"的
                // filterApplied 才导出（空规则结果一律跳过），且只导出一次。
                bool* done = new bool{false};
                QObject::connect(&controller, &tat::MainController::filterApplied,
                                 &window,
                                 [&controller, opts, done](bool ok,
                                                           const QString&) {
                                     if (!ok || *done) return;
                                     auto rs = controller.rulesSnapshot();
                                     if (!rs || !rs->anyRule) return;
                                     *done = true;
                                     controller.exportLines(
                                         1, controller.rowCount(),
                                         opts.exportPath,
                                         /*skipDimmed=*/true);
                                 });
            }
        }
    } else if (!opts.noRestore) {
        controller.restoreSession();
    }

    if (!opts.load.isEmpty()) {
        std::vector<tat::FilterRule> rules;
        controller.loadFilters(opts.load, &rules);
        if (!rules.empty()) {
            if (!opts.file.isEmpty()) {
                // 规则须在文件打开后应用（onFileOpened 会清空过滤状态，
                // 此处连接顺序在 MainWindow 之后 → 最终生效的是 --load 规则）
                QObject::connect(&controller, &tat::MainController::fileOpened,
                                 &window,
                                 [&controller, rules](const QString&, int) {
                                     controller.applyFilter(rules);
                                 });
            } else {
                controller.applyFilter(rules);
            }
        }
    }

    const int rc = app.exec();
    ::close(g_sigPipe[0]);
    ::close(g_sigPipe[1]);
    return rc;
}