#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QSize>

#include "Layer.h"

namespace uwp {

// 씬(편집중 프리셋)의 단일 진실 소스.
// 모든 뷰(PreviewCanvas/PropertyPanel/MediaList)는 이 모델만 보고 동기화한다.
// m_layers 는 항상 zIndex 오름차순 정렬 유지.
class SceneModel : public QObject {
    Q_OBJECT
public:
    explicit SceneModel(QObject* parent = nullptr);

    void  setCanvasSize(const QSize& s);
    QSize canvasSize() const { return m_canvas; }

    // 반환: 새 레이어 id
    QString addLayer(const QString& mediaPath, const QRectF& geometry);
    void    removeLayer(const QString& id);
    void    clear();

    int                   layerCount() const { return m_layers.size(); }
    const QVector<Layer>& layers()     const { return m_layers; }
    const Layer*          layer(const QString& id) const;

    void setGeometry(const QString& id, const QRectF& g);
    void setOpacity(const QString& id, double op);
    void setDisplayTime(const QString& id, int sec);
    void setEndAction(const QString& id, EndAction a);
    void setName(const QString& id, const QString& name);

    // z-order (정렬 + 연속 zIndex 재배치)
    void raise(const QString& id);
    void lower(const QString& id);
    void toFront(const QString& id);
    void toBack(const QString& id);

    // 선택 ("" = 해제)
    void    select(const QString& id);
    QString selectedId() const { return m_selected; }

    // 직렬화용 일괄 교체 (SceneSerializer 가 사용)
    void replaceAll(const QVector<Layer>& layers);

signals:
    void layerAdded(const QString& id);
    void layerRemoved(const QString& id);
    void layerChanged(const QString& id);   // geometry/opacity/... 변경
    void zOrderChanged();                    // 순서 변경 → 뷰가 zValue 재적용
    void selectionChanged(const QString& id);
    void sceneReset();                       // clear / replaceAll

private:
    int  indexOf(const QString& id) const;
    void sortByZ();
    void normalizeZ();                       // 0..n-1 연속 재배치

    QSize          m_canvas{1920, 1080};
    QVector<Layer> m_layers;
    QString        m_selected;
    int            m_idSeq = 0;
};

} // namespace uwp
