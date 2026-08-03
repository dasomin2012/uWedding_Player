#pragma once

#include <QString>
#include <QVector>

#include "scene/Layer.h"   // EndAction, Layer 재사용

namespace uwp {

// UI-D Phase A: Page — 프로그램 내부의 순차 재생 단위 (CMS_WebServer 의
// preset.pages 개념과 유사, 단 인덱스 대신 UUID id 부여).
//   * displayTimeSec 은 페이지 단위 (0 = 수동 TAKE 필요, >0 = 자동 다음 페이지)
//   * layers 는 페이지 소유 — Preview 캔버스는 "현재 편집중 페이지"의 layers 렌더
struct Page {
    QString        id;                  // "page_<hex>" — UUID (CMS 의 인덱스 부채 회피)
    QString        name;                // 사용자 지정 라벨 (빈 문자열이면 순번 표시)
    QString        thumbnailRelPath;    // 페이지별 썸네일 (없으면 프로그램 대표 사용)
    int            displayTimeSec = 0;
    QVector<Layer> layers;              // 이전 Program.layers 에 해당
};

// CMS_WebServer 의 "preset" 과 동등한 송출 단위.
// 하나의 Program 은 N개 Page 를 가지며 순차 재생. 마지막 페이지 종료 후
// program.endAction 이 다음 프로그램 여부(Next/First/Loop/Stop/Hold)를 결정.
// 순수 데이터 레코드 (QObject 상속 금지 → QVector 복사·이동 자유).
// invariant: pages 는 항상 최소 1개 (기본 생성자가 빈 페이지 하나 보장).
struct Program {
    QString        id;                  // "prog_<8hex>" 자동 발번
    QString        name;                // 운용자 표시
    QString        thumbnailRelPath;    // data 디렉터리 기준 상대경로
    QString        novastarPresetId;    // 빈 문자열이면 settings.novastar.default_preset_id 폴백
    EndAction      endAction      = EndAction::Hold;   // 마지막 페이지 종료 후 (Program 레벨)
    QVector<Page>  pages          { Page{} };          // 최소 1페이지 invariant

    // BGM — 프로그램 전체에 흐르는 배경음. TAKE 로 Live 진입 시 재생 시작,
    //  다른 프로그램으로 TAKE 되거나 종료 시 정지. 페이지 전환과 독립.
    QString        bgmPath;             // 절대 또는 상대 경로. 빈 문자열=미사용
    int            bgmVolume     = 80;  // 0..100
    bool           bgmLoop       = true;
};

} // namespace uwp
