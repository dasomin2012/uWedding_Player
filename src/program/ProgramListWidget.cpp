#include "ProgramListWidget.h"

#include "Program.h"
#include "scene/Layer.h"   // EndAction

#include <QAbstractItemModel>
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
static constexpr int kActiveRole      = Qt::UserRole + 1;
// endAction 배지용 role — int (EndAction 값). displayTime > 0 일 때만 렌더.
static constexpr int kEndActionRole   = Qt::UserRole + 2;
static constexpr int kDisplayTimeRole = Qt::UserRole + 3;

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

        // 접힘 모드: 썸네일·뱃지 모두 생략, 이름만 알약(pill) 형태로.
        //   활성(재생중) = 크림슨 배경 + 흰 텍스트
        //   선택(편집 대상) = 앰버 배경 + 검정 텍스트 (선명한 대비)
        //   기본 = 팔레트 Base 배경 + 팔레트 Text
        const bool collapsedMode = property("collapsedMode").toBool();
        if (collapsedMode) {
            const QRect pill = card.adjusted(2, 2, -2, -2);
            // 배경/테두리 — 팔레트 의존 대신 고정 톤으로 언제나 눈에 띄게.
            //   기본: 밝은 크림 + 얇은 브론즈 테두리
            //   hover: 살짝 진한 크림
            //   active: 크림슨
            //   selected: 앰버
            QColor bg      = QColor(0xff, 0xff, 0xff);
            QColor border  = QColor(0xd3, 0xc8, 0xb8);
            QColor textCol = QColor(0x1c, 0x15, 0x12);   // 딥 텍스트 상시
            if (active)      { bg = QColor(0xd6, 0x33, 0x24); border = bg; textCol = Qt::white; }
            else if (sel)    { bg = QColor(0xf5, 0x9e, 0x0b); border = QColor(0xd9, 0x77, 0x06); }
            else if (hover)  { bg = QColor(0xf5, 0xed, 0xe0); border = QColor(0xb9, 0x8a, 0x5e); }
            p->setPen(QPen(border, 1));
            p->setBrush(bg);
            p->drawRoundedRect(pill, 6, 6);

            // 이름 — 굵게, 명확한 대비색.
            QFont f = opt.font;
            f.setBold(true);
            p->setFont(f);
            p->setPen(textCol);
            const QString name = idx.data(Qt::DisplayRole).toString();
            const QRect textRect = pill.adjusted(8, 0, -8, 0);
            QFontMetrics fm(f);
            p->drawText(textRect, Qt::AlignCenter,
                        fm.elidedText(name, Qt::ElideRight, textRect.width()));
            p->restore();
            return;
        }

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
        //   selected → 3px 앰버 (기존 2px 팔레트 색은 안 보였다는 피드백 반영)
        //   pen 은 절반이 밖으로 그려지므로 rect 를 안쪽으로 살짝 밀어 시각적 정렬.
        if (active) {
            QPen pen(QColor(0xd6, 0x33, 0x24), 3);
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        } else if (sel) {
            QPen pen(QColor(0xf5, 0x9e, 0x0b), 3);   // amber 500
            pen.setJoinStyle(Qt::MiterJoin);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRect(iconRect.adjusted(1, 1, -1, -1));
        }

        // 이름 pill — 컴팩트 모드 pill 과 완전 동일한 크기/스타일/상태색.
        //   활성(재생중) = 크림슨 + 흰 텍스트
        //   선택(편집 대상) = 앰버
        //   hover = 크림
        //   기본 = 백색 + 브론즈 테두리
        const QString name = idx.data(Qt::DisplayRole).toString();
        const QRect pill(card.x() + kPad,
                         iconRect.bottom() + kPad,
                         card.width() - kPad * 2,
                         40);
        QColor bg      = QColor(0xff, 0xff, 0xff);
        QColor border  = QColor(0xd3, 0xc8, 0xb8);
        QColor textCol = QColor(0x1c, 0x15, 0x12);
        if (active)      { bg = QColor(0xd6, 0x33, 0x24); border = bg; textCol = Qt::white; }
        else if (sel)    { bg = QColor(0xf5, 0x9e, 0x0b); border = QColor(0xd9, 0x77, 0x06); }
        else if (hover)  { bg = QColor(0xf5, 0xed, 0xe0); border = QColor(0xb9, 0x8a, 0x5e); }
        p->setPen(QPen(border, 1));
        p->setBrush(bg);
        p->drawRoundedRect(pill, 6, 6);

        QFont nameFont = opt.font;
        nameFont.setBold(true);
        p->setFont(nameFont);
        p->setPen(textCol);
        QFontMetrics fm(nameFont);
        const QRect textRect = pill.adjusted(8, 0, -8, 0);
        p->drawText(textRect, Qt::AlignCenter,
                    fm.elidedText(name, Qt::ElideRight, textRect.width()));

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

        // endAction 뱃지 — 썸네일 내부 하단 우측. displayTime > 0 일 때만
        // (수동 진행이면 endAction 은 발동 기회가 없어 배지 표시가 오해 유발).
        const int displaySec = idx.data(kDisplayTimeRole).toInt();
        if (displaySec > 0) {
            const int eaInt = idx.data(kEndActionRole).toInt();
            const auto ea = static_cast<EndAction>(eaInt);
            QString label;
            switch (ea) {
                case EndAction::Loop:  label = QStringLiteral("↻");   break;
                case EndAction::Stop:  label = QStringLiteral("■");   break;
                case EndAction::Hold:  label = QStringLiteral("⏸");  break;
                case EndAction::First:
                    if (idx.model() && idx.model()->rowCount() > 0)
                        label = QStringLiteral("→1");
                    break;
                case EndAction::Next: {
                    const int nextRow = idx.row() + 1;
                    if (idx.model() && nextRow < idx.model()->rowCount())
                        label = QStringLiteral("→%1").arg(nextRow + 1);
                    else
                        label = QStringLiteral("■");   // 마지막 → stop
                    break;
                }
            }
            if (!label.isEmpty()) {
                QFont badgeFont = opt.font;
                badgeFont.setBold(true);
                badgeFont.setPointSizeF(qMax(8.0, badgeFont.pointSizeF() - 1));
                p->setFont(badgeFont);
                QFontMetrics bfm(badgeFont);
                const int w = qMax(bfm.horizontalAdvance(label) + 10, 26);
                const int h = bfm.height() + 2;
                const QRect pill(iconRect.right() - w - 4,
                                 iconRect.bottom() - h - 4, w, h);
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(0, 0, 0, 180));
                p->drawRoundedRect(pill, h / 2, h / 2);
                p->setPen(Qt::white);
                p->drawText(pill, Qt::AlignCenter, label);
            }
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
    // 타이틀 라인 없이 [카드 리스트 | + 추가] 한 줄 — 창 하단 탭 스트립 느낌.
    //   m_title 은 setCurrentProgramName / updateHeaderLabel 이 참조하지만
    //   레이아웃에는 넣지 않는다(비가시 상태 유지).
    m_title = new QLabel(tr("프로그램"));

    // 컴팩트 모드(카드 vs. 이름 pill) 전환은 카드 더블클릭이 담당.
    m_btnAdd = new QPushButton(tr("+ 추가"));
    m_btnAdd->setToolTip(tr("현재 씬을 프로그램으로 저장"));
    connect(m_btnAdd, &QPushButton::clicked, this, &ProgramListWidget::addRequested);

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

    // [카드 리스트 stretch=1  |  + 추가 (상단 정렬)]
    //   버튼은 자기 sizeHint 만 취해 스트립 우측 상단에 고정.
    //   리스트가 setFixedHeight(96/234) 를 통해 스트립 세로 크기 결정.
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(m_list, 1);
    lay->addWidget(m_btnAdd, 0, Qt::AlignTop);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (!it) return;
        // 다른 카드의 selected 상태를 즉시 해제 — 앰버 하이라이트 잔재 방지.
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setSelected(m_list->item(i) == it);
        m_list->viewport()->update();
        emit programSelected(it->data(Qt::UserRole).toString());
    });
    // 더블클릭 = 카드/이름-pill 컴팩트 모드 토글. (구 ∧/∨ 헤더 버튼 대체)
    //  단일클릭이 이미 선택+Preview 로드를 담당하므로 더블클릭에 별도 액션을
    //  두지 않으면 무의미했던 자리를 회수한다. Live 송출은 여전히 TAKE 전용.
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*){ setCollapsed(!m_collapsed); });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &ProgramListWidget::showContextMenu);

    // 초기 상태: 펼침 (setCollapsed 와 동일 계산치).
    setFixedHeight(180);
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
        item->setData(kEndActionRole,   static_cast<int>(p.endAction));
        // Phase A: 카드 배지의 "표시 시간" 판정은 첫 페이지 기준.
        //   Phase C 에서 페이지 순회 도입 시 "합계 시간" 이나 "현재 재생 페이지
        //   시간" 등으로 재정의 예정.
        item->setData(kDisplayTimeRole, p.pages.isEmpty() ? 0
                                                          : p.pages.first().displayTimeSec);
        // 현재 접힘 상태에 맞춰 sizeHint 지정 — setPrograms 가 접힘 중에 불려도 정확.
        item->setSizeHint(m_collapsed ? QSize(200, 40) : kCardSize);
    }
    setActiveProgram(m_activeId);   // 강조 유지
}

void ProgramListWidget::selectProgram(const QString& id) {
    if (id.isEmpty()) { m_list->clearSelection(); m_list->setCurrentItem(nullptr); return; }
    // 명시적으로 각 항목 selected 상태 갱신 (다중 하이라이트 방지).
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const bool match = (it->data(Qt::UserRole).toString() == id);
        it->setSelected(match);
        if (match) m_list->setCurrentItem(it);
    }
    m_list->viewport()->update();
}

void ProgramListWidget::updateItem(const QString& id, const Program& p,
                                   const QString& thumbAbsPath) {
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() != id) continue;
        it->setText(p.name.isEmpty() ? id : p.name);
        it->setToolTip(p.name);
        QPixmap thumb;
        if (!thumbAbsPath.isEmpty() && QFileInfo::exists(thumbAbsPath))
            thumb.load(thumbAbsPath);
        it->setIcon(QIcon(thumb.isNull() ? makeDefaultThumb() : thumb));
        it->setData(kEndActionRole,   static_cast<int>(p.endAction));
        it->setData(kDisplayTimeRole, p.pages.isEmpty() ? 0
                                                        : p.pages.first().displayTimeSec);
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
    QAction* renameAct    = menu.addAction(tr("이름 변경..."));
    QAction* displayAct   = menu.addAction(tr("표시 시간 설정..."));
    QAction* endActionAct = menu.addAction(tr("종료 동작 설정..."));

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
    } else if (chosen == endActionAct) {
        emit endActionEditRequested(id);
    }
}

// ---- 접기 / 펼치기 -------------------------------------------------
// 접힘: 카드는 유지하되 썸네일 숨김 → 이름 pill 만 노출. 여러 프로그램 이름을
// 한 눈에 확인하며 스위칭 가능. 델리게이트 paint 는 property("collapsedMode") 를
// 읽어 분기.
void ProgramListWidget::setCollapsed(bool collapsed) {
    if (m_collapsed == collapsed) return;
    m_collapsed = collapsed;
    updateHeaderLabel();

    if (!m_list) { updateGeometry(); return; }

    if (auto* del = m_list->itemDelegate())
        del->setProperty("collapsedMode", collapsed);

    const QSize cell = collapsed ? QSize(200, 40) : kCardSize;
    if (collapsed) {
        m_list->setIconSize(QSize(0, 0));
        m_list->setSpacing(2);
        setFixedHeight(44);
    } else {
        m_list->setIconSize(kThumbSize);
        m_list->setSpacing(2);   // 카드 172 를 컨테이너 180 안에 맞춤
        setFixedHeight(180);
    }
    m_list->setGridSize(cell);
    // gridSize 변경이 기존 items 에 즉시 반영되도록 각 item 의 sizeHint 도 명시.
    //   (QListView 의 uniformItemSizes 캐시가 이전 크기를 붙잡는 사례 회피)
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setSizeHint(cell);
    m_list->doItemsLayout();
    m_list->viewport()->update();
    updateGeometry();
    emit collapseChanged(collapsed);
}

void ProgramListWidget::setCurrentProgramName(const QString& name) {
    if (m_currentProgramName == name) return;
    m_currentProgramName = name;
    updateHeaderLabel();
}

void ProgramListWidget::setTitleVisible(bool visible) {
    if (m_title) m_title->setVisible(visible);
}

void ProgramListWidget::updateHeaderLabel() {
    if (!m_title) return;
    if (m_collapsed) {
        // 접힌 상태: 현재 편집 대상 프로그램 이름을 헤더에 노출 → 사용자가
        // 프로그램 카드를 보지 않고도 "지금 뭐 편집중" 즉시 확인.
        m_title->setText(m_currentProgramName.isEmpty()
            ? tr("프로그램: (선택 없음)")
            : tr("프로그램: %1").arg(m_currentProgramName));
    } else {
        m_title->setText(tr("프로그램"));
    }
}

} // namespace uwp
