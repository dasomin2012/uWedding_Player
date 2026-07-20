#pragma once

#include <QWidget>
#include <QString>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

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
    void selectProgram(const QString& id);      // 편집 대상 항목을 리스트에서 선택

    // 접기/펼치기 — 접었을 때는 헤더만(카드 리스트 숨김) 표시. 헤더의 제목
    // 라벨은 현재 편집 대상 프로그램 이름을 함께 보여준다.
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }
    void setCurrentProgramName(const QString& name);   // 접힌 상태 헤더 라벨용
    // 전체 재구성 없이 단일 항목만 갱신 (편집 자동저장 시 깜빡임 방지).
    // 이름·썸네일 외에 endAction/displayTime 도 카드 배지에 쓰이므로 갱신.
    void updateItem(const QString& id, const Program& p,
                    const QString& thumbAbsPath);

signals:
    void addRequested();                              // [+ Add]
    void programSelected(const QString& id);          // 단일 클릭
    void playRequested(const QString& id);            // 더블클릭 / 컨텍스트 Play
    void renameRequested(const QString& id, const QString& newName);
    void deleteRequested(const QString& id);
    // 접힘 상태 변화 — ControlWindow 가 QSplitter 크기를 재배분.
    void collapseChanged(bool collapsed);
    // 프로그램 단위 표시 시간 편집 요청. Application 이 현재값 조회 후
    // 다이얼로그 표시(위젯이 프로그램 데이터를 직접 갖지 않음).
    void displayTimeEditRequested(const QString& id);
    // 프로그램 종료 동작(반복/정지/유지/다음) 편집 요청. displayTime 과 동일 패턴.
    void endActionEditRequested(const QString& id);

private:
    QString currentId() const;                  // 선택 아이템의 id ("" 가능)
    void    showContextMenu(const QPoint& pos);

    QListWidget* m_list     = nullptr;
    QLabel*      m_title    = nullptr;    // 헤더 텍스트 (접힌 상태 시 이름 포함)
    QPushButton* m_btnToggle = nullptr;   // ∧ / ∨ 접기 토글
    QString      m_dataDir;
    QString      m_activeId;
    QString      m_currentProgramName;    // 접힌 상태 헤더용
    bool         m_collapsed = false;

    void updateHeaderLabel();             // m_title 텍스트 재계산
};

} // namespace uwp
