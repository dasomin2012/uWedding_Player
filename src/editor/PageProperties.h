#pragma once

#include <QWidget>
#include <QString>

#include "scene/Layer.h"   // EndAction

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QPushButton;

namespace uwp {

struct Page;
class MediaDurationProbe;

// 선택된 페이지의 편집 가능한 속성을 우측 패널에 노출.
//   - 이름 (QLineEdit, editingFinished 시 emit)
//   - 표시 시간 ([재생 길이 | 시간 지정] + 초 QSpinBox, 0=재생 길이)
//   - 종료 동작 (QComboBox: 반복/다음 페이지/첫 페이지/정지)
//   - 위/아래로 이동 버튼
// 위젯은 상태를 소유하지 않는다 — Application 이 현재 편집중 페이지 id 를
// 알기 때문에, 시그널은 값만 전달.
class PageProperties : public QWidget {
    Q_OBJECT
public:
    explicit PageProperties(QWidget* parent = nullptr);

    // 페이지 없음(nullptr) 이면 필드 비활성. pageIndex/pageCount 는 위/아래
    // 버튼 활성 여부 판단용.
    void setPage(const Page* page, int pageIndex = -1, int pageCount = 0);

signals:
    void renameRequested(const QString& newName);
    void displayTimeChanged(int seconds);   // 0 = 재생 길이
    void endActionChanged(EndAction action);
    void moveUpRequested();
    void moveDownRequested();

private:
    QLineEdit*   m_name        = nullptr;
    QComboBox*   m_timeMode    = nullptr;   // 0=재생 길이, 1=시간 지정
    QSpinBox*    m_displayTime = nullptr;   // 시간 지정일 때만 노출 (초)
    QLabel*      m_videoLen    = nullptr;   // 재생 길이일 때 실제 영상 길이 표시
    MediaDurationProbe* m_probe = nullptr;
    QString      m_videoPath;               // 현재 페이지 대표 영상 (빈값=없음)
    void updateTimeWidgets();               // 모드에 따라 초 입력 ↔ 영상 길이 전환
    QComboBox*   m_endAction   = nullptr;
    QPushButton* m_btnUp       = nullptr;
    QPushButton* m_btnDown     = nullptr;
    bool         m_loading     = false;   // setPage 중 시그널 발화 억제
};

} // namespace uwp
