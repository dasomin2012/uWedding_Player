#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QMessageBox>
#include <QMutex>
#include <QSharedMemory>
#include <QTextStream>
#include <QThread>

#include "app/Application.h"
#include "app/CrashHandler.h"

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
    // HiDPI 스케일링 활성화 — 4K/QHD 모니터에서 UI 가 너무 작게 보이는
    // 문제 해결. QApplication 생성 전에 설정해야 유효.
    //   * AA_EnableHighDpiScaling: 시스템 DPI 감지 → 위젯 자동 스케일
    //   * AA_UseHighDpiPixmaps  : QPixmap/QIcon 을 devicePixelRatio 로 렌더
    //   * PassThrough           : 분수 배율(예: 1.5x) 도 반올림 없이 통과 →
    //                             150% 스케일 모니터에서 정확 표시
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName("uWeddingPlayer");
    QApplication::setOrganizationName("uWedding");
    QApplication::setApplicationVersion("0.2.0");

    // 앱 UI 기본 폰트 — Windows 기본 한글 폰트 "맑은 고딕" (Malgun Gothic).
    // 시스템 상주 폰트라 별도 번들 불필요. 미설치 환경(비-Windows/구 XP)에서는
    // Qt 시스템 폰트로 폴백.
    {
        QFont uiFont(QStringLiteral("맑은 고딕"));
        uiFont.setPointSize(10);
        uiFont.setStyleStrategy(QFont::PreferAntialias);
        QApplication::setFont(uiFont);
    }

    installFileLogger();
    // P4: 크래시 시 mini-dump 저장 인프라. 로거 뒤에 등록해 실패 로그 확보.
    uwp::installCrashHandler(
        QCoreApplication::applicationDirPath() + "/data/crash");
    qInfo() << "==== uWeddingPlayer starting (Phase 2) ====";

    // 단일 인스턴스 락 — 두 번째 인스턴스가 뜨면 OBS 도 중복 기동되어 포트
    // 충돌·씬 오염이 발생. QSharedMemory 로 OS-레벨 자동 정리(우리가 크래시해도
    // Windows 는 프로세스 핸들과 함께 세그먼트 회수) 보장.
    // 키: 로컬 사용자별 유일화 위해 조직·앱명 프리픽스 (Hanmac 계열 규칙).
    //
    // 재시작(디스플레이 설정 변경) 시나리오: 부모가 startDetached 로 자식 spawn
    // 후 quit() 하는 짧은 순간, 자식이 락을 못 잡을 수 있음. 짧게 재시도해서
    // 부모 exit 대기(최대 ~3초). 진짜 중복 실행이면 재시도 후에도 실패 → 종료.
    QSharedMemory instanceLock(QStringLiteral("Hanmac-uWeddingPlayer-instance"));
    bool locked = instanceLock.create(1);
    if (!locked && instanceLock.error() == QSharedMemory::AlreadyExists) {
        for (int i = 0; i < 30 && !locked; ++i) {
            QThread::msleep(100);
            locked = instanceLock.create(1);
        }
    }
    if (!locked) {
        if (instanceLock.error() == QSharedMemory::AlreadyExists) {
            qWarning() << "uWeddingPlayer: 이미 실행 중 — 두 번째 인스턴스 종료";
            QMessageBox::information(nullptr,
                QStringLiteral("uWeddingPlayer"),
                QStringLiteral("이미 실행 중입니다.\n작업 표시줄에서 기존 창을 확인해 주세요."));
            return 0;
        }
        // 다른 종류의 에러는 락 없이 진행(안전보다 가동성 우선) + 경고.
        qWarning() << "uWeddingPlayer: QSharedMemory::create failed —"
                   << instanceLock.errorString() << "— 락 없이 진행";
    }

    uwp::Application uwpApp;
    if (!uwpApp.initialize()) {
        qCritical() << "Application::initialize failed";
        return 1;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &uwpApp, &uwp::Application::shutdown);

    return app.exec();
}
