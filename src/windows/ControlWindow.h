#pragma once

#include <QMainWindow>

class QLabel;
class QWidget;
class QPushButton;
class QComboBox;

namespace uwp {

class Settings;
class SceneModel;
class SnapshotCache;
class PreviewCanvas;
class PropertyPanel;
class MediaListWidget;

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

    Settings*        m_settings   = nullptr;
    SceneModel*      m_scene      = nullptr;
    SnapshotCache*   m_snapshots  = nullptr;

    MediaListWidget* m_mediaList  = nullptr;
    PreviewCanvas*   m_canvas     = nullptr;
    PropertyPanel*   m_property   = nullptr;
    QLabel*          m_liveMirror = nullptr;
    QPushButton*     m_takeButton = nullptr;
    QComboBox*       m_takeMode   = nullptr;
    QPushButton*     m_programButtons[8] {};
};

} // namespace uwp
