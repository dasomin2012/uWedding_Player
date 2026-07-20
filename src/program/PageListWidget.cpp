#include "PageListWidget.h"

#include "Program.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <functional>

namespace uwp {

// 좌측 컬럼(240px 폭 근처) 안에서 세로 스택으로 여러 페이지 카드를 노출하려면
// 카드가 작아야 한다. 16:9 썸네일 148×84 를 기준으로 카드 총 크기 168×130.
static const QSize kThumbSize(148, 84);
static const QSize kCardSize (170, 132);
static constexpr int kActiveRole = Qt::UserRole + 1;

static QPixmap makeDefaultThumb() {
    QPixmap pm(kThumbSize);
    pm.fill(QColor(40, 40, 44));
    QPainter p(&pm);
    p.setPen(QColor(90, 90, 100));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    p.setPen(QColor(130, 130, 140));
    QFont f = p.font(); f.setPointSize(8); p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, "(no thumb)");
    p.end();
    return pm;
}

// PageCardDelegate — ProgramCardDelegate 와 시각 규칙 통일(작은 크기용).
//   * 좌상단 순번 뱃지, 우상단 X (hover/선택), 활성 페이지 크림슨 테두리.
//   * ON AIR 배지 / endAction 배지는 페이지 단위엔 의미 없어 표시하지 않음
//     (Program.endAction 은 프로그램 카드가 담당).
class PageCardDelegate : public QStyledItemDelegate {
public:
    using DeleteHandler = std::function<void(int row)>;
    static constexpr int kPad     = 6;
    static constexpr int kBtnSize = 18;
    static constexpr int kBtnPad  = 4;
    static constexpr int kNumSize = 18;

    PageCardDelegate(DeleteHandler onDelete, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_onDelete(std::move(onDelete)) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);

        const QRect card = opt.rect.adjusted(3, 3, -3, -3);
        const bool hover  = (opt.state & QStyle::State_MouseOver);
        const bool sel    = (opt.state & QStyle::State_Selected);
        const bool active = idx.data(kActiveRole).toBool();

        const QRect iconRect(card.x() + (card.width() - kThumbSize.width()) / 2,
                             card.y() + kPad,
                             kThumbSize.width(), kThumbSize.height());
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            icon.paint(p, iconRect, Qt::AlignCenter, QIcon::Normal);
        } else {
            p->setPen(QPen(opt.palette.color(QPalette::Mid), 1));
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect);
        }

        // 활성/선택 테두리 — 썸네일에 딱 맞춰 직사각형.
        if (active) {
            QPen pen(QColor(0xd6, 0x33, 0x24), 2);
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen); p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        } else if (sel) {
            QPen pen(opt.palette.color(QPalette::Highlight), 2);
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen); p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        }

        // 이름 (또는 순번) — 썸네일 아래 한 줄 elide.
        const QString name = idx.data(Qt::DisplayRole).toString();
        QFontMetrics fm(opt.font);
        const QRect nameRect(card.x() + kPad, iconRect.bottom() + kPad,
                             card.width() - kPad * 2, fm.height() + 2);
        p->setPen(opt.palette.color(QPalette::Text));
        QFont nameFont = opt.font;
        nameFont.setBold(active);
        p->setFont(nameFont);
        p->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                    fm.elidedText(name, Qt::ElideRight, nameRect.width()));

        // 좌상단 순번 뱃지.
        const QRect numRect(iconRect.x() + kBtnPad, iconRect.y() + kBtnPad,
                            kNumSize, kNumSize);
        p->setPen(Qt::NoPen);
        p->setBrush(active ? QColor(0xd6, 0x33, 0x24)
                           : QColor(0, 0, 0, 170));
        p->drawEllipse(numRect);
        p->setPen(QPen(Qt::white, 1));
        QFont numFont = opt.font;
        numFont.setBold(true);
        numFont.setPointSizeF(qMax(7.5, numFont.pointSizeF() - 2));
        p->setFont(numFont);
        p->drawText(numRect, Qt::AlignCenter, QString::number(idx.row() + 1));

        // 우상단 X — hover 또는 선택 시.
        if (hover || sel) {
            const QRect xr = xRect(iconRect);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 200));
            p->drawEllipse(xr);
            QPen pen(Qt::white, 1.6);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            const int m = 5;
            p->drawLine(xr.left()  + m, xr.top()    + m,
                        xr.right() - m, xr.bottom() - m);
            p->drawLine(xr.right() - m, xr.top()    + m,
                        xr.left()  + m, xr.bottom() - m);
        }

        p->restore();
    }

    bool editorEvent(QEvent* ev, QAbstractItemModel*,
                     const QStyleOptionViewItem& opt,
                     const QModelIndex& idx) override {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            const QRect card = opt.rect.adjusted(3, 3, -3, -3);
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

PageListWidget::PageListWidget(QWidget* parent)
    : QWidget(parent)
{
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::TopToBottom);      // 세로 스택
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(kThumbSize);
    m_list->setGridSize(kCardSize);
    m_list->setSpacing(4);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    m_list->setMouseTracking(true);
    m_list->setItemDelegate(new PageCardDelegate(
        [this](int row){
            QListWidgetItem* it = m_list->item(row);
            if (!it) return;
            const QString id = it->data(Qt::UserRole).toString();
            emit deleteRequested(id);
        }, m_list));

    auto* btnAdd = new QPushButton(tr("+ 페이지 추가"));
    btnAdd->setEnabled(false);   // 프로그램 로드되어야 활성
    connect(btnAdd, &QPushButton::clicked, this, &PageListWidget::addRequested);
    // 프로그램 유무 = m_list 존재 페이지 유무로 판단은 부정확 — 대신 setProgram 이
    // enable 상태를 갱신.
    btnAdd->setObjectName("PageAddButton");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(m_list, 1);
    lay->addWidget(btnAdd);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
        if (it) emit pageSelected(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &PageListWidget::showContextMenu);

    // + 버튼 enable 은 setProgram 에서 갱신 — 여기서는 setProperty 로 캐시.
    btnAdd->setProperty("_addBtn", true);
}

void PageListWidget::setProgram(const Program* program, const QString& dataDir) {
    m_dataDir = dataDir;
    m_list->clear();

    // + 버튼 활성/비활성 갱신 (자식 위젯에서 objectName 으로 찾기)
    QPushButton* btnAdd = findChild<QPushButton*>(QStringLiteral("PageAddButton"));
    if (btnAdd) btnAdd->setEnabled(program != nullptr);
    if (!program) return;

    const QPixmap fallback = makeDefaultThumb();
    for (int i = 0; i < program->pages.size(); ++i) {
        const Page& pg = program->pages[i];
        QPixmap thumb;
        if (!pg.thumbnailRelPath.isEmpty()) {
            const QString abs = QDir(dataDir).filePath(pg.thumbnailRelPath);
            if (QFileInfo::exists(abs)) thumb.load(abs);
        }
        if (thumb.isNull()) thumb = fallback;
        const QString label = pg.name.isEmpty()
            ? QString::number(i + 1) : pg.name;
        auto* item = new QListWidgetItem(QIcon(thumb), label, m_list);
        item->setData(Qt::UserRole, pg.id);
        item->setToolTip(label);
        item->setData(kActiveRole, false);
    }
    setActivePage(m_activeId);
}

void PageListWidget::setActivePage(const QString& id) {
    m_activeId = id;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const bool active = (!id.isEmpty()
                             && it->data(Qt::UserRole).toString() == id);
        it->setData(kActiveRole, active);
        if (active) m_list->setCurrentItem(it);
    }
    m_list->viewport()->update();
}

void PageListWidget::showContextMenu(const QPoint& pos) {
    QListWidgetItem* it = m_list->itemAt(pos);
    if (!it) return;
    const QString id   = it->data(Qt::UserRole).toString();
    const QString name = it->text();

    QMenu menu(this);
    QAction* renameAct  = menu.addAction(tr("이름 변경..."));
    QAction* displayAct = menu.addAction(tr("표시 시간 설정..."));
    QAction* upAct      = menu.addAction(tr("위로"));
    QAction* downAct    = menu.addAction(tr("아래로"));

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == renameAct) {
        bool ok = false;
        const QString nn = QInputDialog::getText(
            this, tr("페이지 이름 변경"),
            tr("이름 (빈 값이면 순번 표시):"),
            QLineEdit::Normal, name, &ok);
        if (ok) emit renameRequested(id, nn);
    } else if (chosen == displayAct) {
        emit displayTimeEditRequested(id);
    } else if (chosen == upAct) {
        emit moveUpRequested(id);
    } else if (chosen == downAct) {
        emit moveDownRequested(id);
    }
}

} // namespace uwp
