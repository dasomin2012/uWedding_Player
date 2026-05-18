\# uWeddingPlayer



웨딩홀 미디어 플레이어. 운용자가 Control 모니터에서 편집하고,

확장 모니터(LED 스크린)로 영상/이미지/문서를 송출하는 단일 PC 시스템.



\## Stack

\- Qt 5.15.2 (Widgets, GraphicsView)

\- C++17

\- MSVC 2019 64bit

\- CMake 3.21+

\- libVLC 3.0.x (Live 영상 재생)

\- FFmpeg CLI (Preview 스냅샷 추출)

\- QtPdf 5.15 또는 Poppler-Qt5 (PDF 렌더링)

\- NovaStar H 시리즈 (LED 컨트롤러, TCP/UDP 프로토콜)



\## Architecture Principles



\### 단일 프로세스, 듀얼 윈도우

\- ControlWindow: 운용자 메인 화면 (일반 윈도우)

\- LiveWindow: 확장 모니터 송출 (frameless, fullscreen, custom 해상도)

\- 두 윈도우는 같은 QApplication 안에서 동작, IPC 불필요



\### Preview는 정지화상, Live만 실제 재생

\- Preview Canvas는 SnapshotCache에서 정지화상 가져와 QGraphicsScene으로 렌더링

\- 영상 디코딩은 Live 측 LivePlayerPool에서만 수행

\- 영상은 첫 프레임 또는 1초 지점 스냅샷으로 Preview 표시

\- 결과: Preview 편집이 Live 송출에 물리적으로 간섭 불가



\### 자유 레이어 구조

\- 하나의 Program(프리셋)은 N개 Layer를 가짐

\- 각 Layer: media path + geometry + z\_index + opacity + display\_time + end\_action

\- uControl의 자유 레이어 편집 방식을 그대로 계승



\### Custom 해상도 송출

\- Live 윈도우는 논리 캔버스 크기(예: 10368×2808)로 그림

\- 실제 모니터 해상도와 무관 (NovaStar가 LED로 분배)

\- 설정에서 출력 모니터 인덱스 선택 가능



\### Take 전환

\- Cut 또는 Fade 모드

\- TakeController가 Preview SceneModel을 Live로 commit

\- NovaStar 프리셋 호출과 동기 실행 (presetId가 있으면)



\## Directory Structure
uWeddingPlayer/

├── CLAUDE.md

├── README.md

├── CMakeLists.txt

├── .gitignore

├── src/

│   ├── main.cpp

│   ├── app/

│   │   ├── Application.{h,cpp}

│   │   └── Settings.{h,cpp}

│   ├── windows/

│   │   ├── ControlWindow.{h,cpp}

│   │   └── LiveWindow.{h,cpp}

│   ├── scene/

│   │   ├── SceneModel.{h,cpp}

│   │   ├── Layer.{h,cpp}

│   │   └── SceneSerializer.{h,cpp}

│   ├── player/

│   │   ├── IMediaWidget.{h,cpp}

│   │   ├── VideoWidget.{h,cpp}

│   │   ├── ImageWidget.{h,cpp}

│   │   ├── DocumentWidget.{h,cpp}

│   │   ├── LivePlayerPool.{h,cpp}

│   │   ├── SnapshotCache.{h,cpp}

│   │   └── MediaFactory.{h,cpp}

│   ├── editor/

│   │   ├── PreviewCanvas.{h,cpp}

│   │   ├── LayerItem.{h,cpp}

│   │   ├── PropertyPanel.{h,cpp}

│   │   └── MediaListWidget.{h,cpp}

│   ├── take/

│   │   ├── TakeController.{h,cpp}

│   │   └── TransitionEffect.{h,cpp}

│   ├── program/

│   │   ├── Program.{h,cpp}

│   │   ├── ProgramRepository.{h,cpp}

│   │   └── ProgramListWidget.{h,cpp}

│   ├── novastar/

│   │   ├── NovaStarController.{h,cpp}

│   │   └── NovaStarPreset.{h,cpp}

│   └── monitoring/

│       └── LiveMirrorWidget.{h,cpp}

├── resources/

│   ├── icons/

│   └── ui/

├── third\_party/

│   └── libvlc/                 # SDK, gitignore

├── data/

│   ├── settings.json

│   └── programs/

└── tests/

## Coding Conventions

\- C++17 표준

\- Qt new connect syntax 사용 (포인터-멤버 방식)

\- 메모리: parent-child ownership 우선, 필요 시 std::unique\_ptr

\- 헤더: `#pragma once`

\- 네임스페이스: `uwp::` (선택적, 필요 시)

\- 클래스명: PascalCase

\- 함수/변수: camelCase

\- 멤버 변수: m\_ 접두사

\- 상수: kSnakeCase 또는 ALL\_CAPS

\- 로그: qDebug/qWarning/qCritical, 파일 출력은 일자별 로테이션

\- 주석: 한국어 OK, public API는 영어 권장



\## Key Design Decisions (확정)



| 항목 | 결정 |

|------|------|

| Preview/Live 재생 분리 | Preview=정지화상, Live=실제 영상 |

| 레이어 구조 | 자유 레이어 (N개 동시 배치) |

| 송출 환경 | Custom 해상도 + NovaStar 연동 |

| Take 모드 | Cut + Fade 둘 다 |

| NovaStar 제어 | Program에 preset\_id 매핑 |

| 지원 미디어 | mp4 등 영상, 이미지, PPT/PDF |

| PPT 처리 | 사전 PDF 변환 권장 (LibreOffice 의존성 회피) |

| 스냅샷 추출 | FFmpeg CLI |



\## setup.json (programs.json) 스키마



```json

{

&#x20; "version": "1.0.0",

&#x20; "settings": {

&#x20;   "canvas": { "width": 10368, "height": 2808 },

&#x20;   "output\_monitor\_index": 1,

&#x20;   "output\_render\_mode": "fit",

&#x20;   "take\_default\_mode": "fade",

&#x20;   "take\_fade\_duration\_ms": 800,

&#x20;   "novastar": {

&#x20;     "enabled": false,

&#x20;     "host": "192.168.1.100",

&#x20;     "port": 5200,

&#x20;     "protocol": "udp"

&#x20;   }

&#x20; },

&#x20; "programs": \[

&#x20;   {

&#x20;     "id": "prog\_001",

&#x20;     "slot": 0,

&#x20;     "name": "신랑 입장",

&#x20;     "thumbnail": "thumbs/prog\_001.png",

&#x20;     "novastar\_preset\_id": "P1",

&#x20;     "layers": \[

&#x20;       {

&#x20;         "id": "layer\_001",

&#x20;         "media": "media/groom\_intro.mp4",

&#x20;         "media\_type": "video",

&#x20;         "geometry": { "x": 0, "y": 0, "w": 10368, "h": 2808 },

&#x20;         "z\_index": 0,

&#x20;         "opacity": 1.0,

&#x20;         "display\_time\_sec": 0,

&#x20;         "end\_action": "loop"

&#x20;       }

&#x20;     ]

&#x20;   }

&#x20; ]

}

```



\## Development Phases



\- \[ ] Phase 1: 듀얼 윈도우 + Custom 해상도 + 단일 비디오 Live 재생

\- \[ ] Phase 2: LivePlayerPool + SnapshotCache

\- \[ ] Phase 3: PreviewCanvas 자유 레이어 편집

\- \[ ] Phase 4: TakeController + Cut/Fade

\- \[ ] Phase 5: Program List + 프리셋 저장/불러오기

\- \[ ] Phase 6: NovaStarController + Program 연동

\- \[ ] Phase 7: Live Monitoring + 설정 화면

\- \[ ] Phase 8: 안정성 테스트 + PPT/PDF 최적화



\## Related Projects (재활용 자산)

\- uControl: 자유 레이어 편집 UI, preset 8칸 그리드, setup.json 파서

\- uPlayer\_win: libVLC 통합, 미디어 재생 로직

\- CMS\_WebServer\_MultiTenant: setup.json 데이터 모델 참고 (네트워크 부분은 제거)



\## Out of Scope (이 프로젝트에서 안 함)

\- 웹서버 / 클라이언트-서버 통신

\- 멀티테넌트

\- 원격 관리 / 폴링

\- 모바일 앱

