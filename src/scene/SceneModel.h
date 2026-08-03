#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QSize>

#include "Layer.h"

class QTimer;

namespace uwp {

// 씬(편집중 프리셋)의 단일 진실 소스.
// 모든 뷰(PreviewCanvas/PropertyPanel/MediaList)는 이 모델만 보고 동기화한다.
// m_layers 는 항상 zIndex 오름차순 정렬 유지.
class SceneModel : public QObject {
    Q_OBJECT
public:
    explicit SceneModel(QObject* parent = nullptr);

    void  setCanvasSize(const QSize& s);
    QSize canvasSize() const { return m_canvas; }

    // 반환: 새 레이어 id
    QString addLayer(const QString& mediaPath, const QRectF& geometry);
    // 텍스트 레이어 신규 생성 — 미디어 경로 없이 캔버스에 바로 배치.
    // geometry 비어있으면 캔버스 중앙 하단(기본 자막 자리)에.
    QString addTextLayer(const QString& initialText,
                         const QRectF& geometry = QRectF());
    void    removeLayer(const QString& id);
    // 지정 레이어를 복제 — 새 id 로 값 복사, 위치는 원본에서 살짝 오프셋
    // (Ctrl+D UX). 실패 시 빈 문자열. 성공 시 새 id 반환 + 자동 선택.
    QString duplicateLayer(const QString& id);
    void    clear();

    int                   layerCount() const { return m_layers.size(); }
    const QVector<Layer>& layers()     const { return m_layers; }
    const Layer*          layer(const QString& id) const;

    void setGeometry(const QString& id, const QRectF& g);
    void setOpacity(const QString& id, double op);
    void setDisplayTime(const QString& id, int sec);
    void setEndAction(const QString& id, EndAction a);
    void setName(const QString& id, const QString& name);

    // 텍스트 위젯 전용 필드 (mediaType == Text 일 때만 의미).
    // 각 setter 는 값이 실제로 바뀌었을 때만 layerChanged emit → 불필요 리페인트 방지.
    void setText(const QString& id, const QString& text);
    void setTextColor(const QString& id, const QString& color);
    void setFontSize(const QString& id, int px);
    void setFontFamily(const QString& id, const QString& family);
    void setFontWeight(const QString& id, int weight);
    void setTextAlign(const QString& id, int align);      // 0=Left, 1=Center, 2=Right
    void setTextVAlign(const QString& id, int align);     // 0=Top,  1=Center, 2=Bottom
    void setBgColor(const QString& id, const QString& color);
    void setBgOpacity(const QString& id, double op);      // 0.0~1.0
    void setPadding(const QString& id, int px);

    // z-order (정렬 + 연속 zIndex 재배치)
    void raise(const QString& id);
    void lower(const QString& id);
    void toFront(const QString& id);
    void toBack(const QString& id);

    // 선택 ("" = 해제)
    void    select(const QString& id);
    QString selectedId() const { return m_selected; }

    // 편집 보조 플래그(비직렬화): 리사이즈 시 종횡비 유지 여부.
    // PropertyPanel "비율 고정" 체크박스가 단일 소스. LayerItem 드래그가 읽음.
    bool aspectLocked() const { return m_aspectLocked; }
    void setAspectLocked(bool on) { m_aspectLocked = on; }

    // 직렬화용 일괄 교체 (SceneSerializer 가 사용)
    void replaceAll(const QVector<Layer>& layers);

    // ---- Undo / Redo (스냅샷 기반) ----
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    // 새 컨텍스트(프로그램/페이지 로드) 로 진입 시 히스토리 초기화.
    // 현재 레이어 상태를 첫 스냅샷으로 등록.
    void resetHistory();
    // 대기중 디바운스 스냅샷을 즉시 확정 (프로그램 전환 전 편집 저장 등).
    void flushHistory();

signals:
    void layerAdded(const QString& id);
    void layerRemoved(const QString& id);
    void layerChanged(const QString& id);   // geometry/opacity/... 변경
    void zOrderChanged();                    // 순서 변경 → 뷰가 zValue 재적용
    void selectionChanged(const QString& id);
    void sceneReset();                       // clear / replaceAll
    // Undo/Redo 가용성 변경 — UI(메뉴/툴바) 활성 상태 동기화용.
    void historyChanged();

private:
    void scheduleHistoryPush();   // 변경 감지 → 디바운스 타이머 재시작
    void pushHistoryNow();        // 실제 스냅샷 append (redo tail 절단 + cap)
    int  indexOf(const QString& id) const;
    void sortByZ();
    void normalizeZ();                       // 0..n-1 연속 재배치

    QSize          m_canvas{1920, 1080};
    QVector<Layer> m_layers;
    QString        m_selected;
    int            m_idSeq = 0;
    bool           m_aspectLocked = true;   // 편집 보조(비직렬화)

    // 히스토리 스택 (undo/redo). 현재 상태 인덱스 = m_historyIdx.
    QVector<QVector<Layer>> m_history;
    int      m_historyIdx      = -1;
    QTimer*  m_historyTimer    = nullptr;
    bool     m_suppressHistory = false;   // undo/redo 내부 replaceAll 억제
    static constexpr int kHistoryCap = 50;
};

} // namespace uwp
