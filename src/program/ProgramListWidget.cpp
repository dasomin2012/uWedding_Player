#include "ProgramListWidget.h"

#include "Program.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTextOption>
#include <QVBoxLayout>

#include <functional>

namespace uwp {

static const QSize kThumbSize(180, 101);       // 16:9 (LED 캔버스 비율)
static const QSize kCardSize(212, 172);        // 카드 (썸네일 + 이름 여백)
// 재생중 상태 배지("ON AIR")용 role — bool, delegate 가 읽음.
static constexpr int kActiveRole = Qt::UserRole + 1;

// 썸네일이 없을 때 표시할 회색 박스.
static QPixmap makeDefaultThumb() {
    QPixmap pm(kThumbSize);
    pm.fill(QColor(48, 48, 54));
    QPainter p(&pm);
    p.setPen(QColor(90, 90, 100));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    p.setPen(QColor(130, 130, 140));
    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, "(no thumb)");
    p.end();
    return pm;
}

// UI-C: 프로그램 카드 델리게이트.
//   레이아웃:
//     +-------------------+
//     | ①              ✕ |   ← 좌상단 순번, 우상단 X(hover)
//     |  [ 썸네일 180×101 ] |
//     |     프로그램 이름    |
//     |    [🔴 ON AIR ]     |   ← active 일 때만
//     +-------------------+
//   상태 시각화:
//     * active(재생중) → 3px 크림슨 테두리 + ON AIR 뱃지
//     * selected       → 브론즈 테두리 (팔레트 Highlight)
//     * hover only     → 은은한 오버레이
//   상호작용:
//     * X 클릭 → 삭제(콜백)
//     * 그 외 위치 → base view 로 전달(단일=선택, 더블=재생)
class ProgramCardDelegate : public QStyledItemDelegate {
public:
    using DeleteHandler = std::function<void(int row)>;

    static constexpr int kPad     = 8;
    static constexpr int kBtnSize = 22;
    static constexpr int kBtnPad  = 6;
    static constexpr int kNumSize = 22;

    ProgramCardDelegate(DeleteHandler onDelete, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_onDelete(std::move(onDelete)) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);

        const QRect card = opt.rect.adjusted(4, 4, -4, -4);
        const bool hover  = (opt.state & QStyle::State_MouseOver);
        const bool sel    = (opt.state & QStyle::State_Selected);
        const bool active = idx.data(kActiveRole).toBool();

        // 썸네일 rect — 카드 상단 중앙. 모든 오버레이(뱃지/테두리)의 기준.
        const QRect iconRect(card.x() + (card.width() - kThumbSize.width()) / 2,
                             card.y() + kPad,
                             kThumbSize.width(), kThumbSize.height());

        // 썸네일 자체
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            icon.paint(p, iconRect, Qt::AlignCenter, QIcon::Normal);
        } else {
            p->setPen(QPen(opt.palette.color(QPalette::Mid), 1));
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect);
        }

        // 강조 테두리 — 썸네일 사각형에 딱 맞춰, 직사각형(pen alignment 반영).
        //   active   → 3px 크림슨
        //   selected → 2px 팔레트 하이라이트(브론즈/샴페인)
        //   pen 은 절반이 밖으로 그려지므로 rect 를 안쪽으로 살짝 밀어 시각적 정렬.
        if (active) {
            QPen pen(QColor(0xd6, 0x33, 0x24), 3);
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        } else if (sel) {
            QPen pen(opt.palette.color(QPalette::Highlight), 2);
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        }

        // 이름 — 썸네일 바로 아래, 단일 라인 elide.
        const QString name = idx.data(Qt::DisplayRole).toString();
        QFontMetrics fm(opt.font);
        const QRect nameRect(card.x() + kPad,
                             iconRect.bottom() + kPad,
                             card.width() - kPad * 2,
                             fm.height() + 2);
        p->setPen(opt.palette.color(QPalette::Text));
        QFont nameFont = opt.font;
        nameFont.setBold(active);
        p->setFont(nameFont);
        p->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideRight, nameRect.width()));

        // 좌상단 순번 뱃지 — 썸네일 내부 좌상단 (카드 밖으로 안 나감).
        const QRect numRect(iconRect.x() + kBtnPad, iconRect.y() + kBtnPad,
                            kNumSize, kNumSize);
        p->setPen(Qt::NoPen);
        p->setBrush(active ? QColor(0xd6, 0x33, 0x24)
                           : QColor(0, 0, 0, 170));
        p->drawEllipse(numRect);
        p->setPen(QPen(Qt::white, 1));
        QFont numFont = opt.font;
        numFont.setBold(true);
        numFont.setPointSizeF(qMax(8.0, numFont.pointSizeF() - 1));
        p->setFont(numFont);
        p->drawText(numRect, Qt::AlignCenter, QString::number(idx.row() + 1));

        // 우상단 X — 썸네일 내부 우상단, hover 또는 selected.
        if (hover || sel) {
            const QRect xr = xRect(iconRect);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 200));
            p->drawEllipse(xr);
            QPen pen(Qt::white, 2.0);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            const int m = 7;
            p->drawLine(xr.left()  + m, xr.top()    + m,
                        xr.right() - m, xr.bottom() - m);
            p->drawLine(xr.right() - m, xr.top()    + m,
                        xr.left()  + m, xr.bottom() - m);
        }

        // ON AIR 뱃지 — 썸네일 내부 하단 중앙, active 일 때만. 작고 반투명하지 않음.
        if (active) {
            QFont pillFont = opt.font;
            pillFont.setBold(true);
            pillFont.setPointSizeF(qMax(8.0, pillFont.pointSizeF() - 2));
            p->setFont(pillFont);
            QFontMetrics pfm(pillFont);
            const QString label = QStringLiteral("● ON AIR");
            const int w = pfm.horizontalAdvance(label) + 10;
            const int h = pfm.height() + 2;
            const QRect pill(iconRect.x() + (iconRect.width() - w) / 2,
                             iconRect.bottom() - h - 4, w, h);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0xd6, 0x33, 0x24));
            p->drawRoundedRect(pill, h / 2, h / 2);
            p->setPen(Qt::white);
            p->drawText(pill, Qt::AlignCenter, label);
        }

        p->restore();
    }

    bool editorEvent(QEvent* ev, QAbstractItemModel*,
                     const QStyleOptionViewItem& opt,
                     const QModelIndex& idx) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            const QRect card = opt.rect.adjusted(4, 4, -4, -4);
            const QRect iconRect(card.x() + (card.width() - kThumbSize.width()) / 2,
                                 card.y() + kPad,
                                 kThumbSize.width(), kThumbSize.height());
            if (me->button() == Qt::LeftButton
                && xRect(iconRect).contains(me->pos())) {
                if (m_onDelete) m_onDelete(idx.row());
                return true;    // 선택 이동/드래그 억제
            }
        }
        return false;
    }

private:
    static QRect xRect(const QRect& iconRect) {
        return QRect(iconRect.right() - kBtnSize - kBtnPad,
                     iconRect.y()     + kBtnPad,
                     kBtnSize, kBtnSize);
    }
    DeleteHandler m_onDelete;
};

ProgramListWidget::ProgramListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* title  = new QLabel(tr("프로그램"));
    title->setStyleSheet("font-weight: bold;");
    auto* btnAdd = new QPushButton(tr("+ 추가"));
    btnAdd->setToolTip(tr("현재 씬을 프로그램으로 저장"));
    connect(btnAdd, &QPushButton::clicked, this, &ProgramListWidget::addRequested);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(title);
    top->addStretch();
    top->addWidget(btnAdd);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(kThumbSize);
    m_list->setGridSize(kCardSize);
    m_list->setSpacing(6);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    // UI-C: hover 즉시 반영
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    m_list->setMouseTracking(true);
    m_list->setItemDelegate(new ProgramCardDelegate(
        [this](int row){
            QListWidgetItem* it = m_list->item(row);
            if (!it) return;
            const QString id   = it->data(Qt::UserRole).toString();
            const QString name = it->text().isEmpty() ? id : it->text();
            const auto r = QMessageBox::question(
                this, tr("프로그램 삭제"),
                tr("\"%1\" 을(를) 삭제할까요?").arg(name));
            if (r == QMessageBox::Yes) emit deleteRequested(id);
        }, m_list));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->addLayout(top);
    lay->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (it) emit programSelected(it->data(Qt::UserRole).toString());
    });
    // UI-C: 더블클릭도 Preview 로드만. Live 송출은 오직 TAKE 버튼으로 (§운영자 요구).
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it) emit programSelected(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &ProgramListWidget::showContextMenu);
}

QString ProgramListWidget::currentId() const {
    auto* it = m_list->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void ProgramListWidget::setPrograms(const QVector<Program>& programs,
                                    const QString& dataDir) {
    m_dataDir = dataDir;
    m_list->clear();

    const QPixmap fallback = makeDefaultThumb();
    for (const Program& p : programs) {
        QPixmap thumb;
        if (!p.thumbnailRelPath.isEmpty()) {
            const QString abs = QDir(dataDir).filePath(p.thumbnailRelPath);
            if (QFileInfo::exists(abs)) thumb.load(abs);
        }
        if (thumb.isNull()) thumb = fallback;

        auto* item = new QListWidgetItem(
            QIcon(thumb), p.name.isEmpty() ? p.id : p.name, m_list);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.name);
        item->setData(kActiveRole, false);      // 아래 setActiveProgram 이 갱신
    }
    setActiveProgram(m_activeId);   // 강조 유지
}

void ProgramListWidget::selectProgram(const QString& id) {
    if (id.isEmpty()) { m_list->clearSelection(); m_list->setCurrentItem(nullptr); return; }
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentItem(it);
            return;
        }
    }
}

void ProgramListWidget::updateItem(const QString& id, const QString& name,
                                   const QString& thumbAbsPath) {
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() != id) continue;
        it->setText(name.isEmpty() ? id : name);
        it->setToolTip(name);
        QPixmap thumb;
        if (!thumbAbsPath.isEmpty() && QFileInfo::exists(thumbAbsPath))
            thumb.load(thumbAbsPath);
        it->setIcon(QIcon(thumb.isNull() ? makeDefaultThumb() : thumb));
        return;
    }
}

void ProgramListWidget::setActiveProgram(const QString& id) {
    m_activeId = id;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const bool active = (!id.isEmpty()
                             && it->data(Qt::UserRole).toString() == id);
        it->setData(kActiveRole, active);
    }
    m_list->viewport()->update();   // active 뱃지·테두리 재도색
}

void ProgramListWidget::showContextMenu(const QPoint& pos) {
    QListWidgetItem* it = m_list->itemAt(pos);
    if (!it) return;
    const QString id   = it->data(Qt::UserRole).toString();
    const QString name = it->text();

    QMenu menu(this);
    QAction* renameAct  = menu.addAction(tr("이름 변경..."));
    QAction* displayAct = menu.addAction(tr("표시 시간 설정..."));

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAct) {
        bool ok = false;
        const QString nn = QInputDialog::getText(
            this, tr("프로그램 이름 변경"), tr("이름:"),
            QLineEdit::Normal, name, &ok);
        if (ok && !nn.isEmpty()) emit renameRequested(id, nn);
    } else if (chosen == displayAct) {
        // Application 이 프로그램 조회 + QInputDialog 표시 (현재값을 초기값으로).
        emit displayTimeEditRequested(id);
    }
}

} // namespace uwp
