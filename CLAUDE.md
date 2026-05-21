# uWeddingPlayer

웨딩홀 미디어 플레이어. 운용자가 Control 모니터에서 편집하고, 확장 모니터(LED 스크린)로 영상·이미지·문서를 송출하는 단일 PC 시스템.

> 본 워크트리(`.claude/worktrees/amazing-yalow-5efda4`)는 메인 트랙 위에 **OBS 송출 백엔드 통합(O0–O6) 트랙**을 얹어 진행 중. 자세한 OBS 통합 내용은 `docs/OBS_INTEGRATION_REPORT.md` 참조.

## Stack

- Qt 5.15.2 (Widgets, GraphicsView, **WebSockets** — OBS 백엔드 빌드 시)
- C++17, MSVC 2019 64bit (MSVC 14.4x+ 대응 `src/compat/msvc_stdext_shim.h` 강제 포함)
- CMake 3.21+
- libVLC 3.0.x (Live 영상 재생, SDK 미탑재 시 VideoWidget이 stub으로 빌드)
- FFmpeg CLI (Preview 스냅샷 추출, 미탑재 시 영상은 graceful 실패)
- QtPdf 5.15 또는 Poppler-Qt5 (PDF 렌더링 — 후속 단계에서 통합 예정)
- NovaStar H 시리즈 (LED 컨트롤러, TCP/UDP 프로토콜 — 미구현)
- **OBS Studio 32.1.2 + obs-websocket 5.7.3** (선택 백엔드, 별도 프로세스, libobs 무링크)

## Architecture Principles

### 단일 프로세스, 듀얼 윈도우

- ControlWindow: 운용자 메인 화면 (일반 윈도우)
- LiveWindow: 확장 모니터 송출 (frameless, fullscreen, custom 해상도)
- 두 윈도우는 같은 QApplication 안에서 동작, IPC 불필요

### Preview는 정지화상, Live만 실제 재생

- Preview Canvas는 SnapshotCache에서 정지화상 가져와 QGraphicsScene으로 렌더링
- 영상 디코딩은 Live 측 LivePlayerPool에서만 수행
- 영상은 1초 지점(없으면 첫 프레임) 스냅샷으로 Preview 표시
- 결과: Preview 편집이 Live 송출에 물리적으로 간섭 불가

### 자유 레이어 구조

- 하나의 Program(프리셋)은 N개 Layer를 가짐
- 각 Layer: `media` + `geometry` + `z_index` + `opacity` + `display_time_sec` + `end_action`
- uControl의 자유 레이어 편집 방식을 그대로 계승

### Custom 해상도 송출

- Live 윈도우는 논리 캔버스 크기(예: 10368×2808)로 그림
- 실제 모니터 해상도와 무관 (NovaStar가 LED로 분배)
- 설정에서 출력 모니터 인덱스 선택 가능

### Take 전환

- Cut 또는 Fade 모드
- TakeController가 Preview SceneModel을 값 복사 스냅샷으로 만들어 Live로 commit (이후 편집과 분리)
- NovaStar 프리셋 호출과 동기 실행 (presetId가 있으면 — Phase 6 / OBS 트랙 O5)

### 송출 백엔드 추상 (`ILiveSink`)

- `TakeController`는 백엔드를 직접 알지 못한다. `src/live/ILiveSink.h`(비-QObject 순수 추상) 인터페이스 뒤로 분리.
- 두 구현이 존재: `LiveWindow`(`engine=qt`, libVLC, 위젯), `ObsLiveBackend`(`engine=obs`, obs-websocket, 비위젯).
- 위젯 백엔드만 `transitionAnchor()`로 자기 자신을 반환해 Qt dip-to-black 전환을 받음. OBS 백엔드는 `nullptr` 반환 → 전환은 OBS 컴포지터가 자체 수행.

### GPL 격리 (1번 설계 제약)

- 본 제품은 웨딩홀 납품 **상용·비공개 소스**.
- OBS / libobs는 GPLv2. libobs를 우리 exe에 링크하면 GPL 전염되어 전체 소스 공개 의무 → 상용 치명적.
- 따라서 OBS는 **별도 프로세스**로 실행하고, 제어는 **obs-websocket(네트워크 프로토콜)으로만** 한다.
- 우리 exe는 `Qt5::WebSockets`(LGPL)만 링크. libobs 헤더/심볼/DLL 일체 무접촉.
- **OBS 플러그인/Lua/Python 스크립트 작성 금지** — 작성 즉시 GPL 영역으로 끌려들어감. NovaStar 글루 등 자사 IP는 전부 우리 exe 측에 둔다.
- 자세한 근거와 배치 절차는 `third_party/obs/README.md`.

### OBS 송출 모델 (engine=obs)

- 두 씬 `UWP_PGM_A` / `UWP_PGM_B`를 ping-pong. **off-air 씬을 재구성**(현재 program이 아닌 씬) → 한 번에 program 전환 → 송출 중 깜빡임 없음.
- **Studio Mode는 비활성**. `SetCurrentProgramScene(target)`로 직접 전환하며, 활성 scene transition(Cut/Fade)이 그 위에 적용된다. (초기 설계의 `TriggerStudioModeTransition` 경로는 P6 결함으로 폐기.)
- 캔버스 = 우리 논리 캔버스(`SetVideoSettings`). 레이어 geometry는 OBS 캔버스와 1:1(`OBS_BOUNDS_STRETCH`). z = `SetSceneItemIndex`.
- 입력 이름은 세션 단조 카운터로 유니크화(`uwp_<n>`) + 시딩 시 잔존 `uwp_*` 일괄 purge → 영속 config 충돌 구조적 방지.
- 미디어: 영상=`ffmpeg_source`(`hw_decode=false`), 이미지=`image_source`. **Document/PPT는 현재 경고 후 skip** — 후속 단계에서 `SnapshotCache`(FFmpeg 페이지 렌더) → `image_source` 브리지 예정.

## Directory Structure

```
uWeddingPlayer/
├── CLAUDE.md
├── CMakeLists.txt
├── .gitignore
├── docs/
│   ├── OBS_INTEGRATION_REPORT.md     # OBS 트랙 자기완결식 온보딩 문서
│   └── STATE_ASSESSMENT_2026-05-19.md
├── src/
│   ├── main.cpp
│   ├── compat/
│   │   └── msvc_stdext_shim.h        # MSVC 14.4x+ stdext 제거 우회 (force-include)
│   ├── app/
│   │   ├── Application.{h,cpp}       # 코디네이터, engine 분기 와이어링
│   │   └── Settings.{h,cpp}          # data/settings.json I/O
│   ├── windows/
│   │   ├── ControlWindow.{h,cpp}     # 운용자 메인 화면 (편집 UI)
│   │   └── LiveWindow.{h,cpp}        # 확장 모니터 송출 + ILiveSink 구현(engine=qt)
│   ├── scene/
│   │   ├── Layer.{h,cpp}             # 데이터 레코드 + 타입 변환
│   │   ├── SceneModel.{h,cpp}        # 편집중 씬의 단일 진실 소스
│   │   └── SceneSerializer.{h,cpp}   # SceneModel <-> JSON
│   ├── player/
│   │   ├── VlcInstance.{h,cpp}       # libvlc_instance_t 싱글톤
│   │   ├── LivePlayerPool.{h,cpp}    # LivePlayer 풀
│   │   ├── VideoWidget.{h,cpp}       # libVLC 네이티브 HWND 호스트
│   │   ├── ImageWidget.{h,cpp}       # Live 측 이미지 레이어
│   │   └── SnapshotCache.{h,cpp}     # Preview 정지화상 추출/캐시
│   ├── editor/
│   │   ├── PreviewCanvas.{h,cpp}     # QGraphicsView 기반 자유 레이어 편집
│   │   ├── LayerItem.{h,cpp}         # 한 Layer 의 시각 표현 (8핸들 리사이즈)
│   │   ├── MediaListWidget.{h,cpp}   # 미디어 소스 패널
│   │   └── PropertyPanel.{h,cpp}     # 선택 Layer 속성 양방향 편집
│   ├── live/
│   │   └── ILiveSink.h               # 송출 백엔드 절단면 (qt/obs 공통)
│   ├── take/
│   │   ├── TakeController.{h,cpp}    # Preview → Live commit 주체
│   │   └── TransitionEffect.{h,cpp}  # Cut / Fade(dip-to-black)
│   ├── novastar/
│   │   └── NovaStarController.{h,cpp} # NovaStar LED preset 호출 (O5 스텁; 실 TCP/UDP 는 Phase 6)
│   ├── program/                      # Phase 5 — Program(프리셋) 시스템
│   │   ├── Program.h                 # 데이터 레코드 (layers + displayTimeSec + endAction)
│   │   ├── ProgramRepository.{h,cpp} # data/programs.json I/O + CRUD + nextIdAfter(자동진행)
│   │   └── ProgramListWidget.{h,cpp} # 하단 동적 리스트 (썸네일 + 추가/삭제/Play)
│   └── obs/                          # engine=obs 전용 (UWP_HAS_OBS=ON 일 때만 빌드)
│       ├── ObsClient.{h,cpp}         # obs-websocket v5 순수 전송 (QtWebSockets)
│       ├── ObsProcessManager.{h,cpp} # 포터블 OBS 수명/창숨김/프로젝터/재기동
│       └── ObsLiveBackend.{h,cpp}    # ILiveSink 구현 — 씬 핑퐁 + 전환
├── third_party/
│   ├── libvlc/                       # libVLC SDK + 런타임 (gitignore, .lib/.dll/plugins)
│   └── obs/                          # 포터블 OBS 런타임 (내용물 gitignore, README만 추적)
│       └── README.md                 # 배치 절차 + GPL 근거 + 고정 버전
└── data/                             # 런타임 데이터 (build/Release/data 에 자동 생성)
    ├── settings.json                 # 앱 설정
    ├── scene.json                    # 스크래치 씬 (자동 저장)
    ├── programs.json                 # Program 목록 (Phase 5, settings.json 과 분리)
    ├── programs/thumbs/              # Program 썸네일 PNG (Phase 5)
    ├── cache/                        # SnapshotCache PNG 캐시
    └── sample.mp4                    # 테스트 영상 (gitignore)
```

## Coding Conventions

- C++17 표준
- Qt new connect syntax (포인터-멤버 방식)
- 메모리: parent-child ownership 우선, 필요 시 `std::unique_ptr`
- 헤더: `#pragma once`
- 네임스페이스: `uwp::`
- 클래스명: `PascalCase` · 함수/변수: `camelCase` · 멤버 변수: `m_` 접두사 · 상수: `kSnakeCase` 또는 `ALL_CAPS`
- 로그: `qDebug` / `qWarning` / `qCritical`, 파일 출력은 일자별 로테이션 (`build/Release/data/uWeddingPlayer.log`)
- 주석: 한국어 OK, public API는 영어 권장
- MSVC 빌드 시 모든 번역 단위 최상단에 `src/compat/msvc_stdext_shim.h` 자동 force-include (CMake `/FI`)

## Key Design Decisions (확정)

| 항목 | 결정 |
|------|------|
| Preview/Live 재생 분리 | Preview = 정지화상(`SnapshotCache`), Live = 실제 재생 |
| 레이어 구조 | 자유 레이어 (N개 동시 배치, zIndex 정렬) |
| 송출 환경 | Custom 해상도 + NovaStar 연동 (NovaStar 컨트롤러는 미구현) |
| Take 모드 | Cut + Fade 둘 다 (qt = dip-to-black, obs = 활성 scene transition) |
| NovaStar 제어 | Program 에 `preset_id` 매핑 — 트리거는 OBS 트랙 O5 또는 Phase 6 |
| 송출 백엔드 선택 | `Settings.engine = qt | obs` — 런타임 분기, 동작 변경 없이 폴백 가능 (qt 자동 폴백은 O6 미구현) |
| OBS 통합 방식 | **별도 프로세스 + obs-websocket only** (libobs 무링크 → GPL 격리 유지) |
| OBS 전환 모델 | Studio Mode 비활성, off-air 씬 재구성 + `SetCurrentProgramScene` 직접 전환 |
| OBS 입력 이름 | 세션 단조 카운터 `uwp_<n>` + 시딩 purge — 영속 config 충돌 구조적 방지 |
| OBS 전환 이름 매칭 | `transitionKind`(`cut_transition` / `fade_transition`)로 탐색 → 로케일 무관 |
| 지원 미디어 (현재) | 영상(`ffmpeg_source`/libVLC), 이미지(`image_source`/QImage). Document/PPT 보류 |
| PPT 처리 (계획) | 사전 PDF 변환 + `SnapshotCache` → `image_source` 브리지 (LibreOffice 의존 회피) |
| 스냅샷 추출 | FFmpeg CLI, `QProcess` 비동기 |

## settings.json 스키마

`data/settings.json` (실행파일 옆 `data/`). 누락 키는 기본값으로 채움. 저장 시 정렬 출력.

```json
{
  "version": "1.0.0",
  "settings": {
    "canvas": { "width": 10368, "height": 2808 },
    "output_monitor_index": 1,
    "output_render_mode": "fit",
    "take_default_mode": "fade",
    "take_fade_duration_ms": 800,
    "test_video_path": "data/sample.mp4",
    "ffmpeg_path": "",
    "snapshot_cache_dir": "data/cache",
    "scene_scratch": "data/scene.json",
    "media_dir": "",
    "novastar": {
      "enabled": false,
      "host": "192.168.1.100",
      "port": 5200,
      "protocol": "udp"
    },
    "engine": "qt",
    "obs": {
      "exe_path": "",
      "ws_url": "ws://127.0.0.1:4455",
      "ws_password": "",
      "scene_a": "UWP_PGM_A",
      "scene_b": "UWP_PGM_B",
      "projector_monitor": 1
    }
  }
}
```

> Phase 5부터 Program 목록은 settings.json 이 아니라 **별도 `data/programs.json`** 에 저장된다(아래 스키마). settings.json 비대화 방지.

레이어 스키마(스크래치 씬 / Program 양쪽 동일):

```json
{
  "id": "layer_001",
  "media": "media/groom_intro.mp4",
  "media_type": "video",
  "geometry": { "x": 0, "y": 0, "w": 10368, "h": 2808 },
  "z_index": 0,
  "opacity": 1.0,
  "display_time_sec": 0,
  "end_action": "loop"
}
```

## data/programs.json 스키마 (Phase 5)

`data/programs.json` (settings.json 과 분리). 부재 시 빈 리스트로 시작, 손상 시 `.bak` 백업 후 빈 리스트.

```json
{
  "version": "1.0.0",
  "programs": [
    {
      "id": "prog_4a1f2b7c",
      "name": "신랑 입장",
      "thumbnail": "programs/thumbs/prog_4a1f2b7c.png",
      "novastar_preset_id": "P1",
      "display_time_sec": 30,
      "end_action": "next",
      "layers": [ ]
    }
  ]
}
```

- `thumbnail`: `data/` 기준 상대경로. PreviewCanvas 합성 결과 PNG (Add/편집 시 자동 렌더).
- `display_time_sec`: 0 = 수동 진행. >0 이면 Play 후 그 시간 뒤 자동 진행.
- `end_action`(program 단위): `next`(다음 program, 마지막은 정지) · `loop`(자기 반복) · `stop`(Live 클리어) · `hold`(유지). Layer 단위 `end_action` 과 별개.
- `novastar_preset_id`: 빈 문자열이면 `settings.novastar.default_preset_id` 폴백.
- `layers`: 레이어 스키마(위)와 동일 — `SceneSerializer` 헬퍼 재사용.

## Build & Run

### 구성

```powershell
# Qt 5.15.2 자동 탐지 (C:/Qt/5.15.2/msvc2019_64 또는 $env:Qt5_DIR)
cmake -B build -DUWP_AUTO_WINDEPLOYQT=ON
cmake --build build --config Release
# 산출물: build/Release/uWeddingPlayer.exe (+ Qt/libVLC 런타임 동봉)
```

### CMake 옵션 / 게이팅

| 옵션 / 변수 | 의미 |
|---|---|
| `UWP_HAS_VLC` | `third_party/libvlc/sdk` 가 있으면 자동 ON. OFF 시 VideoWidget은 stub |
| `UWP_HAS_OBS` | `Qt5::WebSockets` 가 있으면 자동 ON. OFF 시 `engine=obs` 비활성 |
| `UWP_OBS_DEV_ROOT` | 빌드 시 `third_party/obs` 절대경로를 컴파일 정의로 주입 (런타임 폴백) |
| `UWP_AUTO_WINDEPLOYQT` | post-build에 windeployqt 자동 실행 (기본 ON) |

### 디버그 환경 변수

| 변수 | 효과 |
|---|---|
| `UWP_AUTOTAKE=1` | 앱 시작 1.5초 후 자동 Take 1회 (검증 자동화) |
| `UWP_OBS_PING=1` | (수동 기동 OBS에) obs-websocket으로 접속해 `GetVersion` 1회 호출·로깅 |
| `UWP_OBS_NOHIDE=1` | 관리형 OBS 메인창 숨김 생략 (OBS UI 직접 관찰용) |

진단 사다리(권장 순서): ① `UWP_OBS_PING=1` (전송 PoC) → ② `engine=obs` 무 플래그 (정식) → ③ `UWP_OBS_NOHIDE=1` (UI 관찰). 로그: `build/Release/data/uWeddingPlayer.log`.

### 포터블 OBS 배치

`third_party/obs/README.md` 절차 그대로(OBS 32.1.2 zip 펼치고 `portable_mode.txt` 생성, 포트 4455 + 비밀번호 설정). exe 경로 해석 순서: `settings.obs.exe_path` → `<appDir>/obs/bin/64bit/obs64.exe` → `UWP_OBS_DEV_ROOT/bin/64bit/obs64.exe`.

## Development Phases

원래 Phase 1–8 메인 트랙. OBS 통합(O0–O6)은 본 워크트리에서 별도 트랙으로 진행.

- [x] Phase 1: 듀얼 윈도우 + Custom 해상도 + 단일 비디오 Live 재생
- [x] Phase 2: LivePlayerPool + SnapshotCache
- [x] Phase 3: PreviewCanvas 자유 레이어 편집 (LayerItem · PropertyPanel · MediaListWidget)
- [x] Phase 4: TakeController + Cut/Fade
- [x] Phase 5: Program List + 프리셋 저장/불러오기 (동적 리스트 · 편집바인딩 자동저장 · 썸네일 · 자동 진행 · program 단위 NovaStar preset)
- [ ] Phase 6: NovaStarController 실 프로토콜 (TCP/UDP) — Phase 5/O5 에서 program 단위 preset 매핑·트리거(스텁)는 선행 완료
- [ ] Phase 7: Live Monitoring(`src/monitoring/`) + 설정 화면 GUI (현재는 JSON 직접 편집)
- [ ] Phase 8: 안정성 테스트 + PPT/PDF 최적화

### OBS 통합 트랙 (본 워크트리 `claude/amazing-yalow-5efda4`)

상세 진행 이력과 브링업 결함 6건(P1–P6)의 진단·해결은 `docs/OBS_INTEGRATION_REPORT.md`.

- [x] O0: `ILiveSink` 절단면 추출 (무동작 리팩터)
- [x] O1: Settings `engine`/`obs` 필드 + CMake `Qt5::WebSockets` 게이팅 + `third_party/obs/README.md`
- [x] O2: `ObsClient` (obs-websocket v5 핸드셰이크/요청 상관/이벤트)
- [x] O3: `ObsProcessManager` (수명·상태머신·창숨김·프로젝터·재기동)
- [x] O4: `ObsLiveBackend` (시딩·레이어·전환) + O4-fix 6건 (실하드웨어 송출 검증)
- [ ] O5: NovaStar 동기 — `transitionEnded()` ↔ preset 트리거 (NovaStarController 스텁부터)
- [ ] O6: qt 자동 폴백, LICENSES/NOTICE 정비, 대형 캔버스 부하 테스트
- [ ] Document/PPT 송출 — `SnapshotCache` → `image_source` 브리지

## Related Projects (재활용 자산)

- **uControl**: 자유 레이어 편집 UI, 프리셋 개념 계승(uWP는 8칸 그리드 대신 **동적 Program 리스트**로 진화), setup.json 파서
- **uPlayer_win**: libVLC 통합, 미디어 재생 로직
- **CMS_WebServer_MultiTenant**: setup.json 데이터 모델 참고 (네트워크 부분은 제거)

## Out of Scope (이 프로젝트에서 안 함)

- 웹서버 / 클라이언트-서버 통신
- 멀티테넌트
- 원격 관리 / 폴링
- 모바일 앱
- OBS 플러그인 / Lua / Python 스크립트 작성 (GPL 영역 진입 방지)
