#include "Layer.h"

#include <QFileInfo>

namespace uwp {

QString mediaTypeToString(MediaType t) {
    switch (t) {
        case MediaType::Video:    return "video";
        case MediaType::Image:    return "image";
        case MediaType::Document: return "document";
        default:                  return "unknown";
    }
}

MediaType mediaTypeFromString(const QString& s) {
    const QString v = s.toLower();
    if (v == "video")    return MediaType::Video;
    if (v == "image")    return MediaType::Image;
    if (v == "document") return MediaType::Document;
    return MediaType::Unknown;
}

MediaType guessMediaType(const QString& path) {
    static const QStringList kVideo = {
        "mp4", "mov", "avi", "mkv", "wmv", "m4v", "webm", "mpg", "mpeg", "flv"
    };
    static const QStringList kImage = {
        "png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"
    };
    static const QStringList kDoc = { "pdf", "ppt", "pptx" };

    const QString ext = QFileInfo(path).suffix().toLower();
    if (kVideo.contains(ext)) return MediaType::Video;
    if (kImage.contains(ext)) return MediaType::Image;
    if (kDoc.contains(ext))   return MediaType::Document;
    return MediaType::Unknown;
}

QString endActionToString(EndAction a) {
    switch (a) {
        case EndAction::Loop:  return "loop";
        case EndAction::Stop:  return "stop";
        case EndAction::Hold:  return "hold";
        case EndAction::Next:  return "next";
        case EndAction::First: return "first";
    }
    return "loop";
}

EndAction endActionFromString(const QString& s) {
    const QString v = s.toLower();
    if (v == "stop")  return EndAction::Stop;
    if (v == "hold")  return EndAction::Hold;
    if (v == "next")  return EndAction::Next;
    if (v == "first") return EndAction::First;
    return EndAction::Loop;
}

} // namespace uwp
