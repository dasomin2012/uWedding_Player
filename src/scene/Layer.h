#pragma once

#include <QString>
#include <QRectF>
#include <QMetaType>

namespace uwp {

enum class MediaType { Video, Image, Document, Unknown };
enum class EndAction { Loop, Stop, Hold, Next, First };

QString   mediaTypeToString(MediaType t);
MediaType mediaTypeFromString(const QString& s);
MediaType guessMediaType(const QString& path);   // 확장자 기반

QString   endActionToString(EndAction a);
EndAction endActionFromString(const QString& s);

// 하나의 미디어 배치 단위. CLAUDE.md setup.json 레이어 스키마와 1:1.
// 순수 데이터 레코드이므로 멤버를 공개(아래 SceneModel 이 캡슐화 담당).
struct Layer {
    QString   id;
    QString   media;                       // 경로
    MediaType mediaType      = MediaType::Unknown;
    QRectF    geometry;                    // 논리 캔버스 좌표 (x,y,w,h)
    int       zIndex         = 0;
    double    opacity        = 1.0;        // 0.0 ~ 1.0
    int       displayTimeSec = 0;          // 0 = Take 까지 무한
    EndAction endAction      = EndAction::Loop;
    QString   name;                        // UI 표시용
};

} // namespace uwp

// EndAction 을 QVariant(userData)에 담기 위해 Qt 메타시스템에 등록.
// (PropertyPanel 콤보박스가 한글 표시텍스트와 enum 값을 분리 보관)
Q_DECLARE_METATYPE(uwp::EndAction)
