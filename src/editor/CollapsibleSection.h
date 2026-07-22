#pragma once

#include <QFrame>
#include <QString>

class QPushButton;
class QLabel;
class QWidget;
class QVBoxLayout;
class QHBoxLayout;

namespace uwp {

// 접힘/펼침 컨테이너 — 우측 컬럼의 [Live · 미디어 속성 · 자막 속성] 3-스택에
// 사용. 접히면 헤더 한 줄만 남아 다른 섹션에 공간을 양보한다.
//   [▼ 미디어 속성                                            (우측 헤더 위젯)]
//   ↑ 클릭으로 토글                    ↑ setHeaderRight() 로 채워지는 자리
//                                       (예: Live 헤더의 상태 라벨 + ON/OFF)
// 접힘 상태에서도 우측 헤더 위젯은 계속 보이도록 하여, 상태 표시·응급 컨트롤을
// 잃지 않는다.
class CollapsibleSection : public QFrame {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    // 컨텐츠 위젯 설정 — 이전 컨텐츠는 delete. 소유권을 이 섹션이 가짐.
    void setContent(QWidget* content);

    // 헤더 우측에 위젯 배치 (선택). 접힘 상태에서도 보임.
    // 예: Live 상태 라벨 + ON/OFF 버튼.
    void setHeaderRight(QWidget* widget);

    void setExpanded(bool on);
    bool isExpanded() const { return m_expanded; }

signals:
    void expandedChanged(bool expanded);

protected:
    // 헤더 라벨 클릭도 화살표와 동일하게 토글로 처리.
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void updateArrow();

    QPushButton* m_toggle       = nullptr;
    QLabel*      m_title        = nullptr;
    QHBoxLayout* m_headerLayout = nullptr;
    QFrame*      m_headerFrame  = nullptr;   // 헤더 전체 클릭 감지용
    QWidget*     m_headerRight  = nullptr;
    QWidget*     m_content      = nullptr;
    bool         m_expanded     = true;
};

} // namespace uwp
