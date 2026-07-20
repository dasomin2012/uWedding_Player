#include "ControlWindow.h"

#include "app/Settings.h"
#include "scene/SceneModel.h"
#include "editor/PreviewCanvas.h"
#include "editor/PropertyPanel.h"
#include "editor/MediaListWidget.h"
#include "program/ProgramListWidget.h"

#include <QStyle>

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

namespace uwp {

namespace {

// ---- UI-B: QSS 테마 (밝은 / 어두운) --------------------------------
// 정제된 웜 뉴트럴. 크림/에스프레소 톤에 브론즈·샴페인 강조.
// 팔레트 원칙:
//   * 채도를 낮추고 대비를 정돈해 프리미엄 AV 콘솔 느낌.
//   * 액션 강도: 브론즈(FILL·미디어 탭·슬라이더) → 딥레드(TAKE) → 딥에스프레소(F2B).
//   * 위젯 스타일은 objectName 기반으로 분기.
const char* kQssLight = R"(
QMainWindow, QWidget#Central {
    background: #f2ede6;
    color: #1c1512;
}
QWidget { font-size: 13px; color: #1c1512; }

QFrame#Panel {
    background: #fdfaf5;
    border: 1px solid #dcd3c6;
    border-radius: 12px;
}
QLabel#PanelHeader {
    color: #8a7d70;
    font-weight: 700;
    font-size: 11px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    padding: 2px 2px 8px 2px;
    border-bottom: 1px solid #ece4d6;
    margin-bottom: 6px;
}
QPushButton {
    background: #ffffff;
    border: 1px solid #d3c8b8;
    border-radius: 6px;
    padding: 6px 12px;
    color: #1c1512;
}
QPushButton:hover { background: #f5ede0; border-color: #b98a5e; }
QPushButton:pressed { background: #ece0cc; }
QPushButton:disabled { color: #b0a695; background: #f5f0e8; border-color: #e0d8ca; }

QPushButton#TakeButton {
    background: #9d2a1f;
    color: #ffffff;
    border: 0;
    border-radius: 8px;
    font-size: 18px;
    font-weight: 800;
    padding: 14px 28px;
    letter-spacing: 0.08em;
}
QPushButton#TakeButton:hover { background: #b23629; }
QPushButton#TakeButton:pressed { background: #7d1f16; }

QPushButton#TransButton {
    background: #3d342d;
    color: #f5ede0;
    border: 0;
    border-radius: 8px;
    font-size: 15px;
    font-weight: 700;
    padding: 14px 20px;
    letter-spacing: 0.06em;
}
QPushButton#TransButton:hover  { background: #4d423a; }
QPushButton#TransButton:pressed { background: #2a2320; }

QPushButton#PreviewToolBtn {
    background: #ffffff;
    border: 1px solid #d3c8b8;
    border-radius: 6px;
    padding: 4px 12px;
    color: #1c1512;
    font-weight: 600;
    min-height: 22px;
}
QPushButton#PreviewToolBtn:hover  { background: #f5ede0; border-color: #b98a5e; }
QPushButton#PreviewToolBtn:pressed { background: #ece0cc; }
QPushButton#PreviewToolBtn:disabled {
    color: #b0a695; background: #f5f0e8; border-color: #e0d8ca;
}
QLabel#PreviewTimeLabel {
    color: #8a7d70;
    font-weight: 600;
    padding: 2px 8px;
    background: #f5ede0;
    border-radius: 6px;
}

QPushButton#MediaTab {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 5px 14px;
    color: #8a7d70;
    font-weight: 600;
}
QPushButton#MediaTab:hover { background: #f5ede0; color: #1c1512; }
QPushButton#MediaTab:checked {
    background: #1c1512;
    color: #f5ede0;
    border: 1px solid #1c1512;
}

QListWidget, QListView {
    background: transparent;
    border: 0;
    color: #1c1512;
    show-decoration-selected: 0;
}
QListWidget::item, QListView::item {
    color: #1c1512;
}

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #ffffff;
    border: 1px solid #d3c8b8;
    border-radius: 5px;
    padding: 4px 8px;
    color: #1c1512;
    min-height: 22px;
    selection-background-color: #8a5a3b;
    selection-color: #ffffff;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #b98a5e;
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #e4dbcb;
    height: 5px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #8a5a3b;
    width: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #b98a5e;
    border-radius: 2px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; color: #1c1512; }
QMenuBar::item:selected { background: #ece0cc; }
QMenu {
    background: #fdfaf5;
    border: 1px solid #d3c8b8;
    padding: 4px;
    border-radius: 8px;
    color: #1c1512;
}
QMenu::item { padding: 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: #ece0cc; }
QMenu::separator { height: 1px; background: #e4dbcb; margin: 4px 6px; }

QStatusBar { background: transparent; color: #8a7d70; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 5px;
    background: transparent;
    color: #1c1512;
}
QToolBar QToolButton:hover { background: #ece0cc; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical {
    background: #d3c8b8;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #8a5a3b; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #d3c8b8;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #8a5a3b; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #0e0d0c; color: #6a5f55; border-radius: 6px; }
)";

const char* kQssDark = R"(
QMainWindow, QWidget#Central {
    background: #141210;
    color: #e8ddd0;
}
QWidget { font-size: 13px; color: #e8ddd0; }

QFrame#Panel {
    background: #1e1a17;
    border: 1px solid #2c2620;
    border-radius: 12px;
}
QLabel#PanelHeader {
    color: #8f857a;
    font-weight: 700;
    font-size: 11px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    padding: 2px 2px 8px 2px;
    border-bottom: 1px solid #2c2620;
    margin-bottom: 6px;
}
QPushButton {
    background: #26211c;
    border: 1px solid #3a322c;
    border-radius: 6px;
    padding: 6px 12px;
    color: #e8ddd0;
}
QPushButton:hover { background: #33291f; border-color: #c8a37a; }
QPushButton:pressed { background: #40342a; }
QPushButton:disabled { color: #5c534a; background: #1a1613; border-color: #2c2620; }

QPushButton#TakeButton {
    background: #d63324;
    color: #ffffff;
    border: 0;
    border-radius: 8px;
    font-size: 18px;
    font-weight: 800;
    padding: 14px 28px;
    letter-spacing: 0.08em;
}
QPushButton#TakeButton:hover { background: #e64535; }
QPushButton#TakeButton:pressed { background: #b02818; }

QPushButton#TransButton {
    background: #c8a37a;
    color: #14100c;
    border: 0;
    border-radius: 8px;
    font-size: 15px;
    font-weight: 700;
    padding: 14px 20px;
    letter-spacing: 0.06em;
}
QPushButton#TransButton:hover  { background: #d6b48e; }
QPushButton#TransButton:pressed { background: #a88760; }

QPushButton#PreviewToolBtn {
    background: #26211c;
    border: 1px solid #3a322c;
    border-radius: 6px;
    padding: 4px 12px;
    color: #e8ddd0;
    font-weight: 600;
    min-height: 22px;
}
QPushButton#PreviewToolBtn:hover  { background: #33291f; border-color: #c8a37a; }
QPushButton#PreviewToolBtn:pressed { background: #40342a; }
QPushButton#PreviewToolBtn:disabled {
    color: #5c534a; background: #1a1613; border-color: #2c2620;
}
QLabel#PreviewTimeLabel {
    color: #8f857a;
    font-weight: 600;
    padding: 2px 8px;
    background: #26211c;
    border-radius: 6px;
}

QPushButton#MediaTab {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 5px 14px;
    color: #8f857a;
    font-weight: 600;
}
QPushButton#MediaTab:hover { background: #26211c; color: #e8ddd0; }
QPushButton#MediaTab:checked {
    background: #c8a37a;
    color: #14100c;
    border: 1px solid #c8a37a;
}

QListWidget, QListView {
    background: transparent;
    border: 0;
    color: #e8ddd0;
    show-decoration-selected: 0;
}
QListWidget::item, QListView::item {
    color: #e8ddd0;
}

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #26211c;
    border: 1px solid #3a322c;
    border-radius: 5px;
    padding: 4px 8px;
    color: #e8ddd0;
    min-height: 22px;
    selection-background-color: #c8a37a;
    selection-color: #14100c;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #c8a37a;
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #2c2620;
    height: 5px;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #c8a37a;
    width: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #c8a37a;
    border-radius: 2px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; color: #e8ddd0; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #33291f; }
QMenu {
    background: #1e1a17;
    border: 1px solid #3a322c;
    padding: 4px;
    border-radius: 8px;
    color: #e8ddd0;
}
QMenu::item { padding: 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: #33291f; }
QMenu::separator { height: 1px; background: #2c2620; margin: 4px 6px; }

QStatusBar { background: transparent; color: #8f857a; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 5px;
    background: transparent;
    color: #e8ddd0;
}
QToolBar QToolButton:hover { background: #33291f; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical {
    background: #3a322c;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #c8a37a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #3a322c;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #c8a37a; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #050403; color: #4a4038; border-radius: 6px; }
)";

// ---- 패널 래퍼 헬퍼: 내부 위젯을 둥근 프레임 + 선택적 상단 헤더에 담아 반환 ----
// title 이 비면 헤더 없이 프레임만 (내부 위젯이 자체 제목을 가진 경우 사용).
QWidget* wrapPanel(QWidget* content, const QString& title = QString()) {
    auto* frame = new QFrame;
    frame->setObjectName("Panel");
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);
    if (!title.isEmpty()) {
        auto* h = new QLabel(title);
        h->setObjectName("PanelHeader");
        v->addWidget(h);
    }
    v->addWidget(content, 1);
    return frame;
}

} // namespace

// ---- ControlWindow ----------------------------------------------
ControlWindow::ControlWindow(Settings* settings, SceneModel* scene,
                             SnapshotCache* snapshots, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_scene(scene)
    , m_snapshots(snapshots)
{
    setWindowTitle("uWeddingPlayer — Control");
    resize(1600, 940);

    createMenus();
    createCentralLayout();

    // UI-B: 저장된 테마 로드 후 적용.
    QSettings qs("Hanmac", "uWeddingPlayer");
    const QString saved = qs.value("ui/theme", "light").toString();
    applyTheme(saved);

    statusBar()->showMessage("Ready");
}

ControlWindow::~ControlWindow() = default;

void ControlWindow::createMenus() {
    auto* fileMenu    = menuBar()->addMenu(tr("&File"));
    auto* settingsAct = fileMenu->addAction(tr("&Settings..."));
    connect(settingsAct, &QAction::triggered,
            this, &ControlWindow::openSettingsRequested);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("E&xit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* sceneMenu = menuBar()->addMenu(tr("&Scene"));
    auto* saveAct   = sceneMenu->addAction(tr("&Save Scene"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &ControlWindow::saveSceneRequested);
    auto* loadAct = sceneMenu->addAction(tr("&Load Scene"));
    connect(loadAct, &QAction::triggered, this, &ControlWindow::loadSceneRequested);
    sceneMenu->addSeparator();
    auto* clearAct = sceneMenu->addAction(tr("&Clear Scene"));
    connect(clearAct, &QAction::triggered, this, [this]{ m_scene->clear(); });

    auto* toolsMenu  = menuBar()->addMenu(tr("&Tools"));
    auto* monitorAct = toolsMenu->addAction(tr("Select Output &Monitor..."));
    connect(monitorAct, &QAction::triggered,
            this, &ControlWindow::selectOutputMonitorRequested);
    auto* playTestAct = toolsMenu->addAction(tr("Play &Test Video..."));
    connect(playTestAct, &QAction::triggered,
            this, &ControlWindow::playTestVideoRequested);
    toolsMenu->addSeparator();

    // UI-B: 다크 모드 토글
    m_darkThemeAct = toolsMenu->addAction(tr("&다크 모드"));
    m_darkThemeAct->setCheckable(true);
    m_darkThemeAct->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(m_darkThemeAct, &QAction::triggered, this, &ControlWindow::toggleTheme);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(saveAct);
    toolbar->addAction(loadAct);
    toolbar->addAction(clearAct);
    toolbar->addSeparator();
    toolbar->addAction(monitorAct);
    toolbar->addAction(playTestAct);
}

// ================================================================
// UI-A: 3-컬럼 레이아웃
//   +-----------+---------------------+-----------+
//   |           |   Preview Canvas    |  Live     |
//   | Media     |                     |  Mirror   |
//   | Library   +---------------------+           |
//   | (세로)    |                     +-----------+
//   |           |   Programs / Pages  |           |
//   |           |                     | Property  |
//   +-----------+---------------------+-----------+
//   | GlobalBar: F2B / TAKE / 전환 / BGM              |
//   +-------------------------------------------------+
// ================================================================
void ControlWindow::createCentralLayout() {
    // ----- 미디어 리스트 -----
    m_mediaList = new MediaListWidget(m_settings, m_snapshots);
    m_mediaList->setMinimumWidth(200);
    connect(m_mediaList, &MediaListWidget::mediaActivated,
            this, [this](const QString& path) {
                const QSize cs = m_scene->canvasSize();
                // 기본 16:9. 캔버스가 어떤 비율이든 새 레이어는 16:9 로 시작 —
                // 결혼식장 LED 는 대부분 16:9 소스 재생용.
                const qreal w = cs.width() * 0.4;
                const qreal h = w * 9.0 / 16.0;
                m_scene->addLayer(path,
                    QRectF((cs.width() - w) / 2.0,
                           (cs.height() - h) / 2.0, w, h));
            });

    // ----- Preview 캔버스 -----
    m_canvas = new PreviewCanvas(m_scene, m_snapshots);

    // ----- Live 미러 -----
    m_liveMirror = new QLabel;
    m_liveMirror->setObjectName("LiveMirror");
    m_liveMirror->setAlignment(Qt::AlignCenter);
    m_liveMirror->setMinimumSize(240, 135);   // 16:9 mini

    // ----- Programs (UI-C 카드형) -----
    m_programList = new ProgramListWidget;
    m_programList->setMinimumHeight(200);   // 카드 세로(172) + 여유

    // ----- Property Panel -----
    m_property = new PropertyPanel(m_scene);

    // ----- UI-C: TAKE 클러스터 (Live 미러 아래 배치) -----
    m_takeButton = new QPushButton(tr("TAKE"));
    m_takeButton->setObjectName("TakeButton");
    m_takeButton->setMinimumHeight(52);
    connect(m_takeButton, &QPushButton::clicked,
            this, &ControlWindow::takeRequested);

    // 전환 모드 토글 — 한 버튼으로 Fade ↔ Cut. 라벨 = 현재 모드.
    // TAKE 와 동일한 세로 높이 + 유사 스타일(TransButton QSS), 폭은 컴팩트.
    m_btnTransition = new QPushButton;
    m_btnTransition->setObjectName("TransButton");
    m_btnTransition->setMinimumHeight(52);
    m_btnTransition->setMinimumWidth(90);
    {
        const QString dm = m_settings ? m_settings->takeDefaultMode().toLower()
                                      : QString("fade");
        m_btnTransition->setText(dm == "cut" ? QStringLiteral("Cut")
                                             : QStringLiteral("Fade"));
    }
    m_btnTransition->setToolTip(tr("클릭하여 전환 모드 바꾸기 (Fade ↔ Cut)"));
    connect(m_btnTransition, &QPushButton::clicked, this, [this]() {
        const bool isFade = (m_btnTransition->text() == QLatin1String("Fade"));
        const QString next     = isFade ? QStringLiteral("Cut") : QStringLiteral("Fade");
        m_btnTransition->setText(next);
        emit takeModeChanged(next.toLower());
    });

    // ----- 컬럼 조립 -----

    // LEFT: 미디어 (자체 헤더가 있으므로 외부 타이틀 없이 프레임만)
    auto* leftPanel = wrapPanel(m_mediaList);

    // CENTER TOP: Preview (툴바 + 캔버스)
    auto* previewPanel = wrapPanel(buildPreviewPane(), tr("작업 캔버스"));

    // CENTER BOTTOM: Programs (자체 헤더 있음)
    auto* programPanel = wrapPanel(m_programList);

    auto* centerCol = new QSplitter(Qt::Vertical);
    centerCol->addWidget(previewPanel);
    centerCol->addWidget(programPanel);
    centerCol->setStretchFactor(0, 3);
    centerCol->setStretchFactor(1, 1);
    centerCol->setSizes({ 560, 240 });

    // RIGHT TOP: Live mirror + TAKE 클러스터
    auto* livePanel = wrapPanel(buildLivePanel(), tr("Live 송출"));

    // RIGHT BOTTOM: Property (자체 헤더 있음). 세로 공간이 부족할 때 하단
    // 버튼(레이어 삭제 등)이 잘리지 않도록 QScrollArea 로 감싸 스크롤 확보.
    auto* propScroll = new QScrollArea;
    propScroll->setWidget(m_property);
    propScroll->setWidgetResizable(true);
    propScroll->setFrameShape(QFrame::NoFrame);
    propScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* propPanel = wrapPanel(propScroll);

    auto* rightCol = new QSplitter(Qt::Vertical);
    rightCol->addWidget(livePanel);
    rightCol->addWidget(propPanel);
    rightCol->setStretchFactor(0, 0);
    rightCol->setStretchFactor(1, 1);
    rightCol->setSizes({ 340, 500 });

    // OUTER: 3-column horizontal splitter
    auto* outer = new QSplitter(Qt::Horizontal);
    outer->addWidget(leftPanel);
    outer->addWidget(centerCol);
    outer->addWidget(rightCol);
    outer->setStretchFactor(0, 0);
    outer->setStretchFactor(1, 1);
    outer->setStretchFactor(2, 0);
    outer->setSizes({ 260, 940, 340 });

    // ROOT
    auto* central = new QWidget(this);
    central->setObjectName("Central");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(outer, 1);
    setCentralWidget(central);
}

// UI-F: 중앙 상단 Preview 패널 조립. 인라인 툴바 + 캔버스.
//   [▶]   표시 시간: MM:SS                                        [🗑]
//  + 레이어 버튼은 미디어 라이브러리 드래그앤드롭과 중복이라 배치하지 않음.
//  🗑 = 선택 레이어 삭제. 이전엔 속성 패널 하단에 있어 스크롤해 찾아야 했다.
QWidget* ControlWindow::buildPreviewPane() {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(6);

    m_btnPreviewPlay = new QPushButton(QStringLiteral("▶"));
    m_btnPreviewPlay->setObjectName("PreviewToolBtn");
    m_btnPreviewPlay->setEnabled(false);   // placeholder — 후속 트랙
    m_btnPreviewPlay->setToolTip(tr("미리보기 재생 (구현 예정)"));

    m_timeLabel = new QLabel(tr("표시 시간: —"));
    m_timeLabel->setObjectName("PreviewTimeLabel");

    // 우측: 캔버스에 꽉 채우기 (선택 레이어 → geometry = 캔버스 전체).
    //   최대화 창 아이콘(SP_TitleBarMaxButton)이 시각적으로 "꽉 채우기" 의미
    //   전달에 가장 근접. Windows/Linux 모두 네이티브 스타일 아이콘 제공.
    m_btnFillCanvas = new QPushButton;
    m_btnFillCanvas->setObjectName("PreviewToolBtn");
    m_btnFillCanvas->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_btnFillCanvas->setToolTip(tr("선택된 레이어를 캔버스에 꽉 채우기"));
    m_btnFillCanvas->setEnabled(false);
    connect(m_btnFillCanvas, &QPushButton::clicked, this, [this]{
        if (!m_scene || m_scene->selectedId().isEmpty()) return;
        const QSize cs = m_scene->canvasSize();
        m_scene->setGeometry(m_scene->selectedId(),
                             QRectF(0, 0, cs.width(), cs.height()));
    });

    // 우측 끝: 선택 레이어 삭제 (플랫폼 네이티브 휴지통 아이콘).
    m_btnDeleteLayer = new QPushButton;
    m_btnDeleteLayer->setObjectName("PreviewToolBtn");
    m_btnDeleteLayer->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_btnDeleteLayer->setToolTip(tr("선택된 레이어 삭제"));
    m_btnDeleteLayer->setEnabled(false);   // 선택 있을 때만 활성
    connect(m_btnDeleteLayer, &QPushButton::clicked, this, [this]{
        if (m_scene && !m_scene->selectedId().isEmpty())
            m_scene->removeLayer(m_scene->selectedId());
    });
    // 선택 상태 → 두 아이콘 버튼 활성 동기화.
    if (m_scene) {
        connect(m_scene, &SceneModel::selectionChanged, this,
                [this](const QString& id) {
                    const bool has = !id.isEmpty();
                    if (m_btnFillCanvas)  m_btnFillCanvas->setEnabled(has);
                    if (m_btnDeleteLayer) m_btnDeleteLayer->setEnabled(has);
                });
        const bool has = !m_scene->selectedId().isEmpty();
        m_btnFillCanvas->setEnabled(has);
        m_btnDeleteLayer->setEnabled(has);
    }

    toolbar->addWidget(m_btnPreviewPlay);
    toolbar->addSpacing(8);
    toolbar->addWidget(m_timeLabel);
    toolbar->addStretch(1);
    toolbar->addWidget(m_btnFillCanvas);    // 우측: 꽉 채우기
    toolbar->addWidget(m_btnDeleteLayer);   // 우측 끝: 삭제

    v->addLayout(toolbar);
    v->addWidget(m_canvas, 1);
    return box;
}

void ControlWindow::setPreviewDisplayTime(int seconds) {
    if (!m_timeLabel) return;
    if (seconds < 0) {
        m_timeLabel->setText(tr("표시 시간: —"));
    } else if (seconds == 0) {
        m_timeLabel->setText(tr("표시 시간: 수동"));
    } else {
        const int m = seconds / 60;
        const int s = seconds % 60;
        m_timeLabel->setText(tr("표시 시간: %1:%2")
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
    }
}

// UI-C: 우측 상단 Live 패널 조립. mirror 위, TAKE + 전환 토글 아래(한 줄).
QWidget* ControlWindow::buildLivePanel() {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    v->addWidget(m_liveMirror, 1);

    // [       TAKE       ] [Fade/Cut] — TAKE 는 늘어남, 전환 버튼은 컴팩트.
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_takeButton, 1);
    row->addWidget(m_btnTransition);
    v->addLayout(row);

    return box;
}

// ---- UI-B: 테마 적용 / 토글 ----------------------------------------
void ControlWindow::applyTheme(const QString& theme) {
    m_currentTheme = (theme == "dark") ? "dark" : "light";
    const char* qss = (m_currentTheme == "dark") ? kQssDark : kQssLight;
    qApp->setStyleSheet(QString::fromUtf8(qss));
    if (m_darkThemeAct)
        m_darkThemeAct->setChecked(m_currentTheme == "dark");
}

void ControlWindow::toggleTheme() {
    const QString next = (m_currentTheme == "dark") ? "light" : "dark";
    applyTheme(next);
    QSettings qs("Hanmac", "uWeddingPlayer");
    qs.setValue("ui/theme", next);
}

void ControlWindow::setStatusText(const QString& text) {
    statusBar()->showMessage(text);
}

// LiveMirror: 폴링된 프레임을 우상단 Live 패널에 반영.
//   - 빈 이미지는 무시(이전 프레임 유지 → 링크 끊길 때 시각적 안정성).
//   - QLabel 크기에 맞춰 aspect-fit 스케일. 원본 색공간(RGB32/PNG) 그대로.
void ControlWindow::setLiveMirrorImage(const QImage& img) {
    if (!m_liveMirror || img.isNull()) return;
    const QSize labelSize = m_liveMirror->size();
    if (labelSize.width() <= 0 || labelSize.height() <= 0) return;
    m_liveMirror->setPixmap(QPixmap::fromImage(img).scaled(
        labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace uwp
