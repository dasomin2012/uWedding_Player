#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QListWidget;
class QListWidgetItem;

namespace uwp {

struct Program;

// 하단 Program 리스트 (동적·스크롤·추가/삭제).
//  - 상단 [+ Add] 버튼 + QListWidget(IconMode, 가로 흐름).
//  - 각 아이템: 썸네일 + 이름. UserRole = program id.
//  - 단일클릭 = 선택(Preview 로드용), 더블클릭/컨텍스트 Play = 재생(Take).
// 위젯은 UI/시그널만 담당 — 실제 데이터 변경은 Application 이 ProgramRepository 로 수행.
class ProgramListWidget : public QWidget {
    Q_OBJECT
public:
    explicit ProgramListWidget(QWidget* parent = nullptr);

    // dataDir 기준 thumbnailRelPath 로 아이콘 로드. 없으면 기본 회색 박스.
    void setPrograms(const QVector<Program>& programs, const QString& dataDir);
    void setActiveProgram(const QString& id);   // 재생 중 강조 (Phase 5c)

signals:
    void addRequested();                              // [+ Add]
    void programSelected(const QString& id);          // 단일 클릭
    void playRequested(const QString& id);            // 더블클릭 / 컨텍스트 Play
    void renameRequested(const QString& id, const QString& newName);
    void deleteRequested(const QString& id);

private:
    QString currentId() const;                  // 선택 아이템의 id ("" 가능)
    void    showContextMenu(const QPoint& pos);

    QListWidget* m_list    = nullptr;
    QString      m_dataDir;
    QString      m_activeId;
};

} // namespace uwp
