# uWeddingPlayer — OBS 송출 백엔드 통합 보고서

> 대상 독자: 디버깅 전문가 / 코드 리뷰어 (이 문서만으로 온보딩 가능하도록 작성).
> 브랜치: `claude/amazing-yalow-5efda4` · 최종 커밋 `747d5e7`
> 상태: **O0–O4 완료, 실하드웨어(OBS 32.1.2) 실송출 검증 완료**. O5–O6 미착수.

---

## 1. 목적 / 배경

기존 `uWeddingPlayer`는 Qt + libVLC(LGPL)로 확장 모니터에 영상을 송출한다(`engine=qt`).
여기에 **대안 송출 엔진으로 OBS를 추가**(`engine=obs`)하는 작업을 단계(O0–O6)로 진행 중이다.

### 1.1 GPL 격리 — 설계의 제1제약 (반드시 숙지)

- 본 제품은 **웨딩홀 납품 상용·비공개 소스** 제품이다.
- OBS / libobs 는 **GPLv2**. libobs를 우리 exe에 링크하면 GPL 전염 → 제품 전체 소스 공개 의무 → 상용 치명적.
- 따라서 **OBS는 별도 프로세스로 실행하고, 제어는 obs-websocket(네트워크 프로토콜)로만** 한다.
  - 우리 exe는 `Qt5::WebSockets`(LGPL)만 링크. libobs 심볼/헤더/DLL **무접촉**.
  - NovaStar 글루 등 우리 IP는 전부 클라이언트(우리 exe) 측에. **OBS 플러그인/Lua/Python 작성 금지**(작성 즉시 GPL 영역).
- 리뷰 시 OBS 관련 변경마다 “이게 여전히 arm’s-length(비파생)인가?”를 게이팅 질문으로 삼을 것.
- 참고: `third_party/obs/README.md`(포터블 OBS 배치·버전·GPL 근거).

---

## 2. 아키텍처 요약

### 2.1 절단면 (`ILiveSink`)

`TakeController`가 송출 백엔드를 직접 알지 못하도록 순수 추상 인터페이스 뒤로 분리.

```
TakeController ──(ILiveSink*)──▶  LiveWindow      (engine=qt,  libVLC, 위젯)
                                  ObsLiveBackend  (engine=obs, obs-websocket, 비위젯)
```

- `src/live/ILiveSink.h`: `setCanvasSize / showOnMonitor / setTransition / applyScene(layers,onCommitted) / transitionAnchor`.
- `LiveWindow`는 `QWidget`이자 `ILiveSink`(다중상속, 인터페이스는 비-QObject 순수추상이라 moc 충돌 없음). `transitionAnchor()`=this.
- `ObsLiveBackend`는 비위젯 → `transitionAnchor()`=nullptr → `TransitionEffect`(Qt dip-to-black)는 OBS에서 우회되고, **전환은 OBS가 자체 수행**. 그래서 `setTransition(fade,durMs)`로 의도를 전달.

### 2.2 OBS 측 구성요소 (`src/obs/`)

| 파일 | 책임 |
|---|---|
| `ObsClient.{h,cpp}` | obs-websocket v5 **순수 전송**: 연결, Hello/Identify(SHA256 인증), requestId 상관, Event 패스스루. QtWebSockets만. |
| `ObsProcessManager.{h,cpp}` | 포터블 OBS exe 경로 해석 → `QProcess` 기동 → 상태머신 → 웹소켓 Ready 폴링 → 메인창 Win32 숨김 → Program 프로젝터 오픈 → `GetVersion` 하트비트 → 비정상 종료 시 백오프 재기동. |
| `ObsLiveBackend.{h,cpp}` | `ILiveSink` 구현. OBS 시딩, 레이어→OBS 입력/씬아이템 매핑, 두 씬(PGM_A/PGM_B) 핑퐁 + 전환. |

### 2.3 송출 모델 (핵심 — 초기 설계에서 **변경됨**)

- 두 씬 `UWP_PGM_A` / `UWP_PGM_B` 를 ping-pong. **off-air 씬을 재구성**(현재 program이 아닌 씬) → 한 번에 program 전환 → 송출 중 깜빡임 없음.
- **초기 설계(Studio Mode preview + `TriggerStudioModeTransition`)는 폐기**(아래 6번 문제 참조). 현재는 **Studio Mode 비활성 + `SetCurrentProgramScene(target)` 직접 전환**(활성 scene transition으로 전환).
- 캔버스 = 논리 캔버스(`SetVideoSettings`), 레이어 geometry는 OBS 캔버스와 **1:1**(`SetSceneItemTransform`, `OBS_BOUNDS_STRETCH`). z = `SetSceneItemIndex`.
- 미디어: 영상=`ffmpeg_source`, 이미지=`image_source`. **Document/PPT는 현재 경고 후 skip**(보류).

---

## 3. 단계별 진행 현황

| 단계 | 내용 | 커밋 | 상태 |
|---|---|---|---|
| O0 | `ILiveSink` 절단면 추출(무동작 리팩터) | `0f8c9ec` | 완료 |
| O1 | Settings `engine`/`obs`, CMake `Qt5::WebSockets` 게이팅, `third_party/obs/README.md` | `ce404a2` | 완료 |
| O2 | `ObsClient` (obs-websocket v5 전송) | `42adeb9` | 완료, 실 OBS 핸드셰이크 검증 |
| O3 | `ObsProcessManager` (수명/상태머신/창숨김/재기동) | `91c6b71` | 완료, 실기동 검증 |
| O4 | `ObsLiveBackend` (시딩/레이어/전환) | `4b9abb4` | 완료 |
| O4-fix | 브링업 결함 6건 수정 + 실송출 검증 | `747d5e7` | **완료, 실하드웨어 검증** |
| O5 | NovaStar 동기 (`transitionEnded()`↔preset) | — | **미착수**. `NovaStarController` 미구현(로드맵 Phase 6) → 인터페이스+스텁 예정 |
| O6 | qt 자동 폴백, LICENSES/NOTICE, 대형 캔버스 부하 | — | **미착수** |

---

## 4. 브링업 문제 및 해결 과정 (핵심)

실 OBS(**32.1.2 / obs-websocket 5.7.3**, 한국어 로케일, 포터블)로 테스트하며 발견·수정한 결함. 모두 `747d5e7`에 포함, 실송출 재현 검증 완료.

진단 사다리(권장): ① `UWP_OBS_PING=1`(수동 OBS에 GetVersion) → ② `engine=obs`(관리형) → ③ `UWP_OBS_NOHIDE=1`(OBS UI 직접 관찰). 로그: `build/Release/data/uWeddingPlayer.log`.

### P1. libVLC stub → 검은 화면 (OBS 무관, 환경)
- **증상**: 확장 모니터 검정. **진단**: 로그 `VlcInstance: built without libVLC (stub mode)`, `LivePlayer::play (stub)`.
- **원인**: 워크트리에 `third_party/libvlc` 없음(gitignore라 worktree에 미동반) → CMake가 무재생 stub 빌드.
- **해결**: 메인 저장소의 libVLC 트리를 워크트리로 **디렉터리 정션** → 재구성 `UWP_HAS_VLC=ON` 재빌드. (코드 변경 아님)

### P2. 미디어 경로 미해석 → OBS 소스 검정
- **증상**: 입력 생성됐는데 소스 검정. **진단**: `file=` 로그가 상대경로(`data/sample.mp4`).
- **원인**: OBS는 별도 프로세스라 작업 디렉터리가 OBS bin. 상대경로를 못 찾음. (qt 경로는 앱 dir 기준 절대화하고 있었음.)
- **해결**: `ObsLiveBackend::buildLayer`에서 `QDir(applicationDirPath()).absoluteFilePath()`로 절대화 후 전달.

### P3. 입력 이름 충돌 (`"A source already exists by that input name."`)
- **증상**: 2번째 이후 레이어 `CreateInput failed` → 씬에 누락 → 일부만 송출.
- **진단/원인**: 결정적 이름 `uwp_<scene>_<id>` 이 **OBS 포터블 config에 영속**되어 세션 간 잔존물과 충돌. 생성 직전 1건 제거(요청 순서)로도 신뢰성 부족.
- **해결(2단)**: ① 시딩 시 `GetInputList`→`uwp_*` 입력 **일괄 purge**(세션 클린 스타트). ② 입력 이름을 **세션 단조 카운터 `uwp_<n>`로 유니크화** → 충돌이 구조적으로 불가능. 세대 추적은 `m_inputsByScene`.

### P4. ffmpeg_source `hw_decode=true` → 일부 환경 검정
- **해결**: `hw_decode=false`(SW 디코드 우선)로 변경. HW 디코드 실패성 블랙 제거.

### P5. 전환 이름 로케일 의존 (`"No scene transition was found by that name."`)
- **원인**: 한국어 OBS의 내장 전환은 `"자르기"`/`"서서히 사라지기"`. 영어 `"Cut"/"Fade"` 하드코딩 매칭 실패.
- **해결**: 시딩 시 `GetSceneTransitionList`로 **`transitionKind`(`cut_transition`/`fade_transition`)로 탐색**해 표시이름 저장(`m_cutName`/`m_fadeName`). 로케일 무관.

### P6. (핵심) Program이 빈 씬으로 전환 → 검정
- **증상**: 로그 `program scene now = "UWP_PGM_B" (target "UWP_PGM_A")`. 빌드는 PGM_A에 했는데 Program은 빈 PGM_B. 소스가 program이 아니라 활성화 안 돼 preview에서도 검정(파생 증상).
- **원인**: Studio Mode `SetCurrentPreviewScene` + `TriggerStudioModeTransition` 경로가 이 환경(초기 program 상태/포터블 영속)과 어긋남.
- **해결(설계 변경)**: **Studio Mode 비활성화**, `TriggerStudioModeTransition`/`SetCurrentPreviewScene` 제거. `SetCurrentProgramScene(target)`로 **직접 전환**(활성 transition 적용). off-air 재구성이 원자성을 이미 보장하므로 Studio Mode 불필요. 이로써 P6 + “preview 검정” 동시 해소.

### 추가 안전조치
- `ObsClient` 소멸자 `m_sock->blockSignals(true)` — 셧다운 중 `disconnected→failAllPending`이 파괴 직전 백엔드 콜백을 호출하는 UAF 차단.
- `Application` 멤버 선언 순서: `m_obsBackend` → `m_obsProc` → `m_takeController` (역순 소멸로 안전 보장; 주석 명시).
- 디버그 플래그: `UWP_OBS_NOHIDE=1`(창 숨김 생략), `UWP_OBS_PING=1`(전송 PoC). 진단 로그: 입력명/경로, `program scene now =`.

---

## 5. 현재 검증 상태

- **검증됨(실하드웨어)**: `engine=obs`에서 관리형 OBS 기동 → 숨김(또는 NOHIDE) → 시딩 → 다중 레이어 영상(`ffmpeg_source`) 합성 → Cut/Fade 전환 → **확장 모니터 프로젝터 정상 송출**. `program scene == target` 일치. OBS 32.1.2 / ws 5.7.3.
- **자동 검증만(이 환경)**: 컴파일·링크. GUI 실행 불가 환경이라 런타임은 사용자 하드웨어 의존.
- **미검증/보류**: Document/PPT 송출, opacity 색필터(버전 의존, best-effort), 대형 캔버스(10368×2808) 성능, OBS 크래시 자동 재기동 실시나리오, NovaStar 동기.

---

## 6. 빌드 / 실행 / 재현

```powershell
# 구성 (Qt 5.15.2 자동탐지, libVLC는 third_party/libvlc 필요 — 없으면 qt가 stub)
cmake -B build -DUWP_AUTO_WINDEPLOYQT=ON
cmake --build build --config Release
# 산출물: build/Release/uWeddingPlayer.exe (+ Qt/libVLC 런타임 동봉)
```

- 포터블 OBS 배치: `third_party/obs/README.md` 절차(OBS 32.1.2 zip, `portable_mode.txt`, obs-websocket 포트 4455 + 비밀번호).
- `data/settings.json`: `"engine":"obs"`, `obs.ws_password`, `obs.projector_monitor`.
- 실행/디버그:
  - `$env:UWP_OBS_PING="1"` : 수동 기동 OBS에 GetVersion 1회(전송 검증).
  - `$env:UWP_OBS_NOHIDE="1"` : OBS 메인창 안 숨김(OBS UI 직접 관찰).
  - 무 플래그 : 정상 운영(OBS 숨김).
- 로그: `build/Release/data/uWeddingPlayer.log` (날짜별). **진단은 로그 우선.**
- OBS exe 경로 해석 순서: `settings.obs.exe_path` → `<appDir>/obs/bin/64bit/obs64.exe` → 컴파일 정의 `UWP_OBS_DEV_ROOT`(=소스트리 `third_party/obs`).

---

## 7. 코드 맵

```
src/live/ILiveSink.h            절단면 인터페이스
src/take/TakeController.cpp     setTransition 전달 + applyScene 호출
src/obs/ObsClient.*             obs-websocket v5 전송 (인증/상관/이벤트)
src/obs/ObsProcessManager.*     OBS 프로세스 수명/상태머신/창숨김/프로젝터/재기동
src/obs/ObsLiveBackend.*        ILiveSink 구현: seed()/doApply()/rebuildScene()/buildLayer()/triggerTransition()/onObsEvent()
src/app/Application.cpp         engine 분기 와이어링, 가드된 디버그 훅
src/app/Settings.*              engine + ObsConfig (JSON 대칭 load/save)
CMakeLists.txt                  UWP_HAS_OBS 게이팅, UWP_OBS_SOURCES, UWP_OBS_DEV_ROOT
third_party/obs/README.md       포터블 OBS 배치/버전/GPL 근거
```

---

## 8. 리뷰/디버깅 중점 포인트

1. **GPL 경계**: `src/obs/*` 및 그 의존이 libobs를 절대 끌어오지 않는지. obs-websocket 외 OBS 결합 없는지.
2. **비동기 콜백 수명**: `ObsLiveBackend`는 중첩 람다 체인(CreateInput→Transform→Index→...→transition)으로 동작. `client()`는 매 단계 재취득(널 가드)하나, **백엔드/프로세스 소멸 순서**와 `ObsClient::failAllPending` 상호작용을 정밀 검토 요망. (현재 멤버 순서 + `blockSignals`로 방어 중.)
3. **세션 간 상태**: OBS 포터블 config가 입력/씬/현재씬을 영속. 시딩 purge + 유니크 입력명이 충분한지, 크래시 재기동 후 상태 정합성.
4. **버전 의존**: opacity 필터 `color_filter_v2`, 프로젝터 `OpenVideoMixProjector`, 전환 kind 문자열 — OBS 버전 변경 시 재검증 필요(README 핀 32.1.2).
5. **전환 정확성**: `SetCurrentProgramScene`가 활성 transition으로 전환되는 타이밍 vs `SceneTransitionEnded`(→`onCommitted`/`transitionEnded()`) — O5 NovaStar 동기의 기준점이므로 경합 검토.
6. **보류 기능**: Document/PPT → 기존 `SnapshotCache`(FFmpeg 페이지 렌더)를 `image_source`로 브리지하는 설계 필요(실사용 비중 큼).
7. **에러/폴백**: OBS Ready 실패·재기동 한계 초과(`State::Failed`) 시 qt 폴백 미구현(O6). 본식 무중단 관점에서 우선순위 평가.

---

## 9. 변경 이력 (추적용)

| 커밋 | 요지 |
|---|---|
| `0f8c9ec` | O0 ILiveSink 절단면 |
| `ce404a2` | O1 인프라(Settings/CMake/README) |
| `42adeb9` | O2 ObsClient |
| `91c6b71` | O3 ObsProcessManager |
| `4b9abb4` | O4 ObsLiveBackend |
| `747d5e7` | O4-fix 6건 + 실송출 검증, README 핀 32.1.2 |
