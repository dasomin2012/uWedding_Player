#pragma once

#include <QWidget>
#include <QString>

#include "scene/Layer.h"   // EndAction

class QLineEdit;
class QComboBox;

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

    void setProgram(const Program* program);

signals:
    void renameRequested(const QString& newName);
    void endActionChanged(EndAction action);

private:
    QLineEdit* m_name      = nullptr;
    QComboBox* m_endAction = nullptr;
    bool       m_loading   = false;
};

} // namespace uwp
