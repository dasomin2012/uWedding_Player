#include "ControlWindow.h"

#include "app/Settings.h"

#include <QListWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QToolBar>
#include <QApplication>
#include <QFrame>

namespace uwp {

ControlWindow::ControlWindow(Settings* settings, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
{
    setWindowTitle("uWeddingPlayer — Control");
    resize(1440, 900);

    createMenus();
    createCentralLayout();

    statusBar()->showMessage("Ready");
}

ControlWindow::~ControlWindow() = default;

void ControlWindow::createMenus() {
    auto* fileMenu     = menuBar()->addMenu(tr("&File"));
    auto* settingsAct  = fileMenu->addAction(tr("&Settings..."));
    connect(settingsAct, &QAction::triggered,
            this, &ControlWindow::openSettingsRequested);

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("E&xit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* toolsMenu  = menuBar()->addMenu(tr("&Tools"));
    auto* monitorAct = toolsMenu->addAction(tr("Select Output &Monitor..."));
    connect(monitorAct, &QAction::triggered,
            this, &ControlWindow::selectOutputMonitorRequested);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(monitorAct);
}

void ControlWindow::createCentralLayout() {
    auto* central = new QWidget(this);

    // ----- 좌측: Media List -----
    m_mediaList = new QListWidget;
    m_mediaList->addItem("(no media — Phase 1)");
    m_mediaList->setMinimumWidth(180);

    // ----- 중앙 상단 좌: Preview Canvas -----
    m_previewCanvas = new QGraphicsView;
    m_previewCanvas->setScene(new QGraphicsScene(m_previewCanvas));
    m_previewCanvas->setBackgroundBrush(Qt::black);
    m_previewCanvas->setFrameShape(QFrame::Box);
    m_previewCanvas->setMinimumSize(320, 180);

    // ----- 중앙 상단 우: Live Mirror -----
    m_liveMirror = new QLabel("Live Mirror\n(placeholder)");
    m_liveMirror->setAlignment(Qt::AlignCenter);
    m_liveMirror->setStyleSheet(
        "background-color: #111; color: #888; border: 1px solid #444;");
    m_liveMirror->setMinimumSize(320, 180);

    auto* topRow = new QSplitter(Qt::Horizontal);
    topRow->addWidget(m_previewCanvas);
    topRow->addWidget(m_liveMirror);
    topRow->setStretchFactor(0, 2);
    topRow->setStretchFactor(1, 1);

    // ----- 하단: Program List (8칸 그리드) -----
    auto* programWidget = new QWidget;
    programWidget->setMinimumHeight(140);
    auto* programLayout = new QGridLayout(programWidget);
    programLayout->setContentsMargins(0, 4, 0, 0);
    for (int i = 0; i < 8; ++i) {
        m_programButtons[i] = new QPushButton(QString("Program %1").arg(i + 1));
        m_programButtons[i]->setMinimumHeight(60);
        m_programButtons[i]->setEnabled(false);  // Phase 1: 비활성 placeholder
        programLayout->addWidget(m_programButtons[i], i / 4, i % 4);
    }

    auto* centerCol = new QSplitter(Qt::Vertical);
    centerCol->addWidget(topRow);
    centerCol->addWidget(programWidget);
    centerCol->setStretchFactor(0, 3);
    centerCol->setStretchFactor(1, 1);

    // ----- 우측: Property Panel -----
    m_propertyPanel = new QWidget;
    m_propertyPanel->setMinimumWidth(220);
    auto* propLayout = new QVBoxLayout(m_propertyPanel);
    auto* propTitle  = new QLabel("Property Panel");
    propTitle->setStyleSheet("font-weight: bold;");
    propLayout->addWidget(propTitle);
    propLayout->addStretch();

    // ----- 전체 가로 분할 -----
    auto* outer = new QSplitter(Qt::Horizontal);
    outer->addWidget(m_mediaList);
    outer->addWidget(centerCol);
    outer->addWidget(m_propertyPanel);
    outer->setStretchFactor(0, 1);
    outer->setStretchFactor(1, 5);
    outer->setStretchFactor(2, 1);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->addWidget(outer);

    setCentralWidget(central);
}

} // namespace uwp
