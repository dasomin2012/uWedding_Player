#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QString>
#include <QPixmap>

class QGraphicsScene;
class QGraphicsRectItem;

namespace uwp {

class SceneModel;
class SnapshotCache;
class LayerItem;
class InlineTextEditor;

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

    // 현재 씬(스테이지=논리 캔버스 영역)을 size 픽스맵으로 렌더 (Program 썸네일용).
    QPixmap renderThumbnail(const QSize& size) const;

    // 정렬 스냅 가이드 (LayerItem 드래그 중 호출).  xs/ys 는 씬 좌표.
    void showSnapGuides(const QList<qreal>& xs, const QList<qreal>& ys);
    void clearSnapGuides();

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;   // Del → 선택 레이어 삭제

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
    // 텍스트 레이어 캔버스 내 인라인 편집.
    void beginTextEdit(const QString& layerId);
    void endTextEdit(bool commit);

    SceneModel*                 m_model     = nullptr;
    SnapshotCache*              m_snapshots = nullptr;
    QGraphicsScene*             m_scene     = nullptr;
    QGraphicsRectItem*          m_stage     = nullptr;
    QHash<QString, LayerItem*>  m_items;    // layerId -> item
    bool                        m_syncingSel = false;
    // 인라인 텍스트 편집기 — 씬 원생 QGraphicsTextItem 을 사용해 뷰 스케일에
    // 관계없이 LayerItem 과 동일 폰트 크기로 렌더. 편집 완료(포커스 이탈/Esc)
    // 시 숨김. 배경은 별도 QGraphicsRectItem 으로 뒷단에 그린다.
    InlineTextEditor*           m_editText = nullptr;
    QGraphicsRectItem*          m_editBg   = nullptr;
    QString                     m_editingTextId;

    // 정렬 스냅 가이드 (드래그 중 활성). drawForeground 에서 씬 좌표 dashed line 으로 렌더.
    QList<qreal>                m_guideXs;
    QList<qreal>                m_guideYs;
};

} // namespace uwp
