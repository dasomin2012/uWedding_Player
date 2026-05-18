#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

#include "app/Application.h"

namespace {

// 파일 로거: 모든 qDebug/qInfo/qWarning/qCritical 을 콘솔(stderr) 과
// data/uWeddingPlayer.log 에 동시 출력하며, 메시지마다 flush 한다.
// (CLAUDE.md 로그 요구사항 — 일자별 로테이션은 Phase 7 에서 확장)
QFile      g_logFile;
QMutex     g_logMutex;

void messageHandler(QtMsgType type, const QMessageLogContext& ctx,
                     const QString& msg) {
    Q_UNUSED(ctx);
    const char* lvl = "INFO ";
    switch (type) {
        case QtDebugMsg:    lvl = "DEBUG"; break;
        case QtInfoMsg:     lvl = "INFO "; break;
        case QtWarningMsg:  lvl = "WARN "; break;
        case QtCriticalMsg: lvl = "ERROR"; break;
        case QtFatalMsg:    lvl = "FATAL"; break;
    }
    const QString line = QString("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
        .arg(lvl)
        .arg(msg);

    QMutexLocker lock(&g_logMutex);
    // stderr (콘솔 실행 시 가시)
    fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    fflush(stderr);
    // 파일 (flush 보장 → force-kill 에도 손실 없음)
    if (g_logFile.isOpen()) {
        g_logFile.write(line.toUtf8());
        g_logFile.write("\n");
        g_logFile.flush();
    }
    if (type == QtFatalMsg) abort();
}

void installFileLogger() {
    const QString dir = QCoreApplication::applicationDirPath() + "/data";
    QDir().mkpath(dir);
    g_logFile.setFileName(dir + "/uWeddingPlayer.log");
    g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("uWeddingPlayer");
    QApplication::setOrganizationName("uWedding");
    QApplication::setApplicationVersion("0.2.0");

    installFileLogger();
    qInfo() << "==== uWeddingPlayer starting (Phase 2) ====";

    uwp::Application uwpApp;
    if (!uwpApp.initialize()) {
        qCritical() << "Application::initialize failed";
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &uwpApp, &uwp::Application::shutdown);

    return app.exec();
}
