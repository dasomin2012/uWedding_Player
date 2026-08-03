#include "PageListWidget.h"

#include "Program.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
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

// 하단 [프로그램][페이지] 탭에서 가로 스트립으로 표시.
// 프로그램 카드(212×172, 썸네일 180×101)와 사이즈 통일해 탭 스위칭 시
// 컨텐츠 영역 높이가 튀지 않도록 함.
static const QSize kThumbSize(180, 101);
static const QSize kCardSize (212, 172);
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
    m_list->setFlow(QListView::LeftToRight);      // 가로 스트립 (ProgramListWidget 과 통일)
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(kThumbSize);
    m_list->setGridSize(kCardSize);
    // 페이지 카드 한 줄만 노출 — sizeHint 를 카드 높이로 고정해 상위 레이아웃이
    // 필요 이상 세로를 배정하지 않게 (사용자 목표: 리스트 영역 ~140).
    m_list->setFixedHeight(140);
    m_list->setSpacing(6);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    // 타이틀 라벨은 이제 상위 CollapsibleSection 헤더가 담당 — 내부 표시 없음.
    //   ProgramListWidget 과 동일한 [카드 리스트 | + 추가] 한 줄 레이아웃.
    m_titleLabel = new QLabel(tr("페이지"));

    m_btnAdd = new QPushButton(tr("+ 추가"));
    m_btnAdd->setEnabled(false);
    m_btnAdd->setObjectName("PageAddButton");
    connect(m_btnAdd, &QPushButton::clicked, this, &PageListWidget::addRequested);

    // [카드 리스트 stretch=1 | + 추가 (세로 중앙)] — 프로그램 스트립과 동일.
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_btnAdd, 0, Qt::AlignVCenter);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
        if (it) emit pageSelected(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &PageListWidget::showContextMenu);

    // Del 키로 삭제 요청 — X 버튼(delegate)과 동일 시그널.
    m_list->installEventFilter(this);
}

void PageListWidget::setProgram(const Program* program, const QString& dataDir) {
    m_dataDir = dataDir;
    m_list->clear();

    // + 추가 버튼 활성/비활성 — 이전 구현은 findChild 로 버튼을 찾았으나,
    //   상위가 setHeaderRight 로 버튼을 재부모하면 findChild 가 nullptr 을
    //   반환해 항상 disabled 상태로 남는 회귀가 있었다. m_btnAdd 직접 사용.
    if (m_btnAdd) m_btnAdd->setEnabled(program != nullptr);

    // 편집 대상 프로그램 이름 변화를 상위(섹션 헤더 등)에 전파.
    emit programChanged(program ? program->name : QString());

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
        // 라벨: 이름이 있으면 이름, 없으면 "Page N" (숫자만은 정보량 부족).
        const QString label = pg.name.isEmpty()
            ? tr("Page %1").arg(i + 1) : pg.name;
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

void PageListWidget::setTitleVisible(bool visible) {
    if (m_titleLabel) m_titleLabel->setVisible(visible);
}

bool PageListWidget::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_list && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            QListWidgetItem* it = m_list->currentItem();
            if (it) {
                emit deleteRequested(it->data(Qt::UserRole).toString());
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace uwp
