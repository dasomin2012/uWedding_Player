# uWeddingPlayer — 속성(PropertyPanel) UI 개편 리포트

> 대상 독자: 협업자(디버깅/리뷰어). 이 문서만으로 변경 맥락 파악 가능하도록 작성.
> 브랜치: `claude/amazing-yalow-5efda4`
> 관련 커밋: `cc0a681`(Stage 1), `7fddaa5`(Stage 2 개정)
> 원천 핸드오프: `docs/HANDOFF_PROPERTY_PANEL_2026-05-22.md`
> 대상 파일: `src/editor/PropertyPanel.{h,cpp}`, `src/scene/Layer.h`, `src/scene/SceneModel.h`, `src/editor/LayerItem.cpp`

---

## 1. 목적 / 배경

운영자 다수가 **결혼식 현장 50~60대 관리자**. 기존 PropertyPanel은 영문 라벨 + X/Y/W/H 직접 입력 + Opacity 0.00~1.00 스피너 + Z-order 4영문버튼이라 진입장벽이 컸다. 핸드오프(`HANDOFF_PROPERTY_PANEL_2026-05-22.md`)는 3단계(한글화 → 간단/고급 모드 → z-order 단순화)를 제안했고, 본 작업은 **Stage 1 그대로 + Stage 2는 운영자 피드백을 받아 설계를 바꿔** 진행했다.

**Stage 2 설계 변경(중요):** 핸드오프 원안은 "간단/고급 모드 토글 + 위치 프리셋 10개 + 슬라이더"였으나, 운영자 검토 후 다음으로 확정:
- 모드 토글 폐기 → **단일 패널**.
- 위치 프리셋 9개(좌상~우하) 삭제, **"꽉 채우기"만 유지**.
- 그 아래 **X/Y/W/H 직접 입력**(핸드오프의 "고급 위치 메뉴")을 그대로 노출.
- 크기 조정용 **"비율 고정" 체크박스** 추가 — 스피너뿐 아니라 **마우스 드래그 리사이즈에도** 적용.

GPL 트랙(OBS)과 무관. NovaStar/Phase 5와도 무관.

---

## 2. 변경 요약 (커밋 단위)

### Stage 1 — 한글화 (`cc0a681`, 2 files +34/-24)
- `PropertyPanel.cpp`: 운영자 표시 텍스트 전부 한글화.
  - 라벨: `왼쪽(X)`/`위(Y)`/`가로(W)`/`세로(H)`, `미디어`/`이름`/`위치 / 크기`/`투명도`/`표시 시간`/`재생 끝나면`/`표시 순서`, `(선택된 레이어 없음)`, `" 초"`.
  - 버튼: `맨 앞으로`/`한 칸 앞으로`/`한 칸 뒤로`/`맨 뒤로`/`레이어 삭제`.
- **EndAction 콤보박스 표시↔값 분리**: 표시는 한글(`반복 재생`/`정지`/`마지막 화면 유지`/`다음 프로그램으로`), 값은 `QVariant` userData에 `EndAction` enum. `loadFrom`은 `findData`, `commit`은 `itemData().value<EndAction>()`.
- `Layer.h`: `Q_DECLARE_METATYPE(uwp::EndAction)` (enum을 QVariant에 담기 위함).
- **직렬화 무변경**: `endActionToString/FromString`·`SceneSerializer` 미수정 → `scene.json` 키는 영문(loop/stop/hold/next) 유지.

### Stage 2(개정) — 단일 패널 + 비율 고정 리사이즈 (`7fddaa5`, 4 files +202/-73)
- `PropertyPanel.{h,cpp}`: 간단/고급 토글·`QStackedWidget`·프리셋 9개·크기 슬라이더 제거. 단일 패널 구성(아래 §3). "비율 고정" 체크박스 + 투명도 % 슬라이더 유지.
- **비율 고정(기본 ON)** — 단일 소스:
  - 스피너: `가로(W)` 변경 시 `세로(H)` 자동(역도 성립), 재진입 가드로 W↔H 루프 차단.
  - 마우스 드래그: 코너 핸들이 드래그 시작 시점 종횡비 유지(반대 코너 고정). 엣지 핸들은 단축 조정(고정 비대상).
- `SceneModel`: 비직렬화 편집 플래그 `aspectLocked()`/`setAspectLocked()` 추가 → PropertyPanel 체크박스와 LayerItem 드래그가 **하나의 상태**를 공유. PropertyPanel이 체크박스를 모델에 미러링.
- `LayerItem::mouseMoveEvent`: 코너 드래그에 종횡비 유지 분기 추가.

---

## 3. 최종 패널 구성 (현재 UI)

```
속성
미디어 : <경로>
이름   : [____]
[ 꽉 채우기 ]                         ← 강조(파란 배경)
위치 / 크기
  왼쪽(X)[  ]  위(Y)[  ]
  가로(W)[  ]  세로(H)[  ]
[☑ 비율 고정 (가로↔세로 함께 조정)]
투명도: NN%
[────────슬라이더────────]
표시 시간 : [  ] 초
재생 끝나면 : [반복 재생 ▾]
표시 순서
[ 맨 앞으로 ] [ 한 칸 앞으로 ]
[ 한 칸 뒤로 ] [ 맨 뒤로 ]
[ 레이어 삭제 ]
```

---

## 4. 핵심 설계 결정

| 항목 | 결정 / 근거 |
|---|---|
| 모드 토글 폐기 | 운영자 피드백. 어르신에게 모드 개념이 혼란 → 단일 화면이 단순. |
| 프리셋 9개 삭제 | "꽉 채우기" 외엔 잘 안 쓰고, X/Y/W/H 직접 입력으로 정밀 배치 충분. |
| 비율 고정 단일 소스 | 체크박스 상태를 `SceneModel.aspectLocked`(비직렬화)에 미러 → 스피너·드래그가 동일 플래그 참조. 두 경로의 동작 불일치 방지. |
| 드래그 비율 고정 = 코너만 | 코너=비례 확대/축소(반대 코너 고정), 엣지=단축. 표준 편집기 관례. |
| 비율 기준 = 드래그 시작 시점 | 드래그 도중 비율이 흔들리지 않게 `m_startSceneRect`의 W/H 비율 사용. |
| EndAction 표시/값 분리 | 한글 표시 + 영문 직렬화 양립. `endActionToString/FromString` 무수정. |
| 투명도는 % 슬라이더 | 0.00~1.00 스피너보다 어르신 친화. `sliderReleased`에서만 commit(드래그 중 모델 폭주 방지). |

---

## 5. 변경 파일 / 범위

| 파일 | 변경 | 비고 |
|---|---|---|
| `src/editor/PropertyPanel.h` | 단일 패널 멤버 구조 | Stage 2 |
| `src/editor/PropertyPanel.cpp` | 한글화 + 단일 패널 + 체크박스↔모델 동기화 | Stage 1·2 |
| `src/scene/Layer.h` | `Q_DECLARE_METATYPE(EndAction)` | Stage 1 |
| `src/scene/SceneModel.h` | `aspectLocked` 비직렬화 플래그 | Stage 2 |
| `src/editor/LayerItem.cpp` | 코너 드래그 종횡비 유지 | Stage 2 |

**손대지 않음:** `SceneSerializer`(직렬화 형식 불변), `Layer` struct 필드, `endActionToString/FromString/mediaTypeToString`, `ControlWindow`(개정 과정에서 추가했던 모드 영속 와이어링은 전부 되돌림), `MediaListWidget`/`Settings`(협업자 별도 트랙 — 미커밋 상태 유지).

> **범위 확장 고지:** 핸드오프 §5는 `LayerItem`/`SceneModel`을 범위 밖으로 명시했으나, "마우스 드래그 리사이즈에 비율 고정 적용"은 운영자가 직접 요청해 불가피하게 두 파일을 수정함. 직렬화·씬 데이터 모델 필드는 불변(추가한 건 세션 한정 UI 플래그뿐).

---

## 6. 검증 상태

- **자동(빌드):** `cmake --build build --config Release` 무에러. (`VCINSTALLDIR` windeployqt 경고는 프로젝트 초기부터의 무관 안내.)
- **운영자 GUI 확인 완료:**
  - 한글 라벨/버튼 표시.
  - "꽉 채우기" → 캔버스 가득.
  - 스피너 W↔H 비율 고정 ON/OFF.
  - **코너 드래그 비율 고정 ON → 종횡비 유지하며 리사이즈** (요청 기능, 정상 확인).
- **회귀 미발생:** EndAction 한글 표시 ↔ `scene.json` 영문 저장 round-trip 유지.
- **무한 루프 가드:** `loadFrom`/스피너 연동/드래그 전부 `m_loading`(또는 재진입 가드)으로 보호.

---

## 7. 리뷰/디버깅 중점 포인트

1. **비율 고정 단일 소스 정합성**: 체크박스 → `SceneModel.aspectLocked` 미러가 항상 동기인지(토글·초기화 경로). LayerItem은 드래그 시점에 `m_model->aspectLocked()`를 읽음.
2. **드래그 코너 앵커 수학**(`LayerItem::mouseMoveEvent`): 반대 코너 고정 + 지배 축 선택(`desiredW >= desiredH*aspect`) + 마우스 방향 배치. 4개 코너 전수 확인 권장.
3. **루프 안전성**: 스피너 `onWidthChanged`→`m_h->setValue`(가드)→`commitGeometry`→모델 `layerChanged`→`loadFrom`(가드). 어디서도 commit 재발화 없는지.
4. **엣지 핸들 정책**: 현재 비율 고정 비대상(단축). 운영자가 엣지에도 기대하면 정책 재논의.
5. **슬라이더 commit 시점**: `sliderReleased`만 commit → 홈(groove) 클릭만으론 미반영 가능. 어르신은 주로 드래그라 의도된 동작이나 필요 시 조정.
6. **transient 플래그 위치**: `aspectLocked`를 SceneModel에 둔 것이 적절한지(편집 UI 상태 vs 씬 데이터). 비직렬화이므로 저장엔 영향 없음.

---

## 8. 보류 / 다음

- **Stage 3 (표시 순서 단순화)**: 핸드오프 §3대로 **운영자 피드백 후 결정** 보류. 4버튼(맨앞/한칸앞/한칸뒤/맨뒤) 유지 중.
- 푸시: 본 리포트 작성 시점에 PropertyPanel 트랙 2커밋은 로컬(브랜치). 원격 공유 필요 시 push.

---

## 9. 커밋 이력

| 커밋 | 요지 |
|---|---|
| `cc0a681` | Stage 1: Korean PropertyPanel labels + EndAction userData |
| `7fddaa5` | PropertyPanel: single-panel layout + aspect-lock resize (Stage 2 revised) |
