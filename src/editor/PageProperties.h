#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QPushButton;

namespace uwp {

struct Page;

// 선택된 페이지의 편집 가능한 속성을 우측 패널에 노출.
//   - 이름 (QLineEdit, editingFinished 시 emit)
//   - 표시 시간 (QSpinBox, 초 단위, 0=수동)
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
    void displayTimeChanged(int seconds);
    void moveUpRequested();
    void moveDownRequested();

private:
    QLineEdit*   m_name        = nullptr;
    QSpinBox*    m_displayTime = nullptr;
    QPushButton* m_btnUp       = nullptr;
    QPushButton* m_btnDown     = nullptr;
    bool         m_loading     = false;   // setPage 중 시그널 발화 억제
};

} // namespace uwp
