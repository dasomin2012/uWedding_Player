#pragma once

#include <QMainWindow>

class QListWidget;
class QGraphicsView;
class QLabel;
class QWidget;
class QPushButton;

namespace uwp {

class Settings;

// Phase 1 운용자 화면. 모든 패널은 placeholder 위젯으로만 구성됨.
//   +--------+-----------------+--------+
//   | Media  | Preview | Mirror| Prop   |
//   | List   +-----------------+ Panel  |
//   |        | Program List(8) |        |
//   +--------+-----------------+--------+
class ControlWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ControlWindow(Settings* settings, QWidget* parent = nullptr);
    ~ControlWindow() override;

signals:
    void selectOutputMonitorRequested();
    void openSettingsRequested();

private:
    void createMenus();
    void createCentralLayout();

    Settings*       m_settings        = nullptr;

    QListWidget*    m_mediaList       = nullptr;
    QGraphicsView*  m_previewCanvas   = nullptr;
    QLabel*         m_liveMirror      = nullptr;
    QWidget*        m_propertyPanel   = nullptr;
    QPushButton*    m_programButtons[8] {};
};

} // namespace uwp
