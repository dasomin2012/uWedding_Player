#include "LayerItem.h"

#include "scene/SceneModel.h"

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QtMath>

namespace uwp {

LayerItem::LayerItem(SceneModel* model, const QString& id, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_model(model)
    , m_id(id)
{
    setFlag(ItemIsMovable, false);     // 이동/리사이즈는 직접 처리
    setFlag(ItemIsSelectable, true);
    setFlag(ItemSendsGeometryChanges, true);
    setAcceptHoverEvents(false);
}

// ---- 모델 → 아이템 ---------------------------------------------
void LayerItem::applyGeometry(const QRectF& sceneRect) {
    m_applying = true;
    prepareGeometryChange();
    m_w = qMax(1.0, sceneRect.width());
    m_h = qMax(1.0, sceneRect.height());
    setPos(sceneRect.topLeft());
    m_applying = false;
    update();
}

void LayerItem::applyOpacity(double op) {
    setOpacity(qBound(0.0, op, 1.0));
}

void LayerItem::setSnapshot(const QPixmap& pm) {
    m_pixmap = pm;
    update();
}

// ---- 형상 ------------------------------------------------------
QRectF LayerItem::boundingRect() const {
    const double m = handleSpan();   // 핸들이 밖으로 약간 나가므로 여유
    return QRectF(-m, -m, m_w + 2 * m, m_h + 2 * m);
}

double LayerItem::viewScale() const {
    if (scene() && !scene()->views().isEmpty()) {
        return scene()->views().first()->transform().m11();
    }
    return 1.0;
}

double LayerItem::handleSpan() const {
    const double s = viewScale();
    return (s > 0.0) ? (9.0 / s) : 9.0;   // 화면상 ~9px
}

QRectF LayerItem::handleRect(Handle h) const {
    const double s = handleSpan();
    QPointF c;
    switch (h) {
        case TL: c = {0,    0   }; break;
        case T:  c = {m_w/2,0   }; break;
        case TR: c = {m_w,  0   }; break;
        case R:  c = {m_w,  m_h/2}; break;
        case BR: c = {m_w,  m_h }; break;
        case B:  c = {m_w/2,m_h }; break;
        case BL: c = {0,    m_h }; break;
        case L:  c = {0,    m_h/2}; break;
        default: return QRectF();
    }
    return QRectF(c.x() - s/2, c.y() - s/2, s, s);
}

LayerItem::Handle LayerItem::hitTest(const QPointF& p) const {
    if (isSelected()) {
        const Handle hs[] = {TL,T,TR,R,BR,B,BL,L};
        for (Handle h : hs)
            if (handleRect(h).contains(p)) return h;
    }
    if (QRectF(0, 0, m_w, m_h).contains(p)) return Body;
    return None;
}

// ---- paint -----------------------------------------------------
void LayerItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    const QRectF box(0, 0, m_w, m_h);

    if (!m_pixmap.isNull()) {
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        p->drawPixmap(box, m_pixmap, QRectF(m_pixmap.rect()));
    } else {
        p->fillRect(box, QColor(40, 40, 48));
        p->setPen(QColor(150, 150, 160));
        p->drawText(box, Qt::AlignCenter | Qt::TextWordWrap,
                    QString("%1\n(loading…)").arg(m_id));
    }

    QPen border(isSelected() ? QColor(80, 160, 255) : QColor(90, 90, 90));
    border.setCosmetic(true);
    border.setWidth(isSelected() ? 2 : 1);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(box);

    if (isSelected()) {
        p->setBrush(QColor(80, 160, 255));
        p->setPen(Qt::NoPen);
        const Handle hs[] = {TL,T,TR,R,BR,B,BL,L};
        for (Handle h : hs) p->drawRect(handleRect(h));
    }
}

// ---- 상호작용 --------------------------------------------------
QVariant LayerItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemSelectedHasChanged && !m_applying && m_model) {
        if (isSelected() && m_model->selectedId() != m_id)
            m_model->select(m_id);
    }
    return QGraphicsObject::itemChange(change, value);
}

void LayerItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    if (e->button() != Qt::LeftButton) { e->ignore(); return; }
    if (m_model) m_model->select(m_id);
    setSelected(true);

    m_drag           = hitTest(e->pos());
    m_dragStartScene = e->scenePos();
    m_startSceneRect = QRectF(pos(), QSizeF(m_w, m_h));
    e->accept();
}

void LayerItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (m_drag == None) return;
    const QPointF d = e->scenePos() - m_dragStartScene;
    QRectF r = m_startSceneRect;

    switch (m_drag) {
        case Body: r.translate(d); break;
        case TL: r.setTopLeft   (r.topLeft()    + d); break;
        case T:  r.setTop       (r.top()        + d.y()); break;
        case TR: r.setTopRight  (r.topRight()   + d); break;
        case R:  r.setRight     (r.right()      + d.x()); break;
        case BR: r.setBottomRight(r.bottomRight()+ d); break;
        case B:  r.setBottom    (r.bottom()     + d.y()); break;
        case BL: r.setBottomLeft(r.bottomLeft() + d); break;
        case L:  r.setLeft      (r.left()       + d.x()); break;
        default: break;
    }
    r = r.normalized();
    if (r.width()  < 8) r.setWidth(8);
    if (r.height() < 8) r.setHeight(8);

    prepareGeometryChange();
    m_w = r.width();
    m_h = r.height();
    setPos(r.topLeft());
    update();
    e->accept();
}

void LayerItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e) {
    if (m_drag != None) {
        commitToModel();
        m_drag = None;
    }
    e->accept();
}

void LayerItem::commitToModel() {
    if (m_model)
        m_model->setGeometry(m_id, QRectF(pos(), QSizeF(m_w, m_h)));
}

} // namespace uwp
