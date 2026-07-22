#pragma once

#include <QVector>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace uwp {

struct Program;
struct Page;

// UI-D Phase B: 좌측 컬럼 [페이지] 탭 위젯. 세로 페이지 카드 스택 + [+ 페이지 추가].
//   - 현재 편집 대상 프로그램의 페이지들을 표시 (setProgram 으로 주입).
//   - 카드 클릭 → pageSelected(id) → Application 이 이전 페이지 자동저장 후
//     새 페이지 layers 를 SceneModel 로 로드.
//   - + 페이지 추가 → addRequested() → Application 이 Program.pages 에 push +
//     save + 새 페이지로 자동 스위치.
//   - 카드 hover/선택 시 X 버튼(삭제), 우클릭 메뉴(이름 변경 / 위로 / 아래로).
class PageListWidget : public QWidget {
    Q_OBJECT
public:
    explicit PageListWidget(QWidget* parent = nullptr);

    // 편집 대상 프로그램의 페이지 목록으로 전체 재구성.
    //   program==nullptr : 프로그램 없음(스크래치) — 목록 비우고 + 버튼 비활성.
    //   dataDir          : 썸네일 상대경로 앞에 붙일 base (Program 카드와 동일).
    void setProgram(const Program* program, const QString& dataDir);
    // 현재 편집 중 페이지 하이라이트.
    void setActivePage(const QString& pageId);
    // 상위 CollapsibleSection 이 이 위젯을 감쌀 때, 섹션 헤더 타이틀과
    // 내부 "페이지" 라벨이 중복 노출되지 않도록 숨김 제어. 라벨 자체 위치는
    // 유지 (레이아웃 안정성).
    void setTitleVisible(bool visible);
    // "+ 페이지 추가" 버튼 노출 — 상위가 setHeaderRight 로 섹션 헤더에
    // 재부모하면 내부 topRow 는 사실상 빈 껍데기로 축소된다.
    QPushButton* addButton() const { return m_btnAdd; }

signals:
    void addRequested();                                 // [+ 페이지 추가]
    void pageSelected(const QString& id);                // 카드 클릭
    void deleteRequested(const QString& id);             // 카드 X (또는 우클릭)
    void renameRequested(const QString& id, const QString& newName);
    void moveUpRequested(const QString& id);
    void moveDownRequested(const QString& id);
    // 페이지 단위 표시 시간(자동 진행 초) 편집 요청 — Application 이 현재값
    // 조회 후 QInputDialog::getInt 팝업.
    void displayTimeEditRequested(const QString& id);

private:
    void showContextMenu(const QPoint& pos);

    QListWidget* m_list    = nullptr;
    QLabel*      m_titleLabel = nullptr;   // setTitleVisible 대상
    QPushButton* m_btnAdd     = nullptr;   // addButton() 반환값
    QString      m_dataDir;
    QString      m_activeId;
};

} // namespace uwp
