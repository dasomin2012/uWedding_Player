#include "PreviewCanvas.h"

#include "LayerItem.h"
#include "scene/SceneModel.h"
#include "player/SnapshotCache.h"

#include <QEvent>
#include <QFocusEvent>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>

#include <functional>

namespace uwp {

static const char* kMediaMime = "application/x-uwp-media-path";

// 씬 원생 텍스트 편집기 — QPlainTextEdit + QGraphicsProxyWidget 은 뷰 스케일
// 아래에서 폰트가 축소 렌더돼 편집 중 텍스트가 사라진 것처럼 보이는 회귀가
// 있다. QGraphicsTextItem 은 씬 좌표계에서 QPainter 로 직접 그리므로
// LayerItem 과 동일한 스케일·폰트 로 렌더된다.
class InlineTextEditor : public QGraphicsTextItem {
public:
    InlineTextEditor() = default;
    std::function<void()> onCommit;
    std::function<void()> onCancel;

protected:
    void focusOutEvent(QFocusEvent* e) override {
        QGraphicsTextItem::focusOutEvent(e);
        if (onCommit) onCommit();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            if (onCancel) onCancel();
            return;
        }
        QGraphicsTextItem::keyPressEvent(e);
    }
};

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
    // 우리 LayerItem 은 boundingRect 에 8-핸들 마진을 포함하고 selection
    // 상태에 따라 painted 영역이 변한다. Qt 의 dirty-rect 최적화(Minimal/Smart)
    // 는 이런 케이스에서 이전 프레임 픽셀을 남기는 회귀가 관찰됨. 편집용
    // 캔버스라 CPU 여유가 있어 FullViewportUpdate 로 안전 우선.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

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

QPixmap PreviewCanvas::renderThumbnail(const QSize& size) const {
    QPixmap pm(size);
    pm.fill(Qt::black);
    if (!m_scene) return pm;

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // 스테이지(= 논리 캔버스 영역, Preview 정지화상)를 픽스맵에 꽉 채워 렌더.
    const QRectF src = m_scene->sceneRect();
    m_scene->render(&p, QRectF(QPointF(0, 0), QSizeF(size)), src,
                    Qt::IgnoreAspectRatio);
    p.end();
    return pm;
}

void PreviewCanvas::drawBackground(QPainter* p, const QRectF& rect) {
    p->fillRect(rect, QColor(32, 32, 36));   // 스테이지 밖
    QGraphicsView::drawBackground(p, rect);
}

void PreviewCanvas::drawForeground(QPainter* p, const QRectF& rect) {
    QGraphicsView::drawForeground(p, rect);
    if (m_guideXs.isEmpty() && m_guideYs.isEmpty()) return;
    p->save();
    QPen pen(QColor(0xf5, 0x9e, 0x0b));   // 앰버 — pill 색과 통일
    pen.setCosmetic(true);                 // 뷰 스케일 무관 1px 두께
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    p->setPen(pen);
    for (qreal x : m_guideXs)
        p->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (qreal y : m_guideYs)
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    p->restore();
}

void PreviewCanvas::showSnapGuides(const QList<qreal>& xs,
                                    const QList<qreal>& ys) {
    if (m_guideXs == xs && m_guideYs == ys) return;
    m_guideXs = xs;
    m_guideYs = ys;
    viewport()->update();
}

void PreviewCanvas::clearSnapGuides() {
    if (m_guideXs.isEmpty() && m_guideYs.isEmpty()) return;
    m_guideXs.clear();
    m_guideYs.clear();
    viewport()->update();
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
    // 인라인 편집 중 다른 곳 클릭 → 편집 확정. (편집기 아이템 자체 클릭은
    // 편집기가 먼저 잡고 focusOut 이 발생하지 않으므로 여기서 확정하지 않음.)
    if (m_editText && m_editText->isVisible()) {
        const QGraphicsItem* hit = itemAt(e->pos());
        if (hit != m_editText && hit != m_editBg) {
            endTextEdit(true);
        }
    }
    QGraphicsView::mousePressEvent(e);
    // 빈 영역(스테이지/배경) 클릭 → 선택 해제
    if (!e->isAccepted() || itemAt(e->pos()) == m_stage || itemAt(e->pos()) == nullptr) {
        m_model->select(QString());
    }
}

void PreviewCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    // 텍스트 레이어 더블클릭 → 캔버스 내 인라인 편집 시작.
    //   대상 아이템이 m_items 중 하나이고, 해당 Layer 가 Text 타입이면 진입.
    if (QGraphicsItem* it = itemAt(e->pos())) {
        for (auto iter = m_items.begin(); iter != m_items.end(); ++iter) {
            if (static_cast<QGraphicsItem*>(iter.value()) == it) {
                const Layer* l = m_model->layer(iter.key());
                if (l && l->mediaType == MediaType::Text) {
                    beginTextEdit(iter.key());
                    e->accept();
                    return;
                }
                break;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void PreviewCanvas::beginTextEdit(const QString& layerId) {
    const Layer* l = m_model->layer(layerId);
    if (!l) return;

    // 최초 진입 시 씬 아이템 생성. bg 는 QGraphicsRectItem, 텍스트는
    // QGraphicsTextItem 서브클래스(InlineTextEditor) — 씬 원생 렌더로
    // 뷰 스케일에 관계없이 LayerItem 과 동일한 폰트로 표시.
    if (!m_editBg) {
        m_editBg = m_scene->addRect(QRectF(), QPen(QColor("#f59e0b"), 2));
        m_editBg->setZValue(1e6);
        m_editBg->setVisible(false);
    }
    if (!m_editText) {
        m_editText = new InlineTextEditor;
        m_editText->onCommit = [this]{ endTextEdit(true);  };
        m_editText->onCancel = [this]{ endTextEdit(false); };
        m_editText->setTextInteractionFlags(Qt::TextEditorInteraction);
        m_editText->setZValue(1e6 + 1);
        m_scene->addItem(m_editText);
        m_editText->document()->setDocumentMargin(0);
        m_editText->setVisible(false);
    }

    m_editingTextId = layerId;

    // 배경 rect — 레이어 geometry 전체에 채움 + 앰버 테두리(편집 중임 표시).
    QColor bg(l->bgColor);
    bg.setAlphaF(qBound(0.0, l->bgOpacity, 1.0));
    m_editBg->setRect(l->geometry);
    m_editBg->setBrush(bg);
    m_editBg->setVisible(true);

    // 텍스트 아이템 — 폰트/색/정렬을 레이어와 동일하게.
    QFont f(l->fontFamily);
    f.setPixelSize(qMax(1, l->fontSize));
    f.setWeight(l->fontWeight >= 700 ? QFont::Bold : QFont::Normal);
    m_editText->setFont(f);
    m_editText->setDefaultTextColor(QColor(l->textColor));

    QTextOption opt = m_editText->document()->defaultTextOption();
    switch (l->textAlign) {
        case 0: opt.setAlignment(Qt::AlignLeft);    break;
        case 2: opt.setAlignment(Qt::AlignRight);   break;
        default: opt.setAlignment(Qt::AlignHCenter);
    }
    m_editText->document()->setDefaultTextOption(opt);
    m_editText->setPlainText(l->text);

    // 위치: 레이어 (x,y) + padding, 텍스트 폭은 레이어 폭 - 좌우 padding.
    const int pad = qMax(0, l->padding);
    m_editText->setTextWidth(qMax(1.0, l->geometry.width() - 2.0 * pad));
    m_editText->setPos(l->geometry.x() + pad, l->geometry.y() + pad);
    m_editText->setVisible(true);

    // 하위 LayerItem 은 이중 렌더 방지 위해 숨김.
    if (auto* it = m_items.value(layerId, nullptr))
        it->setVisible(false);

    m_editText->setFocus(Qt::MouseFocusReason);
    // 모든 텍스트 선택 — 기본값("텍스트를 입력하세요")을 즉시 대체 편집.
    QTextCursor cursor(m_editText->document());
    cursor.select(QTextCursor::Document);
    m_editText->setTextCursor(cursor);
}

void PreviewCanvas::keyPressEvent(QKeyEvent* e) {
    // 인라인 텍스트 편집 중엔 Del 이 문자 삭제 (편집기가 우선 처리).
    //   편집기 아이템이 포커스를 갖고 있으면 QGraphicsView 기본 dispatch 가
    //   그 아이템에 전달하므로 여기서 별도 처리 불필요.
    if (m_editingTextId.isEmpty()
        && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        const QString id = m_model ? m_model->selectedId() : QString();
        if (!id.isEmpty()) {
            m_model->removeLayer(id);
            e->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(e);
}

void PreviewCanvas::endTextEdit(bool commit) {
    if (m_editingTextId.isEmpty()) return;
    if (commit && m_editText) {
        m_model->setText(m_editingTextId, m_editText->toPlainText());
    }
    if (auto* it = m_items.value(m_editingTextId, nullptr))
        it->setVisible(true);
    if (m_editText) m_editText->setVisible(false);
    if (m_editBg)   m_editBg->setVisible(false);
    m_editingTextId.clear();
    setFocus(Qt::MouseFocusReason);
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
    // 기본 16:9 — MediaListWidget 더블클릭 경로와 일치.
    const qreal w = cs.width() * 0.4;
    const qreal h = w * 9.0 / 16.0;
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

    // Text 레이어는 파일 없음 → SnapshotCache 요청 스킵. paint() 가 QPainter 로 직접 렌더.
    if (m_snapshots && l->mediaType != MediaType::Text && !l->media.isEmpty()) {
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
    // Text 필드(내용·글꼴·색·정렬 등)는 paint() 가 매번 모델을 다시 읽으므로
    // geometry/opacity 변화가 없어도 명시적 update() 로 리페인트 트리거.
    if (l->mediaType == MediaType::Text) it->update();
}

void PreviewCanvas::onZOrderChanged() {
    for (const Layer& l : m_model->layers())
        if (auto* it = m_items.value(l.id, nullptr))
            it->setZValue(l.zIndex);
}

void PreviewCanvas::onSelectionChanged(const QString& id) {
    if (m_syncingSel) return;
    m_syncingSel = true;
    // 원자적 초기화 후 대상 하나만 선택 — 씬의 내부 selection 리스트가
    // loop 갱신 시 stale 항목을 남기는 문제를 방지 (multi-select bug).
    m_scene->clearSelection();
    if (!id.isEmpty()) {
        if (auto* it = m_items.value(id, nullptr))
            it->setSelected(true);
    }
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
