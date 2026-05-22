# uWeddingPlayer — 현재 상황 파악 보고서

> 작성: 2026-05-19  ·  대상: 후속 디버깅·리뷰 진행자
> 워크트리: `.claude/worktrees/amazing-yalow-5efda4`  ·  브랜치: `claude/amazing-yalow-5efda4`
> 직전 커밋(보고서 작성 기준): `747d5e7`(O4-fix 6건+실송출 검증) → `70c622c`(`docs/OBS_INTEGRATION_REPORT.md` 추가)

---

## 0. 한눈 요약

- OBS 송출 백엔드 통합 작업 트랙 **O0–O4 완료**. 실하드웨어(OBS 32.1.2 / obs-websocket 5.7.3) 송출 검증 완료. **O5(NovaStar 동기), O6(qt 폴백·LICENSES·대형 캔버스 부하)는 미착수**.
- 보고서 `docs/OBS_INTEGRATION_REPORT.md`(12.7KB, 자기완결식)가 `70c622c`로 커밋·푸시되어 협업자가 브랜치 풀로 즉시 열람 가능한 상태.
- 현재 디버깅 의뢰는 들어와 있지 않음. **다음 액션은 사용자가 우선순위를 지정한 뒤 착수**(O5/O6/Document 송출 또는 리뷰 지적 사항 대응).

---

## 1. 현재 작업 폴더 상태

본 세션이 접근 가능한 작업 폴더는 두 개다.

| 마운트 | 내용 |
|---|---|
| `D:\PycharmProjects\uWeddingPlayer\웨딩홀 미디어 Player 개발` | 비어 있음 (현재 산출물 없음) |
| `D:\PycharmProjects\uWeddingPlayer\.claude\worktrees\amazing-yalow-5efda4` | 본 작업 워크트리. 소스/빌드/문서 모두 여기 |

워크트리의 git 메타(`.git`)는 메인 저장소(`D:\PycharmProjects\uWeddingPlayer\.git\worktrees\amazing-yalow-5efda4`)를 가리키고 있어 본 환경에서 `git log` 직접 실행은 불가. 커밋 이력 확인은 `OBS_INTEGRATION_REPORT.md` 9절 “변경 이력” 표 + 사용자 보고(70c622c)에 의존한다.

`third_party/libvlc`는 워크트리에 디렉터리 정션(symlink)으로 걸려 있다. Windows에선 정상 동작하나 본 Linux 샌드박스에선 I/O 에러로 펼쳐 보이지 않는다(P1 해결 결과의 부산물). 빌드 시 libVLC 실재 여부 자체는 영향 없음.

---

## 2. 보고서(`OBS_INTEGRATION_REPORT.md`) ↔ 실제 코드 대조

OBS 통합 보고서가 기술한 핵심 결정과 픽스가 코드에 실제로 반영되어 있는지 spot-check한 결과.

### 2.1 절단면 / 백엔드 분리

- `src/live/ILiveSink.h`에 `setCanvasSize / showOnMonitor / setTransition / applyScene(onCommitted) / transitionAnchor`가 그대로 존재. 비-QObject 순수 추상이라 `LiveWindow`(QWidget)와 다중상속 가능. ✓
- `src/take/TakeController.cpp`는 `m_live->setTransition(...)` 호출 후 `transitionAnchor()`가 `nullptr`이면 `TransitionEffect`가 Qt dip-to-black을 우회하고 즉시 `applyScene`을 부르도록 위임하는 구조. ✓
- 백엔드 와이어링은 `src/app/Application.cpp::initialize()`에서 `engine == "obs"`일 때만 `ObsProcessManager` + `ObsLiveBackend`를 만들고, `sink = m_obsBackend.get()`로 교체. `m_obsProc->start()`도 여기서 호출. ✓

### 2.2 GPL 격리

- `src/obs/ObsClient.{h,cpp}`는 `QtWebSockets`(LGPL)만 사용. `libobs` 헤더/심볼/링크 일체 없음. ✓
- CMake도 `find_package(Qt5 COMPONENTS WebSockets)` 결과로만 `UWP_HAS_OBS`를 켜고, `Qt5::WebSockets`만 링크. libobs 관련 절대 미포함. ✓
- `third_party/obs/`는 포터블 OBS 런타임 배치 디렉터리(`portable_mode.txt`, `bin/`, `config/`, `data/`, `obs-plugins/`, README) — exe만 별도 프로세스로 실행, 우리 exe와 무접촉. ✓

### 2.3 송출 모델: Studio Mode 폐기 → SetCurrentProgramScene 직접 전환

- `ObsLiveBackend::seed()`에서 `SetStudioModeEnabled = false`를 명시적으로 보냄(코드 라인 113–117). ✓
- `triggerTransition()`는 `SetCurrentSceneTransition`(이름은 kind로 찾은 `m_cutName`/`m_fadeName`) → `SetCurrentSceneTransitionDuration`(fade일 때) → `SetCurrentProgramScene(target)` 순서로 호출. Studio Mode/`TriggerStudioModeTransition`/`SetCurrentPreviewScene` 호출 없음. ✓
- `doApply()`는 `(programScene == sceneA) ? sceneB : sceneA`로 off-air 씬을 골라 `rebuildScene` 후 전환. ping-pong 패턴 그대로. ✓

### 2.4 브링업 결함 6건 — 코드에서의 확인 위치

| # | 항목 | 확인 위치 | 상태 |
|---|---|---|---|
| P1 | libVLC stub 제거(정션 연결) | 환경 셋업(`third_party/libvlc` 정션). 코드 변경 아님 | 적용됨(워크트리에 정션 존재) |
| P2 | 미디어 경로 절대화 | `ObsLiveBackend::buildLayer` — `QDir(applicationDirPath()).absoluteFilePath(L.media)` | ✓ 코드 247–249 |
| P3 | 입력명 충돌 해소(시딩 purge + 세션 단조 카운터) | `seed()`에서 `GetInputList` → `uwp_*` 일괄 `RemoveInput`. `buildLayer`에서 `uwp_<++m_inputSeq>` | ✓ 코드 154–186 / 277–278 |
| P4 | `hw_decode=false` | `buildLayer`에서 `settings["hw_decode"] = false` | ✓ 코드 262 |
| P5 | 전환 로케일 의존 제거 | `seed()`에서 `GetSceneTransitionList` → `transitionKind`(`cut_transition`/`fade_transition`)로 표시이름 저장 | ✓ 코드 122–139 |
| P6 | 빈 씬 전환 → Studio Mode 폐기 | `seed()` `SetStudioModeEnabled=false`, `triggerTransition`이 `SetCurrentProgramScene` 직접 사용 | ✓ 코드 113–117, 348–407 |

### 2.5 추가 안전조치

- `ObsClient::~ObsClient()`에서 `m_sock->blockSignals(true)` + `abort()`. 소멸 중 `disconnected → failAllPending`이 파괴 직전 백엔드 콜백을 호출하는 UAF 차단. ✓ (`src/obs/ObsClient.cpp` 29–36)
- `Application` 멤버 선언 순서: `m_obsBackend` → `m_obsProc`. C++의 “역순 소멸” 규칙에 따라 `m_obsProc`가 먼저 소멸 → ObsClient `blockSignals`/`abort`로 보류 콜백 차단된 상태에서 `m_obsBackend`가 나중에 안전 소멸. ✓ (`src/app/Application.h` 53–57)
- 디버그 플래그: `UWP_OBS_PING`(`Application.cpp` 184–208), `UWP_OBS_NOHIDE`(`ObsProcessManager.cpp` 262–265) 모두 코드에 존재. 정상 운영(무 플래그)에서는 OBS 메인창 숨김. ✓

### 2.6 발견된 사소한 docstring 노후(설계 변경 미반영)

설계가 “Studio Mode + Preview/Program 전환”에서 “Studio Mode 비활성 + `SetCurrentProgramScene` 직접 전환”으로 바뀌었으나, 다음 두 코멘트는 옛 설계 어휘를 유지하고 있다. 동작에는 영향 없음.

1. `src/live/ILiveSink.h` 라인 22–24 — `setTransition` 주석: “OBS 백엔드는 Studio Mode 전환에 사용”.
2. `src/obs/ObsLiveBackend.h` 라인 22–24 — 클래스 주석: “Studio Mode + 두 씬(PGM_A/PGM_B) 핑퐁”.

리뷰 시 새로 들어온 분이 “Studio Mode”라는 단어 때문에 옛 설계로 오해할 가능성이 있어 후속 작업 때 함께 정리하면 좋다. 단독 커밋으로 분리할 가치는 낮고, O5 작업 끝물에 함께 묶는 정도가 적절.

### 2.7 Settings 스키마 대칭성

`src/app/Settings.{h,cpp}` 확인 결과 `engine` 필드와 `obs` 블록(`exe_path` / `ws_url` / `ws_password` / `scene_a` / `scene_b` / `projector_monitor`) 모두 load/save 양방향 대칭으로 구현. ✓

---

## 3. 검증 상태

- **실하드웨어 검증 완료**: 관리형 OBS 기동 → (선택적) 메인창 숨김 → 시딩 → 다중 레이어(`ffmpeg_source`) 합성 → Cut/Fade 전환 → 확장 모니터 프로젝터 정상 송출. `program scene == target` 일치. OBS 32.1.2 / ws 5.7.3.
- **컴파일·링크만 검증**: GUI 실행이 불가한 환경에서 도는 자동 검증은 빌드 통과까지.
- **미검증/보류**: Document/PPT 송출(현재 경고 후 skip), opacity 색필터(버전 의존 best-effort), 대형 캔버스(10368×2808) 부하, OBS 크래시 자동 재기동 실시나리오, NovaStar 동기 종단간.

---

## 4. 미해결·미착수 작업

### 4.1 O5 — NovaStar 동기

- 핵심 이벤트는 이미 있음: `ObsLiveBackend::onObsEvent`에서 `SceneTransitionEnded`를 받아 `transitionEnded()` 시그널 emit (`src/obs/ObsLiveBackend.cpp` 411–421).
- `NovaStarController` 클래스 자체가 미구현(원래 로드맵 Phase 6 항목)이므로 O5는 “인터페이스+스텁부터”가 1차 작업이 될 가능성이 큼. presetId 매핑은 Program 데이터모델에 이미 표시 항목(`novastar_preset_id`)이 있지만 송출 트리거가 없음.

### 4.2 O6 — qt 자동 폴백 / LICENSES / 대형 캔버스 부하

- `ObsProcessManager` 상태가 `Failed`로 떨어지면 현재는 상태바 메시지만 띄움(`Application.cpp` 114–119). “OBS 죽으면 qt 백엔드로 무중단 폴백”은 미구현. 본식 무중단 관점에서 우선순위가 높을 수 있음.
- 사용자(또는 법무) 측 LICENSES/NOTICE 파일 정비는 OBS 포터블 동봉을 정식 배포 절차에 넣을 때 별도 트랙 필요.

### 4.3 Document/PPT 송출

- 현재 `buildLayer`에서 `MediaType::Video|Image`가 아니면 경고 후 skip. 보고서 8절 6번 항목으로 “기존 `SnapshotCache`(FFmpeg 페이지 렌더) → `image_source`로 브리지”라는 설계 방향이 이미 제시되어 있음. 실사용 비중이 크다는 단서가 있으므로 우선순위 평가 필요.

---

## 5. 리뷰 게이팅으로 권장되는 7개 질문 (보고서 8절 발췌)

후속 리뷰 진행 시 다음 순서로 보는 것을 추천한다(보고서 8절). 본 워크트리에서 핵심 검토 대상은 **2번(비동기 콜백 수명)**과 **1번(GPL 경계)**이다.

1. **GPL 경계**: `src/obs/*` 및 그 의존이 libobs를 끌어오지 않는지. obs-websocket 외 OBS 결합 없는지.
2. **비동기 콜백 수명**: 중첩 람다 체인(CreateInput→Transform→Index→opacity→next) + 백엔드/프로세스 소멸 순서 + `failAllPending`의 상호작용. 멤버 순서/`blockSignals`로 방어 중이나 정밀 검토 필요.
3. **세션 간 상태**: 포터블 config 영속 + 시딩 purge + 유니크 입력명이 크래시 재기동 후에도 정합인지.
4. **버전 의존**: `color_filter_v2`, `OpenVideoMixProjector`, 전환 kind 문자열 — OBS 버전 변경 시 재검증.
5. **전환 정확성**: `SetCurrentProgramScene` vs `SceneTransitionEnded` 경합. O5 NovaStar 동기의 기준점.
6. **Document/PPT 보류**: SnapshotCache → image_source 브리지 설계.
7. **에러/폴백**: OBS Ready 실패·재기동 한계 초과 시 qt 폴백 미구현.

---

## 6. 본 세션에서 권장되는 다음 액션 (대기 중)

사용자가 “현재 상황만 파악” 지시. 디버깅·구현 작업은 우선순위 지정 후 착수. 후보를 우선순위 가설과 함께 적어 둔다.

1. **(권장) 비동기 콜백 수명 정밀 검토**: 보고서 8-2를 “이론적으로 안전”에서 “시나리오별로 안전 증명”까지 끌어올림. 검토 후 발견 시 패치, 미발견 시 코멘트 강화. 코드 영향 최소.
2. **(권장) 코멘트 노후 정리**: 2.6항 두 군데. 작업량 5분, 리뷰 가독성에 직접 기여.
3. **O5 NovaStar 동기 스텁**: `NovaStarController`를 인터페이스+no-op 스텁으로 추가, `ObsLiveBackend::transitionEnded()`에 와이어링. presetId 룩업 자리만 비워둠.
4. **O6 qt 자동 폴백**: `ObsProcessManager::State::Failed` 진입 시 `Application`이 sink를 `LiveWindow`로 재바인딩. 본식 무중단의 핵심.
5. **Document/PPT 브리지**: 보고서 8-6 설계 그대로 구현.

---

## 7. 참고 파일 맵 (이 보고서 기준)

- 보고서 본체: `docs/OBS_INTEGRATION_REPORT.md` — 12.7KB, 자기완결식. 후임자는 이 한 파일만 보고 온보딩 가능.
- 본 보고서: `docs/STATE_ASSESSMENT_2026-05-19.md` — 코드 대조 + 다음 액션 후보.
- 핵심 코드: `src/live/ILiveSink.h` · `src/obs/ObsClient.{h,cpp}` · `src/obs/ObsProcessManager.{h,cpp}` · `src/obs/ObsLiveBackend.{h,cpp}` · `src/app/Application.{h,cpp}` · `src/take/TakeController.{h,cpp}` · `src/app/Settings.{h,cpp}` · `CMakeLists.txt` · `third_party/obs/README.md`.
