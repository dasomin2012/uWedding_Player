#include "SceneSerializer.h"

#include "SceneModel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace uwp {

QJsonArray SceneSerializer::layersToJson(const QVector<Layer>& layers) {
    QJsonArray arr;
    for (const Layer& l : layers) {
        QJsonObject g;
        g["x"] = l.geometry.x();
        g["y"] = l.geometry.y();
        g["w"] = l.geometry.width();
        g["h"] = l.geometry.height();

        QJsonObject o;
        o["id"]               = l.id;
        o["media"]            = l.media;
        o["media_type"]       = mediaTypeToString(l.mediaType);
        o["geometry"]         = g;
        o["z_index"]          = l.zIndex;
        o["opacity"]          = l.opacity;
        o["display_time_sec"] = l.displayTimeSec;
        o["end_action"]       = endActionToString(l.endAction);
        o["name"]             = l.name;
        arr.append(o);
    }
    return arr;
}

QVector<Layer> SceneSerializer::layersFromJson(const QJsonArray& arr) {
    QVector<Layer> out;
    out.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Layer l;
        l.id        = o.value("id").toString();
        l.media     = o.value("media").toString();
        l.mediaType = o.contains("media_type")
                          ? mediaTypeFromString(o.value("media_type").toString())
                          : guessMediaType(l.media);
        const QJsonObject g = o.value("geometry").toObject();
        l.geometry = QRectF(g.value("x").toDouble(), g.value("y").toDouble(),
                            g.value("w").toDouble(), g.value("h").toDouble());
        l.zIndex         = o.value("z_index").toInt();
        l.opacity        = o.value("opacity").toDouble(1.0);
        l.displayTimeSec = o.value("display_time_sec").toInt(0);
        l.endAction      = endActionFromString(o.value("end_action").toString());
        l.name           = o.value("name").toString();
        if (l.id.isEmpty() || l.media.isEmpty()) continue;
        out.push_back(l);
    }
    return out;
}

bool SceneSerializer::saveScene(const SceneModel& model, const QString& path) {
    QJsonObject canvas;
    canvas["w"] = model.canvasSize().width();
    canvas["h"] = model.canvasSize().height();

    QJsonObject root;
    root["version"] = "1.0.0";
    root["canvas"]  = canvas;
    root["layers"]  = layersToJson(model.layers());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "SceneSerializer::saveScene: cannot write" << path;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    qInfo() << "SceneSerializer: saved" << model.layers().size()
            << "layers ->" << path;
    return true;
}

bool SceneSerializer::loadScene(SceneModel& model, const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "SceneSerializer::loadScene: parse error -"
                   << err.errorString();
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject c    = root.value("canvas").toObject();
    if (c.contains("w") && c.contains("h")) {
        model.setCanvasSize(QSize(c.value("w").toInt(), c.value("h").toInt()));
    }
    model.replaceAll(layersFromJson(root.value("layers").toArray()));
    qInfo() << "SceneSerializer: loaded" << model.layers().size()
            << "layers <-" << path;
    return true;
}

} // namespace uwp
