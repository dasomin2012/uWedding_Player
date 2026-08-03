#include "LayerItem.h"

#include "PreviewCanvas.h"
#include "scene/SceneModel.h"

#include <QGraphicsScene>
#include <QGraphicsView>

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

    // Text 레이어: SnapshotCache 없이 QPainter 로 직접 렌더.
    //   (변수명 L 은 Handle::L 과 충돌 → lay 로.)
    const Layer* lay = m_model ? m_model->layer(m_id) : nullptr;
    if (lay && lay->mediaType == MediaType::Text) {
        // 배경 (bgColor + bgOpacity)
        QColor bg(lay->bgColor);
        bg.setAlphaF(qBound(0.0, lay->bgOpacity, 1.0));
        p->fillRect(box, bg);

        // 텍스트
        QFont f(lay->fontFamily);
        f.setPixelSize(qMax(1, lay->fontSize));   // px = 캔버스 좌표계와 1:1
        f.setWeight(lay->fontWeight >= 700 ? QFont::Bold : QFont::Normal);
        p->setFont(f);
        p->setPen(QColor(lay->textColor));
        const QRectF textRect = box.adjusted(lay->padding, lay->padding,
                                             -lay->padding, -lay->padding);
        int flags = Qt::TextWordWrap;
        flags |= (lay->textAlign   == 0) ? Qt::AlignLeft
              : (lay->textAlign    == 2) ? Qt::AlignRight  : Qt::AlignHCenter;
        flags |= (lay->textVAlign  == 0) ? Qt::AlignTop
              : (lay->textVAlign   == 2) ? Qt::AlignBottom : Qt::AlignVCenter;
        p->drawText(textRect, flags, lay->text);
    }
    else if (!m_pixmap.isNull()) {
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
    // Selection 소스는 모델 하나. onSelectionChanged 가 씬을 원자적으로
    // 정리 + 이 아이템 하나만 setSelected(true) 로 만든다. 여기서 직접
    // setSelected 를 호출하지 않는다(과거: 이중 경로가 stale 시각 선택 유발).
    if (m_model && m_model->selectedId() != m_id) m_model->select(m_id);
    if (!isSelected()) setSelected(true);   // 방어: 모델 select 가 same-id no-op 였을 때

    m_drag           = hitTest(e->pos());
    m_dragStartScene = e->scenePos();
    m_startSceneRect = QRectF(pos(), QSizeF(m_w, m_h));
    e->accept();
}

void LayerItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e) {
    if (m_drag == None) return;

    const bool corner = (m_drag == TL || m_drag == TR ||
                         m_drag == BR || m_drag == BL);
    const bool lockAspect = corner && m_model && m_model->aspectLocked()
                            && m_startSceneRect.width()  > 0.5
                            && m_startSceneRect.height() > 0.5;

    QRectF r = m_startSceneRect;

    if (lockAspect) {
        // 비율 고정: 반대편 코너를 고정점(anchor)으로 두고 종횡비를 유지하며
        // 마우스를 따라 크기 조정(지배 축 기준).
        const qreal aspect = m_startSceneRect.width() / m_startSceneRect.height();
        QPointF anchor;
        switch (m_drag) {
            case TL: anchor = m_startSceneRect.bottomRight(); break;
            case TR: anchor = m_startSceneRect.bottomLeft();  break;
            case BR: anchor = m_startSceneRect.topLeft();     break;
            case BL: anchor = m_startSceneRect.topRight();    break;
            default: anchor = m_startSceneRect.topLeft();     break;
        }
        const QPointF m = e->scenePos();
        const qreal desiredW = qAbs(m.x() - anchor.x());
        const qreal desiredH = qAbs(m.y() - anchor.y());
        qreal newW, newH;
        if (desiredW >= desiredH * aspect) { newW = desiredW; newH = newW / aspect; }
        else                               { newH = desiredH; newW = newH * aspect; }
        newW = qMax<qreal>(8.0, newW);
        newH = qMax<qreal>(8.0, newH);
        const qreal x = (m.x() >= anchor.x()) ? anchor.x() : anchor.x() - newW;
        const qreal y = (m.y() >= anchor.y()) ? anchor.y() : anchor.y() - newH;
        r = QRectF(x, y, newW, newH);
    } else {
        const QPointF d = e->scenePos() - m_dragStartScene;
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
    }

    // 캔버스 밖 이동/리사이즈 제한 — 마그네틱처럼 넘어가면 가장자리에 붙임.
    //   Move (Body):  좌우/상하 밖으로 나가지 않게 위치만 clamp (크기 유지).
    //   Resize edges: 넘어간 변만 캔버스 경계로 스냅 (반대 변은 유지).
    if (m_model) {
        const QSize cs = m_model->canvasSize();
        const qreal cw = cs.width();
        const qreal ch = cs.height();
        if (m_drag == Body) {
            // 위치 clamp: 레이어가 캔버스보다 크면 왼쪽/위 정렬.
            qreal x = r.x(), y = r.y();
            if (r.width()  <= cw) x = qBound<qreal>(0.0, x, cw - r.width());
            else                  x = 0.0;
            if (r.height() <= ch) y = qBound<qreal>(0.0, y, ch - r.height());
            else                  y = 0.0;
            r.moveTo(x, y);
        } else {
            // 리사이즈: 넘어간 변만 클램프.
            const qreal L = qMax<qreal>(0.0, r.left());
            const qreal T = qMax<qreal>(0.0, r.top());
            const qreal R = qMin<qreal>(cw, r.right());
            const qreal B = qMin<qreal>(ch, r.bottom());
            r = QRectF(QPointF(L, T), QPointF(R, B));
            if (r.width()  < 8) r.setWidth(8);
            if (r.height() < 8) r.setHeight(8);
        }
    }

    // 정렬 스냅 — 캔버스 가장자리·중심 + 다른 레이어의 가장자리·중심.
    //   임계값: 뷰 스케일에 반비례 (화면상 ~8px 정도 유지).
    QList<qreal> activeGuideXs, activeGuideYs;
    if (m_model) {
        qreal viewScale = 1.0;
        if (scene() && !scene()->views().isEmpty()) {
            viewScale = qMax<qreal>(0.0001,
                scene()->views().first()->transform().m11());
        }
        const qreal SNAP = 8.0 / viewScale;   // 씬 좌표 임계값

        const QSize cs = m_model->canvasSize();
        QVector<qreal> snapXs, snapYs;
        snapXs << 0.0 << cs.width() / 2.0 << (qreal)cs.width();
        snapYs << 0.0 << cs.height() / 2.0 << (qreal)cs.height();
        for (const Layer& other : m_model->layers()) {
            if (other.id == m_id) continue;
            snapXs << other.geometry.left()   << other.geometry.right()
                   << other.geometry.center().x();
            snapYs << other.geometry.top()    << other.geometry.bottom()
                   << other.geometry.center().y();
        }

        // 가장 가까운 스냅 x/y 를 찾는 helper.
        auto bestSnap = [](const QVector<qreal>& targets,
                           const QVector<qreal>& edges, qreal thresh,
                           qreal& outDelta, qreal& outAt) -> bool {
            qreal best = thresh;
            for (qreal t : targets) {
                for (qreal e : edges) {
                    const qreal d = qAbs(t - e);
                    if (d < best) { best = d; outDelta = t - e; outAt = t; }
                }
            }
            return best < thresh;
        };

        // 이동/리사이즈 별로 "움직이는 변" 지정 후 스냅.
        QVector<qreal> movingXs, movingYs;
        if (m_drag == Body) {
            movingXs << r.left() << r.right() << r.center().x();
            movingYs << r.top()  << r.bottom() << r.center().y();
        } else {
            switch (m_drag) {
                case L:  movingXs << r.left(); break;
                case R:  movingXs << r.right(); break;
                case T:  movingYs << r.top(); break;
                case B:  movingYs << r.bottom(); break;
                case TL: movingXs << r.left();  movingYs << r.top();    break;
                case TR: movingXs << r.right(); movingYs << r.top();    break;
                case BL: movingXs << r.left();  movingYs << r.bottom(); break;
                case BR: movingXs << r.right(); movingYs << r.bottom(); break;
                default: break;
            }
        }

        qreal dx = 0, dy = 0, ax = 0, ay = 0;
        if (bestSnap(snapXs, movingXs, SNAP, dx, ax)) {
            if (m_drag == Body) r.translate(dx, 0);
            else {
                // 리사이즈: 움직이는 변만 이동.
                if (m_drag == L || m_drag == TL || m_drag == BL)
                    r.setLeft(r.left() + dx);
                else
                    r.setRight(r.right() + dx);
            }
            activeGuideXs << ax;
        }
        if (bestSnap(snapYs, movingYs, SNAP, dy, ay)) {
            if (m_drag == Body) r.translate(0, dy);
            else {
                if (m_drag == T || m_drag == TL || m_drag == TR)
                    r.setTop(r.top() + dy);
                else
                    r.setBottom(r.bottom() + dy);
            }
            activeGuideYs << ay;
        }
        if (r.width()  < 8) r.setWidth(8);
        if (r.height() < 8) r.setHeight(8);

        // 캔버스 뷰에 가이드 위치 전달.
        if (scene() && !scene()->views().isEmpty()) {
            if (auto* pc = qobject_cast<PreviewCanvas*>(scene()->views().first()))
                pc->showSnapGuides(activeGuideXs, activeGuideYs);
        }
    }

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
        // 스냅 가이드 정리.
        if (scene() && !scene()->views().isEmpty()) {
            if (auto* pc = qobject_cast<PreviewCanvas*>(scene()->views().first()))
                pc->clearSnapGuides();
        }
    }
    e->accept();
}

void LayerItem::commitToModel() {
    if (m_model)
        m_model->setGeometry(m_id, QRectF(pos(), QSizeF(m_w, m_h)));
}

} // namespace uwp
