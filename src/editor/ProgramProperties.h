#pragma once

#include <QWidget>
#include <QString>

#include "scene/Layer.h"   // EndAction

class QLineEdit;
class QComboBox;
class QPushButton;

namespace uwp {

struct Program;

// 편집중 프로그램의 속성 편집 위젯 (우측 패널).
//   - 이름 (QLineEdit)
//   - 종료 동작 (QComboBox: Loop/Stop/Hold/Next/First)
// 프로그램 단위 표시 시간은 Phase A 에서 첫 페이지 시간과 동일 개념이라
// PageProperties 로 이관 — 여기선 프로그램만의 필드에 집중.
class ProgramProperties : public QWidget {
    Q_OBJECT
public:
    explicit ProgramProperties(QWidget* parent = nullptr);

    // programIndex/programCount 는 앞/뒤로 버튼 활성 판단용 (경계 도달 시 비활성).
    void setProgram(const Program* program,
                    int programIndex = -1, int programCount = 0);

signals:
    void renameRequested(const QString& newName);
    void endActionChanged(EndAction action);
    void moveUpRequested();     // 앞으로 (index 감소)
    void moveDownRequested();   // 뒤로 (index 증가)

private:
    QLineEdit*   m_name      = nullptr;
    QComboBox*   m_endAction = nullptr;
    QPushButton* m_btnUp     = nullptr;
    QPushButton* m_btnDown   = nullptr;
    bool         m_loading   = false;
};

} // namespace uwp
