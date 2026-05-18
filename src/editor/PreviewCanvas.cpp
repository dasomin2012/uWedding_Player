#include "PreviewCanvas.h"

#include "LayerItem.h"
#include "scene/SceneModel.h"
#include "player/SnapshotCache.h"

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>

namespace uwp {

static const char* kMediaMime = "application/x-uwp-media-path";

PreviewCanvas::PreviewCanvas(SceneModel* model, SnapshotCache* snapshots,
                             QWidget* parent)
    : QGraphicsView(parent)
    , m_model(model)
    , m_snapshots(snapshots)
{
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_stage = m_scene->addRect(QRectF(), QPen(QColor(70, 70, 70)),
                               QBrush(Qt::black));
    m_stage->setZValue(-1000);
    m_stage->setFlag(QGraphicsItem::ItemIsSelectable, false);

    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setAcceptDrops(true);
    setFrameShape(QFrame::Box);
    setMinimumSize(360, 200);

    syncCanvasRect();

    connect(m_model, &SceneModel::layerAdded,       this, &PreviewCanvas::onLayerAdded);
    connect(m_model, &SceneModel::layerRemoved,     this, &PreviewCanvas::onLayerRemoved);
    connect(m_model, &SceneModel::layerChanged,     this, &PreviewCanvas::onLayerChanged);
    connect(m_model, &SceneModel::zOrderChanged,    this, &PreviewCanvas::onZOrderChanged);
    connect(m_model, &SceneModel::selectionChanged, this, &PreviewCanvas::onSelectionChanged);
    connect(m_model, &SceneModel::sceneReset,       this, &PreviewCanvas::onSceneReset);

    if (m_snapshots) {
        connect(m_snapshots, &SnapshotCache::snapshotReady,
                this, &PreviewCanvas::onSnapshotReady);
    }

    // 모델이 이미 레이어를 갖고 있을 수 있다(시작 시 스크래치 씬 선로딩).
    // 시그널 구독 이전 상태를 초기 동기화.
    rebuildFromModel();
}

void PreviewCanvas::syncCanvasRect() {
    const QSize cs = m_model->canvasSize();
    const QRectF r(0, 0, cs.width(), cs.height());
    m_scene->setSceneRect(r);
    m_stage->setRect(r);
    fitStage();
}

void PreviewCanvas::fitStage() {
    if (m_stage) fitInView(m_stage, Qt::KeepAspectRatio);
}

void PreviewCanvas::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, QColor(32, 32, 36));   // 스테이지 밖
    QGraphicsView::drawBackground(p, rect);
}

void PreviewCanvas::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    fitStage();
}

void PreviewCanvas::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        const double f = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        scale(f, f);
        e->accept();
    } else {
        QGraphicsView::wheelEvent(e);
    }
}

void PreviewCanvas::mousePressEvent(QMouseEvent* e) {
    QGraphicsView::mousePressEvent(e);
    // 빈 영역(스테이지/배경) 클릭 → 선택 해제
    if (!e->isAccepted() || itemAt(e->pos()) == m_stage || itemAt(e->pos()) == nullptr) {
        m_model->select(QString());
    }
}

// ---- drag & drop ----------------------------------------------
static QString mediaPathFromMime(const QMimeData* md) {
    if (md->hasFormat(kMediaMime))
        return QString::fromUtf8(md->data(kMediaMime));
    if (md->hasUrls() && !md->urls().isEmpty())
        return md->urls().first().toLocalFile();
    return QString();
}

void PreviewCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (!mediaPathFromMime(e->mimeData()).isEmpty()) e->acceptProposedAction();
}

void PreviewCanvas::dragMoveEvent(QDragMoveEvent* e) {
    if (!mediaPathFromMime(e->mimeData()).isEmpty()) e->acceptProposedAction();
}

void PreviewCanvas::dropEvent(QDropEvent* e) {
    const QString path = mediaPathFromMime(e->mimeData());
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    const QPointF sp = mapToScene(e->pos());   // Qt 5.15: QDropEvent::pos()
    addMediaAt(path, sp);
    e->acceptProposedAction();
}

void PreviewCanvas::addMediaAt(const QString& path, const QPointF& scenePos) {
    const QSize cs = m_model->canvasSize();
    qreal w = cs.width()  * 0.4;
    qreal h = cs.height() * 0.4;
    QRectF r(scenePos.x() - w / 2, scenePos.y() - h / 2, w, h);
    m_model->addLayer(path, r);   // → onLayerAdded
}

// ---- model -> view --------------------------------------------
void PreviewCanvas::onLayerAdded(const QString& id) {
    const Layer* l = m_model->layer(id);
    if (!l || m_items.contains(id)) return;

    auto* item = new LayerItem(m_model, id);
    m_scene->addItem(item);
    item->applyGeometry(l->geometry);
    item->applyOpacity(l->opacity);
    item->setZValue(l->zIndex);
    m_items.insert(id, item);

    if (m_snapshots) {
        const QImage c = m_snapshots->cached(l->media);
        if (!c.isNull()) item->setSnapshot(QPixmap::fromImage(c));
        else             m_snapshots->request(l->media);
    }
}

void PreviewCanvas::onLayerRemoved(const QString& id) {
    if (auto* it = m_items.take(id)) {
        m_scene->removeItem(it);
        delete it;
    }
}

void PreviewCanvas::onLayerChanged(const QString& id) {
    LayerItem* it = m_items.value(id, nullptr);
    const Layer* l = m_model->layer(id);
    if (!it || !l) return;
    it->applyGeometry(l->geometry);
    it->applyOpacity(l->opacity);
}

void PreviewCanvas::onZOrderChanged() {
    for (const Layer& l : m_model->layers())
        if (auto* it = m_items.value(l.id, nullptr))
            it->setZValue(l.zIndex);
}

void PreviewCanvas::onSelectionChanged(const QString& id) {
    if (m_syncingSel) return;
    m_syncingSel = true;
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        it.value()->setSelected(it.key() == id);
    m_syncingSel = false;
}

void PreviewCanvas::onSceneReset() {
    rebuildFromModel();
}

void PreviewCanvas::rebuildFromModel() {
    for (auto* it : m_items) { m_scene->removeItem(it); delete it; }
    m_items.clear();
    syncCanvasRect();
    for (const Layer& l : m_model->layers())
        onLayerAdded(l.id);
    qInfo() << "PreviewCanvas: rebuilt" << m_items.size() << "layer item(s)";
}

void PreviewCanvas::onSnapshotReady(const QString& mediaPath, const QImage& image) {
    const QPixmap pm = QPixmap::fromImage(image);
    for (const Layer& l : m_model->layers()) {
        if (l.media != mediaPath) continue;
        if (auto* it = m_items.value(l.id, nullptr)) it->setSnapshot(pm);
    }
}

} // namespace uwp
