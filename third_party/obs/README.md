# third_party/obs — 포터블 OBS 런타임 배치

`engine=obs` 송출 백엔드가 사용하는 **수정 없는 업스트림 OBS Studio**를 여기에 둔다.
이 디렉터리 내용물은 대용량이라 `.gitignore` 로 제외되고, **이 README 만 추적**된다.
배포/개발 PC 마다 아래 절차로 직접 배치한다.

## GPL 경계 (중요)

- 우리 exe 는 **libobs 를 링크하지 않는다.** OBS 는 별도 프로세스로 실행되고,
  제어는 오직 obs-websocket(localhost WebSocket + JSON)으로만 한다.
- 따라서 우리 코드는 OBS(GPLv2)의 파생 저작물이 아니다 → 비공개 유지 가능.
- **여기 OBS 트리는 절대 수정하지 말 것.** 수정 없는 업스트림이어야 GPL 의무가
  "출처 안내" 한 줄로 갈음된다. OBS 플러그인/스크립트도 추가 금지(=GPL 영역).

## 고정 버전

| 항목 | 고정 값 |
|---|---|
| OBS Studio | **32.1.2 (64-bit, Windows)** — 실송출 검증 완료(O2~O4). OBS ≥ 28 이면 호환 |
| obs-websocket | **5.7.3** (OBS 내장, 별도 설치 불필요) |
| 프로토콜 | obs-websocket 5.x (협상 RPC 버전 1) |

버전을 올릴 때는 `ObsClient`(O2) 핸드셰이크와 창 숨김 로직(O3)을 재검증한 뒤
이 표를 갱신한다. 버전 불일치는 무음 실패의 주원인.

## 배치 절차

1. OBS Studio 32.1.2 Windows zip(또는 설치본)을 받아 이 디렉터리에 푼다.
   최종적으로 다음이 존재해야 한다:

   ```
   third_party/obs/
   ├── bin/64bit/obs64.exe
   ├── data/
   ├── obs-plugins/64bit/         # obs-websocket.dll 포함
   └── portable_mode.txt          # 빈 파일 — 포터블 모드 강제 (아래)
   ```

2. **포터블 모드 강제**: `third_party/obs/portable_mode.txt` 빈 파일을 만든다.
   이러면 OBS 가 운용자 개인 `%APPDATA%\obs-studio` 를 건드리지 않고
   `third_party/obs/config/` 안에 격리된 설정을 쓴다(재현성·무간섭).

3. **obs-websocket 설정** (최초 1회, OBS UI 에서):
   - Tools → WebSocket Server Settings
   - Enable WebSocket server ✔
   - Server Port: `4455`
   - Authentication: 비밀번호 설정 권장(로컬이라도)
   - 설정 후 OBS 를 한 번 정상 종료하면 `config/` 에 저장됨.

4. **settings.json 매핑** (`data/settings.json` 의 `obs` 블록):

   ```json
   "obs": {
     "exe_path": "",                 // 비우면 third_party/obs/bin/64bit/obs64.exe 자동 해석(O3)
     "ws_url": "ws://127.0.0.1:4455",
     "ws_password": "<위에서 설정한 비밀번호>",
     "scene_a": "UWP_PGM_A",
     "scene_b": "UWP_PGM_B",
     "projector_monitor": 1
   }
   ```
   그리고 `"engine": "obs"` 로 전환(기본은 `"qt"` — 동작 무변경).

## 빌드와의 관계

- CMake 는 `Qt5::WebSockets` 존재 시 `UWP_HAS_OBS=1` 로 빌드(이 트리 없어도 빌드 성공).
- 이 OBS 트리는 **빌드에 복사되지 않는다.** 런타임에 `ObsProcessManager`(O3)가
  `Settings.obs().exePath`(없으면 이 경로)를 해석해 프로세스를 띄운다.
- `Qt5::WebSockets` 모듈이 없으면 `engine=obs` 는 비활성, `engine=qt` 만 동작.
