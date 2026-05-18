#pragma once

#include <QObject>
#include <QHash>
#include <QImage>
#include <QString>

class QProcess;

namespace uwp {

class Settings;

// 미디어의 정지화상을 제공한다 (Preview 렌더링용).
//  - 이미지 파일: 직접 로드 후 다운스케일.
//  - 영상 파일  : FFmpeg CLI 로 1초 지점(없으면 첫 프레임) 추출.
// 결과는 메모리 + 디스크(png) 에 캐시. 추출은 QProcess 비동기.
// FFmpeg 미탑재 시 graceful: 영상은 snapshotFailed, 이미지는 정상.
class SnapshotCache : public QObject {
    Q_OBJECT
public:
    explicit SnapshotCache(Settings* settings, QObject* parent = nullptr);
    ~SnapshotCache() override;

    // 메모리 캐시에 있으면 반환, 없으면 null QImage.
    QImage cached(const QString& mediaPath) const;

    // 캐시에 있으면 즉시 snapshotReady, 없으면 비동기 추출 후 emit.
    void request(const QString& mediaPath);

    bool ffmpegAvailable() const { return !m_ffmpeg.isEmpty(); }

signals:
    void snapshotReady(const QString& mediaPath, const QImage& image);
    void snapshotFailed(const QString& mediaPath, const QString& reason);

private slots:
    void onProcessFinished(int exitCode, int exitStatus);

private:
    struct Job;

    QString resolveFfmpeg() const;
    QString cacheKey(const QString& absPath) const;
    QString cacheFilePath(const QString& key) const;
    bool    isImageFile(const QString& path) const;
    void    finishWithImage(const QString& media, const QImage& img);
    void    startFfmpeg(Job* job, double seekSeconds);

    Settings*               m_settings = nullptr;
    QString                 m_ffmpeg;       // 해석된 ffmpeg 실행경로 (없으면 빈값)
    QString                 m_cacheDir;     // 절대경로
    int                     m_maxDim = 1280;
    QHash<QString, QImage>  m_mem;          // mediaPath -> image
    QHash<QProcess*, Job*>  m_jobs;         // 진행중 추출
    QHash<QString, bool>    m_inFlight;     // mediaPath 중복 요청 방지
};

} // namespace uwp
