#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QString>

class QGraphicsScene;
class QGraphicsRectItem;

namespace uwp {

class SceneModel;
class SnapshotCache;
class LayerItem;

// 논리 캔버스(Settings 크기)를 씬 좌표로 그리는 편집 뷰.
//  - 검은 스테이지(LED 경계) + fit/zoom
//  - SceneModel 시그널에 반응해 LayerItem 생성/갱신/삭제
//  - SnapshotCache 결과를 경로 매칭하여 해당 LayerItem 에 반영
//  - 미디어 드롭(파일 URL / MediaList 커스텀 MIME) → 레이어 추가
class PreviewCanvas : public QGraphicsView {
    Q_OBJECT
public:
    PreviewCanvas(SceneModel* model, SnapshotCache* snapshots,
                  QWidget* parent = nullptr);

    void fitStage();

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onLayerAdded(const QString& id);
    void onLayerRemoved(const QString& id);
    void onLayerChanged(const QString& id);
    void onZOrderChanged();
    void onSelectionChanged(const QString& id);
    void onSceneReset();
    void onSnapshotReady(const QString& mediaPath, const QImage& image);

private:
    void rebuildFromModel();
    void syncCanvasRect();
    void addMediaAt(const QString& path, const QPointF& scenePos);

    SceneModel*                 m_model     = nullptr;
    SnapshotCache*              m_snapshots = nullptr;
    QGraphicsScene*             m_scene     = nullptr;
    QGraphicsRectItem*          m_stage     = nullptr;
    QHash<QString, LayerItem*>  m_items;    // layerId -> item
    bool                        m_syncingSel = false;
};

} // namespace uwp
