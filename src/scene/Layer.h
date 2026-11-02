#pragma once

#include <QString>
#include <QRectF>
#include <QMetaType>

namespace uwp {

enum class MediaType { Video, Image, Document, Text, Clock, Unknown };
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

    // ---- Text 위젯 전용 필드 (mediaType == Text 일 때만 유효) ----
    // 참고: CMS_WebServer_MultiTenant/templates/subtitle.html 의 subtitle 위젯
    // 속성 세트를 참조. 정적 텍스트 우선(마키 스크롤은 v2).
    QString   text;                        // 표시 문자열 (여러 줄 가능)
    QString   textColor      = "#ffffff";  // Foreground
    int       fontSize       = 48;         // pt
    QString   fontFamily     = "맑은 고딕";
    int       fontWeight     = 700;        // 400=Regular, 700=Bold
    int       textAlign      = 1;          // 0=Left, 1=Center, 2=Right
    int       textVAlign     = 1;          // 0=Top,  1=Center, 2=Bottom
    QString   bgColor        = "#000000";  // 배경색
    double    bgOpacity      = 0.0;        // 0.0(투명) ~ 1.0
    int       padding        = 8;          // px (신규 텍스트 레이어 기본값)

    // 마키(가로 스크롤). scrollEnabled=true 면 텍스트를 우→좌로 무한 반복.
    //   scrollSpeed = px/sec 씬 좌표. 0 이면 사실상 정지.
    bool      scrollEnabled  = false;
    int       scrollSpeed    = 120;        // 웨딩홀 LED 자막 관용 속도

    // Clock 위젯 전용 — QDateTime::toString 포맷.
    //   기본 "HH:mm" (24시간). 흔한 대안: "HH:mm:ss", "yyyy-MM-dd HH:mm",
    //   "A hh:mm" (오전/오후) 등. Text 필드는 렌더 시 이 포맷으로 재계산.
    QString   timeFormat     = "HH:mm";
};

} // namespace uwp

// EndAction 을 QVariant(userData)에 담기 위해 Qt 메타시스템에 등록.
// (PropertyPanel 콤보박스가 한글 표시텍스트와 enum 값을 분리 보관)
Q_DECLARE_METATYPE(uwp::EndAction)
