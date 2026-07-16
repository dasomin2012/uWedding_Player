#include "MediaListWidget.h"

#include "app/Settings.h"
#include "player/SnapshotCache.h"
#include "scene/Layer.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <functional>

namespace uwp {

static const char* kMediaMime = "application/x-uwp-media-path";
// 항목이 어느 소스(파일 자신 또는 상위 폴더)에서 왔는지 보관 → 소스 단위 제거.
static constexpr int kSourceRole    = Qt::UserRole + 1;
// UI-E: 타입 탭 필터 매칭용 캐시(재분류 비용 회피).
static constexpr int kMediaTypeRole = Qt::UserRole + 2;

// 드래그 시 커스텀 MIME(파일 경로) 제공.
class MediaList : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    // Qt 5.15 시그니처: 값 전달 (Qt6 는 const ref)
    QMimeData* mimeData(const QList<QListWidgetItem*> items) const override {
        auto* md = new QMimeData;
        if (!items.isEmpty()) {
            const QString p = items.first()->data(Qt::UserRole).toString();
            md->setData(kMediaMime, p.toUtf8());
            md->setUrls({ QUrl::fromLocalFile(p) });
        }
        return md;
    }
};

// UI-E: 미디어 타일 델리게이트.
//  - 카드 = [상단 아이콘 128×72] + [하단 파일명 2줄 랩·중앙정렬].
//    QStyledItemDelegate 기본 IconMode 페인트는 setUniformItemSizes/QSS
//    조합에서 파일명이 클리핑되어 보이지 않는 사례가 있어 직접 그린다.
//  - hover 또는 selected 상태에서 우상단 X 표시.
//  - X 클릭은 editorEvent 에서 소비하여 드래그·선택 이동 억제 후 콜백 호출.
//  - MOC 회피 위해 Q_OBJECT/signals 대신 std::function 콜백.
class MediaTileDelegate : public QStyledItemDelegate {
public:
    using RemoveHandler = std::function<void(int row)>;
    static constexpr int kIconW    = 128;
    static constexpr int kIconH    = 72;
    static constexpr int kPad      = 6;
    static constexpr int kBtnSize  = 20;
    static constexpr int kBtnPad   = 4;

    MediaTileDelegate(RemoveHandler onRemove, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_onRemove(std::move(onRemove)) {}

    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);

        const QRect r    = opt.rect.adjusted(kPad/2, kPad/2, -kPad/2, -kPad/2);
        const bool hover = (opt.state & QStyle::State_MouseOver);
        const bool sel   = (opt.state & QStyle::State_Selected);

        // 카드 배경 — hover/selected 강조
        if (sel) {
            p->setPen(Qt::NoPen);
            p->setBrush(opt.palette.brush(QPalette::Highlight));
            p->drawRoundedRect(r, 6, 6);
        } else if (hover) {
            p->setPen(Qt::NoPen);
            QColor hi = opt.palette.color(QPalette::Highlight);
            hi.setAlpha(30);
            p->setBrush(hi);
            p->drawRoundedRect(r, 6, 6);
        }

        // 아이콘 — 상단 중앙 (실제 픽스맵 크기와 kIconW/H 중 작은 쪽 사용)
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        const QRect iconRect(r.x() + (r.width() - kIconW) / 2,
                             r.y() + kPad, kIconW, kIconH);
        if (!icon.isNull()) {
            icon.paint(p, iconRect, Qt::AlignCenter, QIcon::Normal);
        } else {
            // 스냅샷 준비 전 placeholder — 얇은 프레임
            p->setPen(QPen(opt.palette.color(QPalette::Mid), 1));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(iconRect, 4, 4);
        }

        // 파일명 — 하단, 2줄 word-wrap, 중앙 정렬, elide
        const QRect textRect(r.x() + kPad,
                             iconRect.bottom() + kPad,
                             r.width() - kPad * 2,
                             r.bottom() - iconRect.bottom() - kPad * 2);
        p->setPen(sel ? opt.palette.color(QPalette::HighlightedText)
                      : opt.palette.color(QPalette::Text));
        p->setFont(opt.font);
        // Qt 는 TextWordWrap 시 자동 두 줄 배치. 텍스트가 넘치면 마지막 줄 elide.
        const QString text = idx.data(Qt::DisplayRole).toString();
        QTextOption topt(Qt::AlignHCenter | Qt::AlignTop);
        topt.setWrapMode(QTextOption::WrapAnywhere);
        p->drawText(textRect, text, topt);

        // 우상단 X — hover 또는 selected
        if (hover || sel) {
            const QRect xr = xRect(opt.rect);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 200));
            p->drawEllipse(xr);
            QPen pen(Qt::white, 2.0);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            const int m = 6;
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
            if (me->button() == Qt::LeftButton
                && xRect(opt.rect).contains(me->pos())) {
                if (m_onRemove) m_onRemove(idx.row());
                return true;    // 드래그·선택 이동 억제
            }
        }
        return false;
    }

private:
    static QRect xRect(const QRect& r) {
        return QRect(r.right() - kBtnSize - kBtnPad,
                     r.top()   + kBtnPad,
                     kBtnSize, kBtnSize);
    }
    RemoveHandler m_onRemove;
};

MediaListWidget::MediaListWidget(Settings* settings, SnapshotCache* snapshots,
                                 QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_snapshots(snapshots)
{
    auto* title = new QLabel(tr("미디어 라이브러리"));
    title->setStyleSheet("font-weight: bold;");

    auto* btnFile   = new QPushButton(tr("파일 추가"));
    auto* btnFolder = new QPushButton(tr("폴더 추가"));
    auto* btnRow    = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addWidget(btnFile);
    btnRow->addWidget(btnFolder);

    // UI-E: 타입 탭 (3개 exclusive 버튼) — 전체 / 영상 / 이미지.
    //  * 문서(PDF/PPT)는 "전체"에서만 보임 — 별도 탭 없음.
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(4);
    m_typeGroup = new QButtonGroup(this);
    m_typeGroup->setExclusive(true);
    struct TabDef { QString label; int value; };
    const TabDef defs[] = {
        { tr("전체"),   -1 },
        { tr("영상"),   static_cast<int>(MediaType::Video) },
        { tr("이미지"), static_cast<int>(MediaType::Image) },
    };
    for (const auto& d : defs) {
        auto* b = new QPushButton(d.label);
        b->setCheckable(true);
        b->setObjectName("MediaTab");     // QSS 훅
        if (d.value == -1) b->setChecked(true);
        tabRow->addWidget(b);
        m_typeGroup->addButton(b, d.value);
    }
    tabRow->addStretch(1);

    // UI-E: 타일 그리드
    m_list = new MediaList(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(128, 72));            // 16:9 미리보기
    m_list->setGridSize(QSize(148, 118));           // 아이콘 + 두 줄 파일명 여유
    m_list->setResizeMode(QListView::Adjust);       // 컬럼수 자동 재계산
    m_list->setMovement(QListView::Static);         // 드래그 재배치 금지
    m_list->setWordWrap(true);                      // 긴 파일명 두 줄 랩
    m_list->setUniformItemSizes(true);              // 그리드 성능
    m_list->setSpacing(4);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // hover 상태 즉시 갱신(카드 위 X 버튼 표시).
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    m_list->setMouseTracking(true);
    m_list->setItemDelegate(new MediaTileDelegate(
        [this](int row){ removeSourceAt(row); }, m_list));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(title);
    lay->addLayout(btnRow);
    lay->addLayout(tabRow);
    lay->addWidget(m_list, 1);

    connect(btnFile,   &QPushButton::clicked, this, &MediaListWidget::openFile);
    connect(btnFolder, &QPushButton::clicked, this, &MediaListWidget::openFolder);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &MediaListWidget::onItemDoubleClicked);
    if (m_snapshots) {
        connect(m_snapshots, &SnapshotCache::snapshotReady,
                this, &MediaListWidget::onSnapshotReady);
    }
    // UI-E: 필터 변경 → 재적용
    connect(m_typeGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id){ m_typeFilter = id; applyFilter(); });

    // 이전 실행에서 기억한 소스 복원 (폴더는 재스캔)
    if (m_settings) m_sources = m_settings->mediaSources();
    rebuildList();
}

bool MediaListWidget::isSupported(const QString& path) const {
    return guessMediaType(path) != MediaType::Unknown;
}

QStringList MediaListWidget::scanFolder(const QString& dir) const {
    static const QStringList filters = {
        "*.mp4","*.mov","*.avi","*.mkv","*.wmv","*.m4v","*.webm",
        "*.png","*.jpg","*.jpeg","*.bmp","*.gif","*.webp","*.tif","*.tiff",
        "*.pdf","*.ppt","*.pptx"
    };
    QStringList out;
    const QFileInfoList entries =
        QDir(dir).entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : entries) out << fi.absoluteFilePath();
    return out;
}

void MediaListWidget::addPath(const QString& path, const QString& source) {
    if (!QFileInfo::exists(path) || !isSupported(path)) return;
    // 중복 방지 (다른 소스에서 같은 파일이 또 들어와도 한 번만 표시)
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == path) return;

    auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
    item->setData(Qt::UserRole, path);
    item->setData(kSourceRole, source);
    item->setData(kMediaTypeRole, static_cast<int>(guessMediaType(path)));
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    item->setToolTip(source == path ? path
                                     : QString("%1\n(folder: %2)").arg(path, source));
    item->setIcon(QIcon());   // 스냅샷 준비 전 빈 아이콘

    if (m_snapshots) {
        const QImage c = m_snapshots->cached(path);
        if (!c.isNull()) item->setIcon(QPixmap::fromImage(c));
        else             m_snapshots->request(path);
    }
}

// m_sources(파일/폴더) → 목록 전체 재구성. 폴더는 현재 내용으로 재스캔.
void MediaListWidget::rebuildList() {
    m_list->clear();
    for (const QString& src : m_sources) {
        const QFileInfo fi(src);
        if (fi.isDir()) {
            for (const QString& f : scanFolder(src)) addPath(f, src);
        } else if (fi.isFile()) {
            addPath(src, src);
        }
        // 존재하지 않는 소스(미연결 드라이브 등)는 표시만 건너뜀 — 목록엔 유지.
    }
    applyFilter();
}

// UI-E: 타입 탭(-1=전체) 기준으로 각 항목 setHidden 갱신.
// 목록 자체는 재구성하지 않음.
void MediaListWidget::applyFilter() {
    const int t = m_typeFilter;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const bool ok = (t < 0) || (it->data(kMediaTypeRole).toInt() == t);
        it->setHidden(!ok);
    }
}

void MediaListWidget::persistSources() {
    if (!m_settings) return;
    m_settings->setMediaSources(m_sources);
    m_settings->save();   // 즉시 디스크 반영 (다음 실행 복원 보장)
}

void MediaListWidget::openFile() {
    const QString start = m_settings ? m_settings->mediaDir() : QString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("파일 열기"), start,
        tr("미디어 (*.mp4 *.mov *.avi *.mkv *.wmv *.png *.jpg *.jpeg *.bmp "
           "*.gif *.webp *.pdf);;모든 파일 (*.*)"));
    if (files.isEmpty()) return;
    for (const QString& f : files)
        if (isSupported(f) && !m_sources.contains(f)) m_sources << f;
    if (m_settings)
        m_settings->setMediaDir(QFileInfo(files.first()).absolutePath());
    rebuildList();
    persistSources();
}

void MediaListWidget::openFolder() {
    const QString start = m_settings ? m_settings->mediaDir() : QString();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("폴더 열기"), start);
    if (dir.isEmpty()) return;
    if (!m_sources.contains(dir)) m_sources << dir;
    if (m_settings) m_settings->setMediaDir(dir);
    rebuildList();
    persistSources();
}

void MediaListWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) emit mediaActivated(item->data(Qt::UserRole).toString());
}

void MediaListWidget::onSnapshotReady(const QString& mediaPath,
                                      const QImage& image) {
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() == mediaPath)
            it->setIcon(QPixmap::fromImage(image));
    }
}

// UI-E: 카드 X 버튼 → 그 카드의 소스(파일 또는 폴더) 제거.
// 폴더 소스는 그 폴더에서 온 모든 항목이 사라지므로 확인 받는다.
void MediaListWidget::removeSourceAt(int row) {
    if (row < 0 || row >= m_list->count()) return;
    QListWidgetItem* it = m_list->item(row);
    if (!it) return;
    const QString src = it->data(kSourceRole).toString();
    if (src.isEmpty()) return;

    const QFileInfo fi(src);
    if (fi.isDir()) {
        const auto r = QMessageBox::question(
            this, tr("목록에서 제거"),
            tr("폴더 \"%1\" 을(를) 목록에서 제거할까요?\n"
               "(폴더에서 불러온 모든 항목이 사라집니다)")
                .arg(fi.fileName().isEmpty() ? src : fi.fileName()));
        if (r != QMessageBox::Yes) return;
    }

    m_sources.removeAll(src);
    rebuildList();
    persistSources();
}

} // namespace uwp
