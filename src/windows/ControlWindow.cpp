#include "ControlWindow.h"

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#include "app/Settings.h"
#include "scene/SceneModel.h"
#include "editor/PreviewCanvas.h"
#include "editor/PropertyPanel.h"
#include "editor/MediaListWidget.h"
#include "program/PageListWidget.h"
#include "program/ProgramListWidget.h"

#include <QStyle>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QSizePolicy>
#include <QTime>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
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

/* 대화상자·메시지박스 — 테마 배경과 매칭. */
QDialog, QMessageBox, QInputDialog {
    background: #fdfaf5;
    color: #1c1512;
}
QDialog QLabel, QMessageBox QLabel, QInputDialog QLabel {
    color: #1c1512;
    background: transparent;
}
QGroupBox {
    background: transparent;
    border: 1px solid #dcd3c6;
    border-radius: 8px;
    margin-top: 12px;
    padding-top: 8px;
    color: #1c1512;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    left: 8px;
    color: #8a7d70;
}

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
    font-size: 18px;
    font-weight: 700;
    padding: 14px 10px;
    letter-spacing: 0.04em;
}
QPushButton#TransButton:hover  { background: #4d423a; }
QPushButton#TransButton:pressed { background: #2a2320; }

/* 응급 BLACK 토글 — 비활성: 흰 배경 + 검은 텍스트,
                   활성:   검은 배경 + 흰 텍스트. */
QPushButton#LivePowerButton {
    background: #ffffff;
    color: #1c1512;
    border: 1px solid #d3c8b8;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 800;
    padding: 14px 4px;
    letter-spacing: 0.05em;
}
QPushButton#LivePowerButton:hover  { background: #f5ede0; border-color: #b98a5e; }
QPushButton#LivePowerButton:pressed { background: #ece0cc; }
QPushButton#LivePowerButton:checked {
    background: #000000;
    color: #ffffff;
    border-color: #000000;
}
QPushButton#LivePowerButton:checked:hover { background: #1c1512; }

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
    border-radius: 6px;
    padding: 5px 9px;
    color: #1c1512;
    min-height: 22px;
    selection-background-color: #8a5a3b;
    selection-color: #ffffff;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 2px solid #b98a5e;
    padding: 4px 8px;   /* border 굵기 늘면 padding 1px 줄여 총 크기 유지 */
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #e4dbcb;
    height: 6px;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #8a5a3b;
    width: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #b98a5e;
    border-radius: 3px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; color: #1c1512; }
QMenuBar::item:selected { background: #ece0cc; }
QMenu {
    background: #fdfaf5;
    border: 1px solid #d3c8b8;
    padding: 4px;
    border-radius: 8px;
    color: #1c1512;
}
QMenu::item { padding: 6px 20px; border-radius: 6px; }
QMenu::item:selected { background: #ece0cc; }
QMenu::separator { height: 1px; background: #e4dbcb; margin: 4px 6px; }

QStatusBar { background: transparent; color: #8a7d70; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 6px;
    background: transparent;
    color: #1c1512;
}
QToolBar QToolButton:hover { background: #ece0cc; }

/* Slimmer 스크롤바 — 8px, hover 시 브랜드 액센트 색으로 강조. */
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical {
    background: #d3c8b8;
    border-radius: 4px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #8a5a3b; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #d3c8b8;
    border-radius: 4px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #8a5a3b; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #0e0d0c; color: #6a5f55; border-radius: 6px; }

QTabWidget#BottomTabs::pane {
    background: transparent;
    border: 0;
    padding-top: 4px;
}
QTabWidget#BottomTabs QTabBar::tab {
    background: transparent;
    color: #8a7d70;
    padding: 6px 14px;
    border: 0;
    border-bottom: 2px solid transparent;
    font-weight: 600;
}
QTabWidget#BottomTabs QTabBar::tab:hover { color: #1c1512; }
QTabWidget#BottomTabs QTabBar::tab:selected {
    color: #1c1512;
    border-bottom: 2px solid #8a5a3b;
}

QPushButton#ProgramCollapseBtn {
    background: transparent;
    border: 1px solid #d3c8b8;
    border-radius: 6px;
    color: #1c1512;
    font-weight: 700;
    padding: 0;
}
QPushButton#ProgramCollapseBtn:hover { background: #f5ede0; }
)";

const char* kQssDark = R"(
QMainWindow, QWidget#Central {
    background: #141210;
    color: #e8ddd0;
}
QWidget { font-size: 13px; color: #e8ddd0; }

/* 대화상자·메시지박스 — 테마 배경과 매칭. */
QDialog, QMessageBox, QInputDialog {
    background: #1e1a17;
    color: #e8ddd0;
}
QDialog QLabel, QMessageBox QLabel, QInputDialog QLabel {
    color: #e8ddd0;
    background: transparent;
}
QGroupBox {
    background: transparent;
    border: 1px solid #2c2620;
    border-radius: 8px;
    margin-top: 12px;
    padding-top: 8px;
    color: #e8ddd0;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    left: 8px;
    color: #8f857a;
}

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
    font-size: 18px;
    font-weight: 700;
    padding: 14px 10px;
    letter-spacing: 0.04em;
}
QPushButton#TransButton:hover  { background: #d6b48e; }
QPushButton#TransButton:pressed { background: #a88760; }

/* 응급 BLACK 토글 — 다크 팔레트: 비활성 시 크림 배경 + 딥 텍스트,
                                  활성 시 검은 배경 + 흰 텍스트. */
QPushButton#LivePowerButton {
    background: #e8ddd0;
    color: #14100c;
    border: 1px solid #3a322c;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 800;
    padding: 14px 4px;
    letter-spacing: 0.05em;
}
QPushButton#LivePowerButton:hover  { background: #d6c8b8; border-color: #c8a37a; }
QPushButton#LivePowerButton:pressed { background: #b8a790; }
QPushButton#LivePowerButton:checked {
    background: #000000;
    color: #ffffff;
    border-color: #000000;
}
QPushButton#LivePowerButton:checked:hover { background: #1c1512; }

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
    border-radius: 6px;
    padding: 5px 9px;
    color: #e8ddd0;
    min-height: 22px;
    selection-background-color: #c8a37a;
    selection-color: #14100c;
}
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 2px solid #c8a37a;
    padding: 4px 8px;
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #2c2620;
    height: 6px;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #c8a37a;
    width: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #c8a37a;
    border-radius: 3px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; color: #e8ddd0; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: #33291f; }
QMenu {
    background: #1e1a17;
    border: 1px solid #3a322c;
    padding: 4px;
    border-radius: 8px;
    color: #e8ddd0;
}
QMenu::item { padding: 6px 20px; border-radius: 6px; }
QMenu::item:selected { background: #33291f; }
QMenu::separator { height: 1px; background: #2c2620; margin: 4px 6px; }

QStatusBar { background: transparent; color: #8f857a; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 6px;
    background: transparent;
    color: #e8ddd0;
}
QToolBar QToolButton:hover { background: #33291f; }

/* Slimmer 스크롤바 — 8px, hover 시 브랜드 액센트 색으로 강조. */
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical {
    background: #3a322c;
    border-radius: 4px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #c8a37a; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #3a322c;
    border-radius: 4px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #c8a37a; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #050403; color: #4a4038; border-radius: 6px; }

QTabWidget#BottomTabs::pane {
    background: transparent;
    border: 0;
    padding-top: 4px;
}
QTabWidget#BottomTabs QTabBar::tab {
    background: transparent;
    color: #8f857a;
    padding: 6px 14px;
    border: 0;
    border-bottom: 2px solid transparent;
    font-weight: 600;
}
QTabWidget#BottomTabs QTabBar::tab:hover { color: #e8ddd0; }
QTabWidget#BottomTabs QTabBar::tab:selected {
    color: #e8ddd0;
    border-bottom: 2px solid #c8a37a;
}

QPushButton#ProgramCollapseBtn {
    background: transparent;
    border: 1px solid #3a322c;
    border-radius: 6px;
    color: #e8ddd0;
    font-weight: 700;
    padding: 0;
}
QPushButton#ProgramCollapseBtn:hover { background: #33291f; }
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

// 헤더 위젯을 직접 넣는 변형 — Live 송출 처럼 헤더에 라벨+버튼 구성을
// 넣어야 할 때. content 는 헤더 아래로 배치.
QWidget* wrapPanelWithHeader(QWidget* content, QWidget* headerWidget) {
    auto* frame = new QFrame;
    frame->setObjectName("Panel");
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);
    if (headerWidget) v->addWidget(headerWidget);
    v->addWidget(content, 1);
    return frame;
}

// ---- 외부 QSS 테마용 헬퍼 --------------------------------------------
//   외부 QSS(GTRONICK Aqua/ElegantDark)는 일반 QPushButton 룰만 있어
//   TAKE/BLACK/Fade 처럼 오브젝트이름으로 특수화한 안전 버튼들이 밋밋해진다.
//   외부 테마 뒤에 이 스니펫을 붙여 "빨간 TAKE / 검정 BLACK 토글 / 진한 Fade"
//   식별성을 항상 유지 — 오조작 방지 목적.
const char* kEssentialOverrides = R"(
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
QPushButton#TakeButton:hover  { background: #b23629; }
QPushButton#TakeButton:pressed { background: #7d1f16; }

QPushButton#TransButton {
    background: #3d342d;
    color: #f5ede0;
    border: 0;
    border-radius: 8px;
    font-size: 18px;
    font-weight: 700;
    padding: 14px 10px;
    letter-spacing: 0.04em;
}
QPushButton#TransButton:hover  { background: #4d423a; }
QPushButton#TransButton:pressed { background: #2a2320; }

QPushButton#LivePowerButton {
    background: #ffffff;
    color: #1c1512;
    border: 1px solid #d3c8b8;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 800;
    padding: 14px 4px;
    letter-spacing: 0.05em;
}
QPushButton#LivePowerButton:hover  { background: #f5ede0; border-color: #b98a5e; }
QPushButton#LivePowerButton:pressed { background: #ece0cc; }
QPushButton#LivePowerButton:checked {
    background: #000000;
    color: #ffffff;
    border-color: #000000;
}
)";

QString loadQssFromResource(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
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
    setupStatusBar();                            // UX-1: 도트/엔진/모니터/시계

    // 메뉴바는 기본 숨김 — 웨딩홀 운용 컨텍스트에서 상시 노출은 시각적 노이즈.
    // F10 로 토글 (Windows 관행). 도구 → 디스플레이 등 자주 쓰는 항목은
    // 상단 툴바에도 있으므로 메뉴 숨겨도 접근성 문제 없음.
    menuBar()->hide();
    auto* toggleMenu = new QAction(this);
    toggleMenu->setShortcut(QKeySequence(Qt::Key_F10));
    connect(toggleMenu, &QAction::triggered, this, [this]{
        menuBar()->setVisible(!menuBar()->isVisible());
    });
    addAction(toggleMenu);

    // UI-B: 저장된 테마 로드 후 적용.
    QSettings qs("Hanmac", "uWeddingPlayer");
    const QString saved = qs.value("ui/theme", "light").toString();
    applyTheme(saved);
}

ControlWindow::~ControlWindow() = default;

void ControlWindow::createMenus() {
    // UX-1: 메뉴/툴바 한글화 + 슬림화. 어르신 운용자를 위한 큰 라벨/명확한 어법.
    auto* fileMenu    = menuBar()->addMenu(tr("파일(&F)"));
    auto* settingsAct = fileMenu->addAction(tr("환경 설정(&S)..."));
    connect(settingsAct, &QAction::triggered,
            this, &ControlWindow::openSettingsRequested);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("종료(&X)"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* sceneMenu = menuBar()->addMenu(tr("장면(&S)"));
    auto* saveAct   = sceneMenu->addAction(tr("장면 저장(&S)"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &ControlWindow::saveSceneRequested);
    auto* loadAct = sceneMenu->addAction(tr("장면 불러오기(&L)"));
    connect(loadAct, &QAction::triggered, this, &ControlWindow::loadSceneRequested);
    sceneMenu->addSeparator();
    auto* clearAct = sceneMenu->addAction(tr("장면 비우기(&C)"));
    // 어르신 실수 방지: Y/N 대신 예/취소 + 기본 포커스 취소.
    connect(clearAct, &QAction::triggered, this, [this]{
        const int ret = QMessageBox::question(this,
            tr("장면 비우기"),
            tr("정말 모든 레이어를 지우시겠어요?\n이 동작은 되돌릴 수 없습니다."),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (ret == QMessageBox::Yes) m_scene->clear();
    });

    auto* toolsMenu  = menuBar()->addMenu(tr("도구(&T)"));
    // 디스플레이 설정 — 캔버스 해상도 + 송출 모니터 통합(웨딩홀 현장 세팅).
    auto* displayAct = toolsMenu->addAction(tr("디스플레이 설정(&D)..."));
    connect(displayAct, &QAction::triggered,
            this, &ControlWindow::displaySettingsRequested);
    auto* playTestAct = toolsMenu->addAction(tr("테스트 영상 재생(&T)..."));
    connect(playTestAct, &QAction::triggered,
            this, &ControlWindow::playTestVideoRequested);
    toolsMenu->addSeparator();

    // ----- 테마 서브메뉴 -----------------------------------------
    //   내장 2개(밝은/어두운) + 외부 QSS 2개(ElegantDark/Aqua, GTRONICK/QSS MIT).
    //   QActionGroup exclusive 로 라디오 동작. QSettings ui/theme 에 id 저장.
    auto* themeMenu  = toolsMenu->addMenu(tr("테마(&M)"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const struct { const char* id; const char* label; } kThemes[] = {
        {"light",       "밝은"},
        {"dark",        "어두운"},
        {"elegantdark", "ElegantDark"},
        {"aqua",        "Aqua"},
    };
    for (const auto& t : kThemes) {
        auto* a = themeMenu->addAction(tr(t.label));
        a->setCheckable(true);
        a->setData(QString::fromLatin1(t.id));
        themeGroup->addAction(a);
        m_themeActions.append(a);
        connect(a, &QAction::triggered, this, [this, a]{
            const QString id = a->data().toString();
            applyTheme(id);
            QSettings qs("Hanmac", "uWeddingPlayer");
            qs.setValue("ui/theme", id);
        });
    }

    // UX-1: 도움말 메뉴 — 지금은 정보 다이얼로그만. 실제 도움말은 UX-5.
    auto* helpMenu = menuBar()->addMenu(tr("도움말(&H)"));
    auto* aboutAct = helpMenu->addAction(tr("정보(&I)..."));
    connect(aboutAct, &QAction::triggered, this, [this]{
        QMessageBox::about(this, tr("uWeddingPlayer 정보"),
            tr("<h3>uWeddingPlayer</h3>"
               "<p>웨딩홀 미디어 플레이어</p>"
               "<p>버전: 개발 빌드</p>"
               "<p>© Hanmac IT</p>"));
    });

    // ---- 툴바: 자주 쓰는 3개만 (장면 비우기·테스트 영상 재생은 메뉴에만). ----
    auto* toolbar = addToolBar(tr("도구 모음"));
    toolbar->setObjectName("MainToolBar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->addAction(saveAct);
    toolbar->addAction(loadAct);
    toolbar->addSeparator();
    toolbar->addAction(displayAct);

    // 우측 stretch — [준비|진행] 세그먼트를 툴바 오른쪽 끝으로 밀어냄.
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // 모드 토글 (스텁 — UX-4 에서 활성화). 지금은 준비만 checked, 진행은 disabled.
    auto* modeContainer = new QWidget(toolbar);
    auto* modeRow = new QHBoxLayout(modeContainer);
    modeRow->setContentsMargins(0, 0, 8, 0);
    modeRow->setSpacing(0);
    m_btnModePrep = new QPushButton(tr("준비"), modeContainer);
    m_btnModePrep->setObjectName("ModePrepButton");
    m_btnModePrep->setCheckable(true);
    m_btnModePrep->setChecked(true);
    m_btnModeShow = new QPushButton(tr("진행"), modeContainer);
    m_btnModeShow->setObjectName("ModeShowButton");
    m_btnModeShow->setCheckable(true);
    m_btnModeShow->setEnabled(false);
    m_btnModeShow->setToolTip(tr("진행 모드는 곧 추가됩니다 (UX-4)"));
    modeRow->addWidget(m_btnModePrep);
    modeRow->addWidget(m_btnModeShow);
    toolbar->addWidget(modeContainer);
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
    // "파일 추가" · "폴더 추가" 두 버튼 텍스트가 잘리지 않는 최소 폭.
    m_mediaList->setMinimumWidth(180);
    // UI-D Phase B: 좌측 컬럼 [페이지] 탭 — 현재 편집 프로그램의 페이지 목록.
    m_pageList = new PageListWidget;
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
    // "+ 텍스트" — Preview 툴바 아이콘에서 발화 (UI-F 신호). 파일 없이 씬에
    // 텍스트 레이어 신규 생성. 시계·날씨 위젯도 추후 같은 시그널 채널로 확장.
    connect(this, &ControlWindow::addTextLayerRequested,
            this, [this]{ m_scene->addTextLayer(QString()); });

    // ----- Preview 캔버스 -----
    m_canvas = new PreviewCanvas(m_scene, m_snapshots);

    // ----- Live 미러 -----
    m_liveMirror = new QLabel;
    m_liveMirror->setObjectName("LiveMirror");
    m_liveMirror->setAlignment(Qt::AlignCenter);
    m_liveMirror->setMinimumSize(240, 135);   // 16:9 mini

    // ----- Programs (UI-C 카드형) -----
    m_programList = new ProgramListWidget;
    // 최소 높이는 ProgramListWidget 이 접힘/펼침 상태에 따라 자체 관리.
    // (외부에서 200 을 걸면 접혀도 그 공간 유지되어 빈 영역 발생)

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
    m_btnTransition->setFixedWidth(90);   // TAKE 폰트 크기와 균형 — 폭 여유
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

    // 재생/일시정지 아이콘 버튼 — Qt 표준 미디어 아이콘 (SP_MediaPlay/Pause).
    // Application 이 컨텍스트별 판단 (대기 → 재생, 재생중 → 일시정지, 일시정지 → 재개).
    m_btnPlayPause = new QPushButton;
    m_btnPlayPause->setObjectName("PlayPauseButton");
    m_btnPlayPause->setMinimumHeight(52);
    m_btnPlayPause->setFixedWidth(64);
    m_btnPlayPause->setIcon(QIcon(QStringLiteral(":/icons/play.svg")));
    m_btnPlayPause->setIconSize(QSize(20, 20));
    m_btnPlayPause->setToolTip(tr("재생 / 일시정지 (자동 진행 제어)"));
    connect(m_btnPlayPause, &QPushButton::clicked, this,
            [this]{ emit playPauseRequested(); });

    // ----- 컬럼 조립 -----

    // LEFT: 미디어 라이브러리 전용 컬럼 (편집 중 상시 드래그 소스).
    //   페이지는 하단 탭으로 이동해 미디어와 동시 노출 가능.
    auto* leftPanel = wrapPanel(m_mediaList);

    // CENTER TOP: Preview (툴바 + 캔버스)
    auto* previewPanel = wrapPanel(buildPreviewPane(), tr("작업 캔버스"));

    // CENTER BOTTOM: 프로그램 리스트(접기 가능) 위에 페이지 리스트 세로 스택.
    //   프로그램 선택 후 자동으로 접혀 페이지 편집에 세로 공간 최대 확보.
    //   접힌 상태 헤더에 "프로그램: 이름" 표시 → 컨텍스트 상실 방지.
    auto* bottomStack = new QWidget;
    auto* bstL = new QVBoxLayout(bottomStack);
    bstL->setContentsMargins(0, 0, 0, 0);
    bstL->setSpacing(6);
    bstL->addWidget(m_programList);
    bstL->addWidget(m_pageList, 1);
    auto* programPanel = wrapPanel(bottomStack);

    auto* centerCol = new QSplitter(Qt::Vertical);
    centerCol->setObjectName("SplitCenter");   // QSettings 키
    centerCol->addWidget(previewPanel);
    centerCol->addWidget(programPanel);
    centerCol->setStretchFactor(0, 3);
    centerCol->setStretchFactor(1, 1);
    // 초기: 프로그램 펼침 상태(m_collapsed=false, 234) + 페이지(~200) 합계 여유.
    // collapseChanged 핸들러가 이후 접힘/펼침에 맞춰 재배분.
    centerCol->setSizes({ 500, 460 });

    // 프로그램 리스트 접힘/펼침에 따라 하단 영역 크기 자동 조정 —
    // 펼침 시 카드 표시로 세로가 늘어나므로 Preview 를 살짝 줄여 페이지가
    // 잘리지 않게. 접히면 원위치로.
    connect(m_programList, &ProgramListWidget::collapseChanged,
            this, [centerCol](bool collapsed) {
                const auto sizes = centerCol->sizes();
                const int total = sizes.value(0) + sizes.value(1);
                //   접힘: 하단 340 (프로그램 96 + 페이지 ~200 + 여백)
                //   펼침: 하단 480 (프로그램 234 + 페이지 ~200 + 여백)
                const int bottomTarget = collapsed ? 340 : 480;
                const int topTarget    = qMax(200, total - bottomTarget);
                centerCol->setSizes({ topTarget, bottomTarget });
            });

    // RIGHT TOP: Live 헤더 — 상태 라벨 + 화면 마스크 ON/OFF 토글.
    //   상태 라벨: 재생중(녹) / 일시정지(주황) / 대기(회) 3-state.
    //   ON/OFF: 프로젝터 화면을 검정으로 마스크 (구 BLACK 응급 기능).
    auto* liveHeader = new QWidget;
    {
        auto* hh = new QHBoxLayout(liveHeader);
        hh->setContentsMargins(0, 0, 0, 0);
        hh->setSpacing(8);
        m_liveStatus = new QLabel;
        m_liveStatus->setObjectName("LiveStatusLabel");
        m_btnLivePower = new QPushButton(tr("ON"));
        m_btnLivePower->setObjectName("LivePowerButton");
        m_btnLivePower->setCheckable(true);
        m_btnLivePower->setMinimumWidth(64);   // ON/OFF 라벨 여유 폭
        m_btnLivePower->setToolTip(
            tr("프로젝터 화면 표시 ON/OFF — OFF 는 응급 검정 마스크"));
        connect(m_btnLivePower, &QPushButton::clicked,
                this, &ControlWindow::blackoutRequested);
        hh->addWidget(m_liveStatus, 1);
        hh->addWidget(m_btnLivePower);
    }
    auto* livePanel = wrapPanelWithHeader(buildLivePanel(), liveHeader);
    setLiveState(LiveState::Idle);    // 초기 라벨/아이콘 통일
    setLivePowerOff(false);           // 초기 ON

    // RIGHT BOTTOM: Property (자체 헤더 있음). 세로 공간이 부족할 때 하단
    // 버튼(레이어 삭제 등)이 잘리지 않도록 QScrollArea 로 감싸 스크롤 확보.
    auto* propScroll = new QScrollArea;
    propScroll->setWidget(m_property);
    propScroll->setWidgetResizable(true);
    propScroll->setFrameShape(QFrame::NoFrame);
    propScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 배경을 부모 패널(QFrame#Panel) 색과 동기화 — 기본 QScrollArea·viewport
    // 배경이 시스템 팔레트에 따라 어긋나 보이는 문제 회피.
    propScroll->setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget "
        "{ background: transparent; }"));
    m_property->setAutoFillBackground(false);
    auto* propPanel = wrapPanel(propScroll);

    auto* rightCol = new QSplitter(Qt::Vertical);
    rightCol->setObjectName("SplitRight");
    rightCol->addWidget(livePanel);
    rightCol->addWidget(propPanel);
    rightCol->setStretchFactor(0, 0);
    rightCol->setStretchFactor(1, 1);
    rightCol->setSizes({ 340, 500 });

    // OUTER: 3-column horizontal splitter
    auto* outer = new QSplitter(Qt::Horizontal);
    outer->setObjectName("SplitOuter");
    outer->addWidget(leftPanel);
    outer->addWidget(centerCol);
    outer->addWidget(rightCol);
    outer->setStretchFactor(0, 0);
    outer->setStretchFactor(1, 1);
    outer->setStretchFactor(2, 0);
    outer->setSizes({ 200, 1000, 340 });   // 미디어 라이브러리 컴팩트 + 버튼 텍스트 여유

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
    m_btnPreviewPlay->setEnabled(false);   // 프로그램·displayTimeSec 있어야 활성
    m_btnPreviewPlay->setToolTip(
        tr("미리보기 재생 — 표시 시간이 흐를 시간을 시각화 (실제 송출 아님)"));
    connect(m_btnPreviewPlay, &QPushButton::clicked,
            this, &ControlWindow::togglePreviewPlay);

    // 100ms 틱 — 초 단위 표시라 1s 로도 충분하지만 pause 반응성/정확도 확보용.
    m_previewTimer = new QTimer(this);
    m_previewTimer->setInterval(100);
    connect(m_previewTimer, &QTimer::timeout,
            this, &ControlWindow::tickPreviewPlay);

    m_timeLabel = new QLabel(tr("표시 시간: —"));
    m_timeLabel->setObjectName("PreviewTimeLabel");

    // TAKE 는 실제 송출 — 시뮬레이션 리셋.
    connect(this, &ControlWindow::takeRequested,
            this, &ControlWindow::resetPreviewSim);

    // 위젯 추가 클러스터 — 파일 없이 캔버스에 직접 얹는 요소들.
    //   [T] 텍스트 (Lucide "type" — 대문자 T 세리프 형태)
    //   추후 시계·날씨·QR 등 동종 위젯을 같은 클러스터에 나란히 배치.
    //   미디어 라이브러리(파일 소스)와 성격이 다르므로 편집 캔버스 툴바가
    //   더 자연스러운 자리 — 드래그·드롭 대신 원-클릭 삽입 UX.
    auto* btnAddText = new QPushButton;
    btnAddText->setObjectName("PreviewToolBtn");
    btnAddText->setIcon(QIcon(QStringLiteral(":/icons/type.svg")));
    btnAddText->setToolTip(tr("텍스트 레이어 추가 — 자막·안내문 등"));
    connect(btnAddText, &QPushButton::clicked,
            this, &ControlWindow::addTextLayerRequested);

    // 우측: 캔버스에 꽉 채우기 (선택 레이어 → geometry = 캔버스 전체).
    //   Lucide maximize-2 (양방향 확장 화살표) — "꽉 채우기" 의미에 가장 근접.
    m_btnFillCanvas = new QPushButton;
    m_btnFillCanvas->setObjectName("PreviewToolBtn");
    m_btnFillCanvas->setIcon(QIcon(QStringLiteral(":/icons/maximize.svg")));
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
    m_btnDeleteLayer->setIcon(QIcon(QStringLiteral(":/icons/trash.svg")));
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
    toolbar->addWidget(btnAddText);         // 위젯 클러스터: 텍스트(추후 시계·날씨)
    toolbar->addSpacing(12);                // 위젯 ↔ 편집 액션 시각 구분
    toolbar->addWidget(m_btnFillCanvas);    // 우측: 꽉 채우기
    toolbar->addWidget(m_btnDeleteLayer);   // 우측 끝: 삭제

    v->addLayout(toolbar);
    v->addWidget(m_canvas, 1);
    return box;
}

void ControlWindow::setPreviewDisplayTime(int seconds) {
    m_previewTotalSec = seconds;
    // 프로그램 컨텍스트가 바뀌면 시뮬레이션은 항상 초기 상태.
    resetPreviewSim();
    if (m_btnPreviewPlay) m_btnPreviewPlay->setEnabled(seconds > 0);
}

void ControlWindow::togglePreviewPlay() {
    if (m_previewTotalSec <= 0) return;
    if (m_previewRunning) {
        m_previewTimer->stop();
        m_previewRunning = false;
        m_btnPreviewPlay->setText(QStringLiteral("▶"));
        emit previewPlayingChanged(false);
    } else {
        // 이미 총 시간에 도달했다면 처음부터 재생
        if (m_previewElapsedMs >= m_previewTotalSec * 1000)
            m_previewElapsedMs = 0;
        m_previewTimer->start();
        m_previewRunning = true;
        m_btnPreviewPlay->setText(QStringLiteral("⏸"));
        emit previewPlayingChanged(true);
    }
    updateTimeLabel();
}

void ControlWindow::tickPreviewPlay() {
    m_previewElapsedMs += m_previewTimer->interval();
    const int totalMs = m_previewTotalSec * 1000;
    if (m_previewElapsedMs >= totalMs) {
        m_previewElapsedMs = totalMs;
        m_previewTimer->stop();
        m_previewRunning = false;
        m_btnPreviewPlay->setText(QStringLiteral("▶"));
        updateTimeLabel();
        // 자연 만료 → Application 이 프로그램의 endAction 조회하여 후속 결정.
        //   Loop/Next/First: restartPreviewCountdown 호출로 체인 계속.
        //   Stop:            resetPreviewSim → 창 닫힘.
        //   Hold:            아무것도 안 함 → 창 유지, 카운터 "00:30 / 00:30".
        // 이전 구현은 previewPlayingChanged(false) 를 발화해 무조건 창을 닫았음.
        emit previewCompleted();
        return;
    }
    updateTimeLabel();
}

void ControlWindow::restartPreviewCountdown(int newTotalSec) {
    m_previewTotalSec  = newTotalSec;
    m_previewElapsedMs = 0;
    if (!m_previewTimer) return;
    if (newTotalSec > 0) {
        m_previewTimer->start();
        m_previewRunning = true;
        if (m_btnPreviewPlay) m_btnPreviewPlay->setText(QStringLiteral("⏸"));
    } else {
        // 새 프로그램이 수동 진행이면 타이머 정지, 창은 유지.
        m_previewTimer->stop();
        m_previewRunning = false;
        if (m_btnPreviewPlay) m_btnPreviewPlay->setText(QStringLiteral("▶"));
    }
    updateTimeLabel();
    // 창 열림/닫힘 상태 신호는 발화하지 않음 — 세션 자체는 그대로 진행.
}

void ControlWindow::resetPreviewSim() {
    const bool wasRunning = m_previewRunning;
    if (m_previewTimer) m_previewTimer->stop();
    m_previewRunning   = false;
    m_previewElapsedMs = 0;
    if (m_btnPreviewPlay) m_btnPreviewPlay->setText(QStringLiteral("▶"));
    updateTimeLabel();
    if (wasRunning) emit previewPlayingChanged(false);
}

void ControlWindow::updateTimeLabel() {
    if (!m_timeLabel) return;
    if (m_previewTotalSec < 0) {
        m_timeLabel->setText(tr("표시 시간: —"));
        return;
    }
    if (m_previewTotalSec == 0) {
        m_timeLabel->setText(tr("표시 시간: 수동"));
        return;
    }
    const auto fmt = [](int sec) {
        return QString("%1:%2").arg(sec / 60, 2, 10, QChar('0'))
                                .arg(sec % 60, 2, 10, QChar('0'));
    };
    const QString total = fmt(m_previewTotalSec);
    if (m_previewRunning || m_previewElapsedMs > 0) {
        const int elapsedSec = m_previewElapsedMs / 1000;
        m_timeLabel->setText(tr("표시 시간: %1 / %2").arg(fmt(elapsedSec), total));
    } else {
        m_timeLabel->setText(tr("표시 시간: %1").arg(total));
    }
}

// UI-C: 우측 상단 Live 패널 조립. mirror 위, TAKE + 전환 토글 아래(한 줄).
QWidget* ControlWindow::buildLivePanel() {
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    v->addWidget(m_liveMirror, 1);

    // [       TAKE       ] [Fade/Cut] [▶/⏸] — TAKE 는 늘어남, 나머지 컴팩트.
    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_takeButton, 1);
    row->addWidget(m_btnTransition);
    row->addWidget(m_btnPlayPause);
    v->addLayout(row);

    return box;
}

// ---- 테마 적용 ------------------------------------------------------
//   지원 id: light / dark (내장 raw string), elegantdark / aqua (외부 QSS 리소스).
//   외부 QSS 뒤에는 kEssentialOverrides 를 append — TAKE/BLACK/Fade 오조작
//   방지용 최소 규칙을 항상 유지.
void ControlWindow::applyTheme(const QString& theme) {
    QString style;
    QString id = theme;
    if (id == "dark") {
        style = QString::fromUtf8(kQssDark);
    } else if (id == "elegantdark") {
        style = loadQssFromResource(":/themes/ElegantDark.qss")
              + QString::fromUtf8(kEssentialOverrides);
    } else if (id == "aqua") {
        style = loadQssFromResource(":/themes/Aqua.qss")
              + QString::fromUtf8(kEssentialOverrides);
    } else {
        id    = "light";       // 알 수 없는 값은 기본으로 폴백
        style = QString::fromUtf8(kQssLight);
    }
    m_currentTheme = id;
    qApp->setStyleSheet(style);
    // 서브메뉴 라디오 상태 반영.
    for (QAction* a : m_themeActions) {
        if (a) a->setChecked(a->data().toString() == m_currentTheme);
    }
    // 타이틀바 색 — HWND 가 있어야 적용됨. 없으면 showEvent 에서 재적용.
    applyTitlebarTheme();
}

// Windows 11 타이틀바 다크모드. HWND 필요 → 창이 show 된 이후여야 확실히 반영.
void ControlWindow::applyTitlebarTheme() {
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;
    const BOOL useDark = (m_currentTheme == QLatin1String("dark")
                       || m_currentTheme == QLatin1String("elegantdark"))
                        ? TRUE : FALSE;
    // Windows 10 20H1+ = 20, 초기 인사이더 빌드 = 19. 둘 다 시도.
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                          &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
#endif
}

void ControlWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    // 생성자 시점의 applyTheme 호출 때는 HWND 미확보 → 여기서 재적용.
    applyTitlebarTheme();
    // 창이 실제 크기를 가진 후 레이아웃 복원 — 그렇지 않으면 restoreState 가
    // 잘못된 스케일로 계산될 수 있음. 최초 한 번만.
    if (!m_layoutRestored) {
        m_layoutRestored = true;
        restoreLayoutState();
    }
}

void ControlWindow::closeEvent(QCloseEvent* event) {
    saveLayoutState();
    QMainWindow::closeEvent(event);
}

// 레이아웃 상태 저장 — findChildren 로 명명된 QSplitter 전량 스캔.
// 사용자가 리사이즈한 각 스플리터의 handle 위치 + ProgramList 접힘 상태를
// QSettings 에 저장.
void ControlWindow::saveLayoutState() {
    QSettings qs("Hanmac", "uWeddingPlayer");
    for (QSplitter* s : findChildren<QSplitter*>()) {
        const QString name = s->objectName();
        if (name.isEmpty()) continue;
        qs.setValue(QStringLiteral("layout/") + name, s->saveState());
    }
    if (m_programList)
        qs.setValue(QStringLiteral("layout/programsCollapsed"),
                    m_programList->isCollapsed());
}

// 복원 — 저장값 있으면 QSplitter::restoreState. 없으면 setSizes 기본값 유지.
//   접힘 상태를 먼저 반영(centerCol 사이즈 재배분) → 그 위에 QSplitter
//   restoreState 로 최종 위치 확정 (사용자가 리사이즈한 배치가 우선).
void ControlWindow::restoreLayoutState() {
    QSettings qs("Hanmac", "uWeddingPlayer");
    if (m_programList) {
        const QVariant v = qs.value(QStringLiteral("layout/programsCollapsed"));
        if (v.isValid()) m_programList->setCollapsed(v.toBool());
    }
    for (QSplitter* s : findChildren<QSplitter*>()) {
        const QString name = s->objectName();
        if (name.isEmpty()) continue;
        const QByteArray state =
            qs.value(QStringLiteral("layout/") + name).toByteArray();
        if (!state.isEmpty()) s->restoreState(state);
    }
}

void ControlWindow::setStatusText(const QString& text) {
    // UX-1: 이제 임시(5초) 메시지 — 상시 상태는 우측 영구 위젯이 담당.
    statusBar()->showMessage(text, 5000);
}

// ---------------------------------------------------------------------------
// UX-1: 상태표시줄 초기화.
//   좌측(addWidget)      → 🟢 엔진 도트 + "OBS 송출" 또는 "Qt 송출"
//                        → 🟢 모니터 도트 + "송출 모니터 N번"
//   우측(addPermanentWidget) → HH:MM:SS 시계 (QTimer 1Hz).
//   setStatusText() 로 들어오는 임시 메시지는 좌측 stretch 영역에 5초간 표시됨.
// ---------------------------------------------------------------------------
void ControlWindow::setupStatusBar() {
    auto mkDot = [](const QString& color) {
        auto* dot = new QLabel;
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QString(
            "background:%1; border-radius:5px;").arg(color));
        return dot;
    };

    // 엔진 배지 — settings.engine 이 "obs" 면 OBS, 아니면 Qt.
    const QString engine = m_settings ? m_settings->engine() : QStringLiteral("qt");
    const bool obsMode   = (engine == QStringLiteral("obs"));
    m_statusObsDot  = mkDot(obsMode ? "#2ea043" : "#888");
    m_statusObsText = new QLabel(obsMode ? tr("OBS 송출") : tr("Qt 송출"));
    m_statusObsText->setStyleSheet("color:#444;");

    // 출력 모니터 인덱스 — 백엔드에 따라 다른 필드에서 조회.
    const int monIdx = m_settings
        ? (obsMode ? m_settings->obs().projectorMonitor
                   : m_settings->outputMonitorIndex())
        : 1;
    m_statusMonitorDot  = mkDot("#2ea043");
    m_statusMonitorText = new QLabel(tr("송출 모니터 %1번").arg(monIdx));
    m_statusMonitorText->setStyleSheet("color:#444;");

    m_statusClock = new QLabel;
    m_statusClock->setStyleSheet("color:#444; font-weight:500;");
    updateClock();
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &ControlWindow::updateClock);
    m_clockTimer->start(1000);

    QStatusBar* sb = statusBar();
    sb->setStyleSheet("QStatusBar { font-size: 12px; }");
    sb->addWidget(m_statusObsDot);
    sb->addWidget(m_statusObsText);
    sb->addWidget(new QLabel(QStringLiteral("　")));   // 배지 간 공백
    sb->addWidget(m_statusMonitorDot);
    sb->addWidget(m_statusMonitorText);
    sb->addPermanentWidget(m_statusClock);
}

void ControlWindow::updateClock() {
    if (m_statusClock)
        m_statusClock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

// Programs 리스트 접기/펼치기 및 접힌 헤더 이름 갱신 편의 슬롯 (Application 용).
void ControlWindow::showPagesTab() {
    // 3안 재재배치: 탭이 아니라 세로 스택. "페이지 탭으로 스위치" 의미는 이제
    // "프로그램 리스트를 접어 페이지에 공간을 준다"로 재해석.
    if (m_programList) m_programList->setCollapsed(true);
}
void ControlWindow::showProgramsTab() {
    if (m_programList) m_programList->setCollapsed(false);
}

// Live 헤더 상태 라벨 (4-state): 재생중(녹) · 일시정지(주황) · 대기(회) · Screen OFF(검).
// 클러스터의 재생/일시정지 아이콘도 동기 갱신 (ScreenOff 는 pause 상태로 취급).
void ControlWindow::setLiveState(LiveState s) {
    if (m_liveStatus) {
        switch (s) {
        case LiveState::Playing:
            m_liveStatus->setText(tr("● Live 재생중"));
            m_liveStatus->setStyleSheet(QStringLiteral(
                "color: #2ea043; font-weight: 700; letter-spacing: 0.06em;"));
            break;
        case LiveState::Paused:
            m_liveStatus->setText(tr("● Live 일시정지"));
            m_liveStatus->setStyleSheet(QStringLiteral(
                "color: #e2a132; font-weight: 700; letter-spacing: 0.06em;"));
            break;
        case LiveState::ScreenOff:
            m_liveStatus->setText(tr("● Screen OFF"));
            m_liveStatus->setStyleSheet(QStringLiteral(
                "color: #4a4038; font-weight: 700; letter-spacing: 0.06em;"));
            break;
        case LiveState::Idle:
        default:
            m_liveStatus->setText(tr("● Live 대기"));
            m_liveStatus->setStyleSheet(QStringLiteral(
                "color: #8a7d70; font-weight: 700; letter-spacing: 0.06em;"));
            break;
        }
    }
    setPlayPauseState(s);
}

// Live 헤더 ON/OFF 버튼 — off=true → 프로젝터 검정 마스크 상태.
void ControlWindow::setLivePowerOff(bool off) {
    if (!m_btnLivePower) return;
    QSignalBlocker sb(m_btnLivePower);
    m_btnLivePower->setChecked(off);
    m_btnLivePower->setText(off ? tr("OFF") : tr("ON"));
}

// 이전 API 호환 — 이제 헤더 ON/OFF 버튼이 blackout 을 시각화한다.
void ControlWindow::setBlackoutActive(bool active) {
    setLivePowerOff(active);
}

// 클러스터의 재생/일시정지 아이콘 반영. 재생중=일시정지(누르면 pause), 그 외=재생.
void ControlWindow::setPlayPauseState(LiveState s) {
    if (!m_btnPlayPause) return;
    m_btnPlayPause->setIcon(QIcon(s == LiveState::Playing
        ? QStringLiteral(":/icons/pause.svg")
        : QStringLiteral(":/icons/play.svg")));
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
