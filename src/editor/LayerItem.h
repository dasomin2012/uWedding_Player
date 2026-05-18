#pragma once

#include <QGraphicsObject>
#include <QPixmap>
#include <QRectF>

namespace uwp {

class SceneModel;

// 한 Layer 의 시각 표현.
//  - SnapshotCache 정지화상을 geometry 에 맞춰 렌더 (Preview=정지화상)
//  - 선택 시 8핸들 사각 리사이즈 + 본체 이동
//  - 사용자 조작 종료 시 SceneModel 에 geometry 기록
// 위치 = layer.geometry.topLeft(), boundingRect = (0,0,w,h) 로컬.
class LayerItem : public QGraphicsObject {
    Q_OBJECT
public:
    LayerItem(SceneModel* model, const QString& id, QGraphicsItem* parent = nullptr);

    QString layerId() const { return m_id; }

    // 모델 → 아이템 (피드백 루프 방지: 내부 플래그로 writeback 억제)
    void applyGeometry(const QRectF& sceneRect);
    void applyOpacity(double op);
    void setSnapshot(const QPixmap& pm);

    QRectF boundingRect() const override;
    void   paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    enum Handle { None, TL, T, TR, R, BR, B, BL, L, Body };

    double viewScale() const;            // 화면 픽셀↔씬 환산
    double handleSpan() const;           // 핸들 한 변(씬 단위)
    Handle hitTest(const QPointF& local) const;
    QRectF handleRect(Handle h) const;
    void   commitToModel();

    SceneModel* m_model   = nullptr;
    QString     m_id;
    qreal       m_w = 100.0;
    qreal       m_h = 100.0;
    QPixmap     m_pixmap;
    bool        m_applying = false;      // 모델→아이템 적용중

    Handle      m_drag = None;
    QPointF     m_dragStartScene;
    QRectF      m_startSceneRect;
};

} // namespace uwp
