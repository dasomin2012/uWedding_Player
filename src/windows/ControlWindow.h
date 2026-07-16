#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QWidget;
class QPushButton;
class QComboBox;
class QSlider;
class QAction;
class QImage;

namespace uwp {

class Settings;
class SceneModel;
class SnapshotCache;
class PreviewCanvas;
class PropertyPanel;
class MediaListWidget;
class ProgramListWidget;

// Phase 3 운용자 화면.
//   +-----------+----------------------+----------+
//   | MediaList | PreviewCanvas | Take | Property |
//   |  (편집)   |   (자유레이어) | Mir. |  Panel   |
//   |           +----------------------+          |
//   |           | Program List (8)     |          |
//   +-----------+----------------------+----------+
class ControlWindow : public QMainWindow {
    Q_OBJECT
public:
    ControlWindow(Settings* settings, SceneModel* scene,
                  SnapshotCache* snapshots, QWidget* parent = nullptr);
    ~ControlWindow() override;

public slots:
    void setStatusText(const QString& text);

    // LiveMirror: Application 이 폴링한 프레임을 우상단 "Live 송출" 패널로.
    // 빈 QImage 는 무시(직전 프레임 유지).
    void setLiveMirrorImage(const QImage& img);

public:
    ProgramListWidget* programList()  const { return m_programList; }  // Application 와이어링용
    PreviewCanvas*     previewCanvas() const { return m_canvas; }      // 썸네일 렌더용

signals:
    void selectOutputMonitorRequested();
    void openSettingsRequested();
    void playTestVideoRequested();
    void saveSceneRequested();
    void loadSceneRequested();
    void takeRequested();
    void takeModeChanged(const QString& mode);   // "cut" | "fade"

private:
    void createMenus();
    void createCentralLayout();
    QWidget* buildGlobalBar();          // 하단 F2B / TAKE / 전환 / BGM
    void applyTheme(const QString& theme);   // "light" | "dark" — QSS 적용
    void toggleTheme();                      // 다크 모드 토글 (Tools 메뉴)

    Settings*        m_settings   = nullptr;
    SceneModel*      m_scene      = nullptr;
    SnapshotCache*   m_snapshots  = nullptr;

    MediaListWidget* m_mediaList  = nullptr;
    PreviewCanvas*   m_canvas     = nullptr;
    PropertyPanel*   m_property   = nullptr;
    QLabel*            m_liveMirror = nullptr;
    QPushButton*       m_takeButton = nullptr;
    QComboBox*         m_takeMode   = nullptr;
    ProgramListWidget* m_programList = nullptr;

    // UI-A: 하단 GlobalBar (F2B / BGM 슬라이더 placeholder)
    QWidget*     m_globalBar   = nullptr;
    QPushButton* m_btnFbtb     = nullptr;
    QSlider*     m_bgmSlider   = nullptr;

    // UI-B: 다크 모드
    QAction*     m_darkThemeAct = nullptr;
    QString      m_currentTheme = "light";
};

} // namespace uwp
