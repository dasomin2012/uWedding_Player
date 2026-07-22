#pragma once

#include <QWidget>

#include "scene/Layer.h"

namespace uwp {

// Live 측 텍스트 레이어. 이미지·비디오 위젯들과 동일하게 네이티브 윈도우
// (WA_NativeWindow) 로 만들어 z-order/포커스 규칙을 통일.
// Layer 의 text/textColor/fontSize/fontFamily/fontWeight/textAlign/textVAlign/
// bgColor/bgOpacity/padding 을 그대로 반영해 페인트한다.  Preview 캔버스의
// QPainter 렌더 로직과 시각적으로 동일해, 편집 결과가 그대로 송출됨.
class TextWidget : public QWidget {
    Q_OBJECT
public:
    explicit TextWidget(const Layer& layer, QWidget* parent = nullptr);

    // 재편집 없이 새로 만들어 붙이는 스타일이지만, 필요 시 setter 로도 갱신 가능.
    void setLayer(const Layer& layer);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    Layer m_layer;   // 값 복사 스냅샷 — Live 는 편집과 분리된 정지 상태
};

} // namespace uwp
