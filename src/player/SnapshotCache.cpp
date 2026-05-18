#include "SnapshotCache.h"

#include "app/Settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>

namespace uwp {

struct SnapshotCache::Job {
    QString   media;       // 요청된 원본 경로
    QString   absPath;
    QString   key;
    QString   outFile;
    int       attempt = 0; // 0 = -ss 1, 1 = -ss 0(첫 프레임)
    QProcess* proc    = nullptr;
};

// ---- helpers --------------------------------------------------
static QImage scaledToMax(const QImage& src, int maxDim) {
    if (src.isNull()) return src;
    const int w = src.width(), h = src.height();
    if (qMax(w, h) <= maxDim) return src;
    return src.scaled(QSize(maxDim, maxDim), Qt::KeepAspectRatio,
                       Qt::SmoothTransformation);
}

// ---- ctor / dtor ----------------------------------------------
SnapshotCache::SnapshotCache(Settings* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_ffmpeg = resolveFfmpeg();
    if (m_ffmpeg.isEmpty()) {
        qWarning() << "SnapshotCache: ffmpeg not found — video snapshots disabled "
                      "(set settings.ffmpeg_path or add ffmpeg to PATH)";
    } else {
        qInfo() << "SnapshotCache: ffmpeg =" << m_ffmpeg;
    }

    QString dir = m_settings ? m_settings->snapshotCacheDir() : QString("data/cache");
    if (QDir::isRelativePath(dir)) {
        dir = QDir(QCoreApplication::applicationDirPath()).filePath(dir);
    }
    QDir().mkpath(dir);
    m_cacheDir = dir;
    qInfo() << "SnapshotCache: cache dir =" << m_cacheDir;
}

SnapshotCache::~SnapshotCache() {
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it.key()) {
            it.key()->kill();
            it.key()->deleteLater();
        }
        delete it.value();
    }
    m_jobs.clear();
}

// ---- resolution -----------------------------------------------
QString SnapshotCache::resolveFfmpeg() const {
    // 1) 명시 설정
    if (m_settings && !m_settings->ffmpegPath().isEmpty()) {
        QString p = m_settings->ffmpegPath();
        if (QDir::isRelativePath(p)) {
            p = QDir(QCoreApplication::applicationDirPath()).filePath(p);
        }
        if (QFileInfo::exists(p)) return p;
        qWarning() << "SnapshotCache: settings.ffmpeg_path not found:" << p;
    }
    // 2) 실행파일 옆 번들
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath("ffmpeg.exe");
    if (QFileInfo::exists(bundled)) return bundled;

    // 3) PATH
    const QString onPath = QStandardPaths::findExecutable("ffmpeg");
    if (!onPath.isEmpty()) return onPath;

    return QString();
}

QString SnapshotCache::cacheKey(const QString& absPath) const {
    const QFileInfo fi(absPath);
    const QString raw = absPath + "|" + QString::number(fi.size()) + "|"
                        + QString::number(fi.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString SnapshotCache::cacheFilePath(const QString& key) const {
    return QDir(m_cacheDir).filePath(key + ".png");
}

bool SnapshotCache::isImageFile(const QString& path) const {
    static const QStringList kImg = {
        "png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"
    };
    return kImg.contains(QFileInfo(path).suffix().toLower());
}

// ---- public ---------------------------------------------------
QImage SnapshotCache::cached(const QString& mediaPath) const {
    return m_mem.value(mediaPath);
}

void SnapshotCache::request(const QString& mediaPath) {
    const QString absPath = QFileInfo(mediaPath).absoluteFilePath();

    if (!QFileInfo::exists(absPath)) {
        emit snapshotFailed(mediaPath, "file not found");
        return;
    }
    if (m_mem.contains(mediaPath)) {
        emit snapshotReady(mediaPath, m_mem.value(mediaPath));
        return;
    }

    // 이미지: 직접 로드
    if (isImageFile(absPath)) {
        QImageReader reader(absPath);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            emit snapshotFailed(mediaPath, "image load failed: " + reader.errorString());
            return;
        }
        finishWithImage(mediaPath, scaledToMax(img, m_maxDim));
        return;
    }

    // 영상: 디스크 캐시 확인
    const QString key      = cacheKey(absPath);
    const QString diskFile = cacheFilePath(key);
    if (QFileInfo::exists(diskFile)) {
        QImage img(diskFile);
        if (!img.isNull()) {
            finishWithImage(mediaPath, img);
            return;
        }
    }

    if (m_ffmpeg.isEmpty()) {
        emit snapshotFailed(mediaPath, "ffmpeg not available");
        return;
    }
    if (m_inFlight.value(mediaPath, false)) {
        return;  // 이미 추출 진행중
    }

    auto* job   = new Job;
    job->media   = mediaPath;
    job->absPath = absPath;
    job->key     = key;
    job->outFile = diskFile;
    job->attempt = 0;
    m_inFlight[mediaPath] = true;
    startFfmpeg(job, 1.0);
}

// ---- internal -------------------------------------------------
void SnapshotCache::finishWithImage(const QString& media, const QImage& img) {
    m_mem.insert(media, img);
    qInfo() << "SnapshotCache: snapshotReady" << media
            << img.width() << "x" << img.height();
    emit snapshotReady(media, img);
}

void SnapshotCache::startFfmpeg(Job* job, double seekSeconds) {
    auto* proc = new QProcess(this);
    job->proc = proc;
    m_jobs.insert(proc, job);

    // ffmpeg 는 stderr 로 매우 장황하게 출력한다. QProcess 파이프를 읽지
    // 않으면 버퍼가 가득 차 ffmpeg 가 write 에서 데드락된다.
    // → 출력 최소화 + 채널을 null 로 폐기 + stdin 비활성(과거 캐시 덮어쓰기
    //   확인 프롬프트 방지).
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());

    const QStringList args = {
        "-y",
        "-nostdin",
        "-loglevel", "error",
        "-ss", QString::number(seekSeconds, 'f', 3),
        "-i",  job->absPath,
        "-frames:v", "1",
        "-update", "1",
        job->outFile
    };

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SnapshotCache::onProcessFinished);
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        // start 실패 등 — finished 가 안 올 수 있으므로 별도 처리
        Job* j = m_jobs.value(proc, nullptr);
        if (!j) return;
        const QString media = j->media;
        m_inFlight.remove(media);
        m_jobs.remove(proc);
        delete j;
        proc->deleteLater();
        emit snapshotFailed(media, "ffmpeg failed to start");
    });

    qInfo() << "SnapshotCache: ffmpeg args:" << args.join(' ');
    qInfo() << "SnapshotCache: outFile =" << job->outFile;
    proc->start(m_ffmpeg, args);
}

void SnapshotCache::onProcessFinished(int exitCode, int exitStatus) {
    auto* proc = qobject_cast<QProcess*>(sender());
    if (!proc) return;
    Job* job = m_jobs.value(proc, nullptr);
    if (!job) return;

    qInfo() << "SnapshotCache: ffmpeg finished exitCode=" << exitCode
            << "status=" << exitStatus
            << "outSize=" << QFileInfo(job->outFile).size();

    QImage img;
    const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0
                     && QFileInfo(job->outFile).size() > 0);
    if (ok) {
        img = QImage(job->outFile);
    }

    if (img.isNull()) {
        if (job->attempt == 0) {
            // 1초 지점 실패(영상 길이 < 1s 등) → 첫 프레임 재시도
            ++job->attempt;
            m_jobs.remove(proc);
            proc->deleteLater();
            startFfmpeg(job, 0.0);
            return;
        }
        const QString media = job->media;
        m_inFlight.remove(media);
        m_jobs.remove(proc);
        proc->deleteLater();
        delete job;
        emit snapshotFailed(media, "ffmpeg produced no frame");
        return;
    }

    const QString media   = job->media;
    const QImage  scaled  = scaledToMax(img, m_maxDim);
    if (scaled.size() != img.size()) {
        scaled.save(job->outFile, "PNG");  // 다운스케일본으로 캐시 갱신
    }
    m_inFlight.remove(media);
    m_jobs.remove(proc);
    proc->deleteLater();
    delete job;
    finishWithImage(media, scaled);
}

} // namespace uwp
