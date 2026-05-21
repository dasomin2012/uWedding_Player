#include "Settings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace uwp {

bool Settings::load(const QString& path) {
    QFile f(path);
    if (!f.exists()) {
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "Settings::load: cannot open" << path;
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "Settings::load: parse error -" << err.errorString();
        return false;
    }

    const QJsonObject root   = doc.object();
    m_version                = root.value("version").toString(m_version);

    const QJsonObject s      = root.value("settings").toObject();
    const QJsonObject canvas = s.value("canvas").toObject();
    m_canvasWidth            = canvas.value("width").toInt(m_canvasWidth);
    m_canvasHeight           = canvas.value("height").toInt(m_canvasHeight);

    m_outputMonitorIndex     = s.value("output_monitor_index").toInt(m_outputMonitorIndex);
    m_outputRenderMode       = s.value("output_render_mode").toString(m_outputRenderMode);
    m_takeDefaultMode        = s.value("take_default_mode").toString(m_takeDefaultMode);
    m_takeFadeDurationMs     = s.value("take_fade_duration_ms").toInt(m_takeFadeDurationMs);
    m_testVideoPath          = s.value("test_video_path").toString(m_testVideoPath);
    m_ffmpegPath             = s.value("ffmpeg_path").toString(m_ffmpegPath);
    m_snapshotCacheDir       = s.value("snapshot_cache_dir").toString(m_snapshotCacheDir);
    m_mediaDir               = s.value("media_dir").toString(m_mediaDir);
    m_sceneScratch           = s.value("scene_scratch").toString(m_sceneScratch);

    const QJsonObject nova   = s.value("novastar").toObject();
    m_novaStar.enabled         = nova.value("enabled").toBool(m_novaStar.enabled);
    m_novaStar.host            = nova.value("host").toString(m_novaStar.host);
    m_novaStar.port            = nova.value("port").toInt(m_novaStar.port);
    m_novaStar.protocol        = nova.value("protocol").toString(m_novaStar.protocol);
    m_novaStar.defaultPresetId = nova.value("default_preset_id")
                                     .toString(m_novaStar.defaultPresetId);

    m_engine                 = s.value("engine").toString(m_engine);

    const QJsonObject obs    = s.value("obs").toObject();
    m_obs.exePath            = obs.value("exe_path").toString(m_obs.exePath);
    m_obs.wsUrl              = obs.value("ws_url").toString(m_obs.wsUrl);
    m_obs.wsPassword         = obs.value("ws_password").toString(m_obs.wsPassword);
    m_obs.sceneA             = obs.value("scene_a").toString(m_obs.sceneA);
    m_obs.sceneB             = obs.value("scene_b").toString(m_obs.sceneB);
    m_obs.projectorMonitor   = obs.value("projector_monitor").toInt(m_obs.projectorMonitor);

    return true;
}

bool Settings::save(const QString& path) const {
    QJsonObject canvas;
    canvas["width"]  = m_canvasWidth;
    canvas["height"] = m_canvasHeight;

    QJsonObject nova;
    nova["enabled"]           = m_novaStar.enabled;
    nova["host"]              = m_novaStar.host;
    nova["port"]              = m_novaStar.port;
    nova["protocol"]          = m_novaStar.protocol;
    nova["default_preset_id"] = m_novaStar.defaultPresetId;

    QJsonObject obs;
    obs["exe_path"]          = m_obs.exePath;
    obs["ws_url"]            = m_obs.wsUrl;
    obs["ws_password"]       = m_obs.wsPassword;
    obs["scene_a"]           = m_obs.sceneA;
    obs["scene_b"]           = m_obs.sceneB;
    obs["projector_monitor"] = m_obs.projectorMonitor;

    QJsonObject s;
    s["canvas"]                  = canvas;
    s["output_monitor_index"]    = m_outputMonitorIndex;
    s["output_render_mode"]      = m_outputRenderMode;
    s["take_default_mode"]       = m_takeDefaultMode;
    s["take_fade_duration_ms"]   = m_takeFadeDurationMs;
    s["test_video_path"]         = m_testVideoPath;
    s["ffmpeg_path"]             = m_ffmpegPath;
    s["snapshot_cache_dir"]      = m_snapshotCacheDir;
    s["media_dir"]               = m_mediaDir;
    s["scene_scratch"]           = m_sceneScratch;
    s["novastar"]                = nova;
    s["engine"]                  = m_engine;
    s["obs"]                     = obs;

    QJsonObject root;
    root["version"]  = m_version;
    root["settings"] = s;
    root["programs"] = QJsonArray{};  // Phase 1: 빈 배열

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Settings::save: cannot write" << path;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void Settings::setCanvasSize(int w, int h) {
    m_canvasWidth  = w;
    m_canvasHeight = h;
}

void Settings::setOutputMonitorIndex(int idx) {
    m_outputMonitorIndex = idx;
}

} // namespace uwp
