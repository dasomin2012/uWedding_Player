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
#include <QPainterPath>
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

// 미디어 리스트 델리게이트 (썸네일만, 파일명은 툴팁으로).
//   한 행 = 한 미디어 = 썸네일 aspect-fill 크롭. 파일명은 delegate 에 그리지
//   않고 QListWidgetItem::toolTip 으로 마우스 오버 시 표시.
//   선택 하이라이트가 이미지와 정확히 일치.
//   hover/selected 시 우상단 X 로 소스 제거.
class MediaTileDelegate : public QStyledItemDelegate {
public:
    using RemoveHandler = std::function<void(int row)>;
    static constexpr int kRowW     = 128;   // 셀 폭 (좌측 미디어 패널의 기본 너비)
    static constexpr int kRowH     = 72;    // 행 높이
    static constexpr int kPad      = 6;
    static constexpr int kBtnSize  = 20;
    static constexpr int kBtnPad   = 4;

    MediaTileDelegate(RemoveHandler onRemove, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_onRemove(std::move(onRemove)) {}

    QSize sizeHint(const QStyleOptionViewItem&,
                   const QModelIndex&) const override {
        return QSize(kRowW, kRowH);
    }

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

        // 썸네일 — 셀 전체를 aspect-fill 로 채움. 파일명은 툴팁으로만 표시.
        const QIcon icon = idx.data(Qt::DecorationRole).value<QIcon>();
        const QRect thumbRect = r.adjusted(2, 2, -2, -2);
        if (!icon.isNull()) {
            const QSize pxSize = thumbRect.size() * 2;   // HiDPI 여유
            const QPixmap pm = icon.pixmap(pxSize);
            if (!pm.isNull()) {
                p->save();
                QPainterPath clip;
                clip.addRoundedRect(thumbRect, 4, 4);
                p->setClipPath(clip);
                QPixmap scaled = pm.scaled(
                    thumbRect.size(), Qt::KeepAspectRatioByExpanding,
                    Qt::SmoothTransformation);
                const int sx = (scaled.width()  - thumbRect.width())  / 2;
                const int sy = (scaled.height() - thumbRect.height()) / 2;
                p->drawPixmap(thumbRect,
                              scaled, QRect(sx, sy, thumbRect.width(),
                                                    thumbRect.height()));
                p->restore();
            }
        } else {
            // 스냅샷 준비 전 placeholder — 얇은 프레임.
            p->setPen(QPen(opt.palette.color(QPalette::Mid), 1));
            p->setBrush(opt.palette.color(QPalette::AlternateBase));
            p->drawRoundedRect(thumbRect, 4, 4);
        }

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

    // 타입 필터 — 영상 / 이미지 두 개 토글. 둘 다 unchecked = 전체 표시,
    // 하나만 checked = 그 타입만 필터. 좁은 폭에서 "전체" 라벨 잘림 회피 +
    // 없어도 초기 상태가 곧 "전체" 라 UX 손실 없음.
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(4);
    m_typeGroup = new QButtonGroup(this);
    m_typeGroup->setExclusive(false);   // 상호 배타 아님 — 각각 독립 토글
    struct TabDef { QString label; int value; };
    const TabDef defs[] = {
        { tr("영상"),   static_cast<int>(MediaType::Video) },
        { tr("이미지"), static_cast<int>(MediaType::Image) },
    };
    for (const auto& d : defs) {
        auto* b = new QPushButton(d.label);
        b->setCheckable(true);
        b->setObjectName("MediaTab");
        tabRow->addWidget(b);
        m_typeGroup->addButton(b, d.value);
    }
    tabRow->addStretch(1);
    m_typeFilter = -1;   // 초기: 필터 없음 = 전체

    // 가로 행 리스트 (redesign) — 한 행 = 한 미디어. 셀 폭은 view 전체.
    m_list = new MediaList(this);
    m_list->setViewMode(QListView::ListMode);
    m_list->setIconSize(QSize(240, 144));            // 델리게이트가 aspect-fill 로 크롭. 원본 해상도 여유롭게.
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(2);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    // 필터 토글 — 하나 켜면 다른 하나 자동 꺼짐(상호 배타적 결과). 이미 켜진
    // 버튼 재클릭 시 해제 → 필터 없음(전체). 초기값 -1 = 전체.
    connect(m_typeGroup,
            QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) {
                auto* clicked = m_typeGroup->button(id);
                if (!clicked) return;
                if (clicked->isChecked()) {
                    // 다른 버튼 자동 해제.
                    for (auto* b : m_typeGroup->buttons())
                        if (b != clicked && b->isChecked())
                            b->setChecked(false);
                    m_typeFilter = id;
                } else {
                    m_typeFilter = -1;   // 재클릭으로 해제 → 전체
                }
                applyFilter();
            });

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
