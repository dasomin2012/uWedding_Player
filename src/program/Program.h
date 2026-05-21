#pragma once

#include <QString>
#include <QVector>

#include "scene/Layer.h"   // EndAction, Layer 재사용

namespace uwp {

// CMS_WebServer 의 "page" 와 동등한 송출 단위.
// 하나의 Program 은 N개 레이어(영상/이미지) 스냅샷 + 표시시간 + 종료동작을 가진다.
// 순수 데이터 레코드 (QObject 상속 금지 → std::vector/QVector 에서 복사·이동 자유).
struct Program {
    QString        id;                  // "prog_<8hex>" 자동 발번
    QString        name;                // 운용자 표시
    QString        thumbnailRelPath;    // data 디렉터리 기준 상대경로 (예: "programs/thumbs/prog_xxx.png")
    QString        novastarPresetId;    // 빈 문자열이면 settings.novastar.default_preset_id 폴백
    int            displayTimeSec = 0;             // 0 = 수동 진행
    EndAction      endAction      = EndAction::Hold;  // 자동 진행 기본: 정지(안전)
    QVector<Layer> layers;              // SceneModel 스냅샷의 값 복사
};

} // namespace uwp
