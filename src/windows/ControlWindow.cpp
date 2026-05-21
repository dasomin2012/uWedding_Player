#include "ControlWindow.h"

#include "app/Settings.h"
#include "scene/SceneModel.h"
#include "editor/PreviewCanvas.h"
#include "editor/PropertyPanel.h"
#include "editor/MediaListWidget.h"
#include "program/ProgramListWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>

namespace uwp {

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

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(saveAct);
    toolbar->addAction(loadAct);
    toolbar->addAction(clearAct);
    toolbar->addSeparator();
    toolbar->addAction(monitorAct);
    toolbar->addAction(playTestAct);
}

void ControlWindow::createCentralLayout() {
    // ----- 좌측: MediaList -----
    m_mediaList = new MediaListWidget(m_settings, m_snapshots);
    m_mediaList->setMinimumWidth(200);

    // 더블클릭 → 캔버스 중앙에 레이어 추가
    connect(m_mediaList, &MediaListWidget::mediaActivated,
            this, [this](const QString& path) {
                const QSize cs = m_scene->canvasSize();
                const qreal w = cs.width()  * 0.4;
                const qreal h = cs.height() * 0.4;
                m_scene->addLayer(path,
                    QRectF((cs.width() - w) / 2.0,
                           (cs.height() - h) / 2.0, w, h));
            });

    // ----- 중앙 상단 좌: PreviewCanvas -----
    m_canvas = new PreviewCanvas(m_scene, m_snapshots);

    // ----- 중앙 상단 가운데: Take + Cut/Fade 선택 -----
    m_takeButton = new QPushButton("TAKE");
    m_takeButton->setMinimumSize(120, 120);
    m_takeButton->setStyleSheet(
        "QPushButton { font-size: 22px; font-weight: bold; "
        "background-color: #b22; color: white; border-radius: 6px; }"
        "QPushButton:pressed { background-color: #e33; }");
    connect(m_takeButton, &QPushButton::clicked,
            this, &ControlWindow::takeRequested);

    m_takeMode = new QComboBox;
    m_takeMode->addItems({ "cut", "fade" });
    {
        const QString dm = m_settings ? m_settings->takeDefaultMode().toLower()
                                      : QString("fade");
        m_takeMode->setCurrentText(dm == "cut" ? "cut" : "fade");
    }
    connect(m_takeMode, &QComboBox::currentTextChanged,
            this, &ControlWindow::takeModeChanged);

    auto* takeContainer = new QWidget;
    auto* takeLayout    = new QVBoxLayout(takeContainer);
    takeLayout->setContentsMargins(6, 0, 6, 0);
    takeLayout->addStretch();
    takeLayout->addWidget(m_takeButton);
    takeLayout->addWidget(m_takeMode);
    takeLayout->addStretch();

    // ----- 중앙 상단 우: Live Mirror (placeholder) -----
    m_liveMirror = new QLabel("Live Monitor");
    m_liveMirror->setAlignment(Qt::AlignCenter);
    m_liveMirror->setStyleSheet(
        "background-color: #111; color: #888; border: 1px solid #444;");
    m_liveMirror->setMinimumSize(280, 160);

    auto* topRow = new QSplitter(Qt::Horizontal);
    topRow->addWidget(m_canvas);
    topRow->addWidget(takeContainer);
    topRow->addWidget(m_liveMirror);
    topRow->setStretchFactor(0, 5);
    topRow->setStretchFactor(1, 0);
    topRow->setStretchFactor(2, 2);
    topRow->setCollapsible(1, false);

    // ----- 하단: Program List (Phase 5 — 동적 리스트) -----
    m_programList = new ProgramListWidget;
    m_programList->setMinimumHeight(140);

    auto* centerCol = new QSplitter(Qt::Vertical);
    centerCol->addWidget(topRow);
    centerCol->addWidget(m_programList);
    centerCol->setStretchFactor(0, 4);
    centerCol->setStretchFactor(1, 1);

    // ----- 우측: PropertyPanel -----
    m_property = new PropertyPanel(m_scene);

    // ----- 전체 가로 분할 -----
    auto* outer = new QSplitter(Qt::Horizontal);
    outer->addWidget(m_mediaList);
    outer->addWidget(centerCol);
    outer->addWidget(m_property);
    outer->setStretchFactor(0, 1);
    outer->setStretchFactor(1, 5);
    outer->setStretchFactor(2, 1);

    auto* central = new QWidget(this);
    auto* root    = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->addWidget(outer);
    setCentralWidget(central);
}

void ControlWindow::setStatusText(const QString& text) {
    statusBar()->showMessage(text);
}

} // namespace uwp
