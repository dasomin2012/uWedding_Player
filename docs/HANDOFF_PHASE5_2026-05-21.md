# uWeddingPlayer — Claude Code 핸드오프 (Phase 5: Program 시스템)

> **작성:** 2026-05-21 · **PM:** Cowork 세션
> **워크트리:** `.claude/worktrees/amazing-yalow-5efda4`
> **브랜치:** `claude/amazing-yalow-5efda4`
> **선행:** O6-A 완료(`f79cd97`+R1) · O5 핸드오프 별도(`HANDOFF_O5_2026-05-21.md`)
> **참조:** `CLAUDE.md` Phase 5, "settings.json 스키마", "Related Projects(uControl)"

---

## 0. 1분 요약

CMS_WebServer의 "page" 개념과 동등한 **Program** 시스템을 신설한다. 하나의 Program은 N개 레이어(영상/이미지) + 표시 시간 + 종료 동작(다음/반복)을 갖는다. 운용자는 Preview/Edit에서 편집 → "추가" 로 Program 저장 → 하단 리스트에서 선택/재생. 박스 = **썸네일 + 이름**, **동적 리스트(스크롤)**, **추가/삭제 가능**.

본 트랙은 Phase 5a/5b/5c 세 단계로 쪼개 커밋한다. 각 단계는 독립적으로 빌드/검증 가능.

---

## 1. PM 확정 설계 결정

| # | 결정 | 비고 |
|---|---|---|
| D1 | **동적 리스트(스크롤 가능)** — 8칸 placeholder 그리드는 `ProgramListWidget`(QListWidget IconMode)로 교체 | uControl 8칸 그리드보다 사용자가 명시한 "추가/삭제"에 자연스러움. CLAUDE.md "preset 8칸 그리드" 문구는 본 변경으로 갱신 대상. |
| D2 | **클릭=선택(Preview 로드), 더블클릭/메뉴=Play(Take)** | 본식 중 실수로 다른 프로그램 송출 방지. 안전성 우선. |
| D3 | **저장 파일 분리**: `data/programs.json` (settings.json과 별도) | 프로그램 수가 늘면 settings.json이 비대해짐. 마이그레이션: 기존 `settings.json.programs`가 비어있어 호환 우려 없음. |
| D4 | **썸네일 = PreviewCanvas 렌더 결과 PNG**. 저장 경로 `data/programs/thumbs/<id>.png` | 첫 비디오 레이어 SnapshotCache 활용 안 함 — 다중 레이어 합성 결과가 의미있는 썸네일. |
| D5 | **Program 레벨 `displayTimeSec` + `endAction` 신설** (`Next`/`Loop`/`Stop`/`Hold`). Layer 레벨 동일 필드는 유지(개별 미디어 동작용) | 사용자 메시지의 "일정 시간 후 다음/반복"은 program 단위가 자연스러움. Layer의 `EndAction::Next`는 이미 enum에 존재(`Layer.h:9`) — 그대로 재활용. |
| D6 | **O5 NovaStar 연동 마이그레이션**: 재생 중인 Program의 `novastar_preset_id`가 우선, 없으면 settings 디폴트 폴백 | `Application::currentNovaPresetId()` 룩업만 교체. NovaStarController 인터페이스 무변경. |

---

## 2. 데이터 모델

### 2.1 `Program` (신규 `src/program/Program.h`)

```cpp
#pragma once

#include <QString>
#include <QVector>

#include "scene/Layer.h"   // EndAction, Layer 재사용

namespace uwp {

struct Program {
    QString          id;                 // "prog_<8자리hex>" 자동 발번
    QString          name;               // 운용자 표시
    QString          thumbnailRelPath;   // data 디렉터리 기준 상대경로 (예: "programs/thumbs/prog_xxx.png")
    QString          novastarPresetId;   // 빈 문자열이면 settings.novastar.default_preset_id 사용
    int              displayTimeSec = 0;            // 0 = 수동 진행
    EndAction        endAction      = EndAction::Hold;  // 자동 진행 기본: 정지(안전)
    QVector<Layer>   layers;             // SceneModel 스냅샷의 값 복사
};

} // namespace uwp
```

> **주의:** `Program`은 순수 데이터 레코드. `QObject` 상속 금지(`std::vector<Program>` 등에서 복사/이동 자유 보장).

### 2.2 `ProgramRepository` (신규 `src/program/ProgramRepository.{h,cpp}`)

```cpp
class ProgramRepository : public QObject {
    Q_OBJECT
public:
    explicit ProgramRepository(QObject* parent = nullptr);

    // 파일 I/O — 경로는 Application 이 주입(data/programs.json)
    bool load(const QString& path);
    bool save(const QString& path) const;

    // CRUD
    const QVector<Program>& programs() const { return m_programs; }
    int                     indexOf(const QString& id) const;
    const Program*          find(const QString& id) const;
    void                    add(const Program& p);          // 끝에 추가
    void                    update(const Program& p);       // id 기준 교체
    void                    remove(const QString& id);
    void                    reorder(int from, int to);      // 향후 드래그앤드롭용

    // 자동 진행 — 현재 program의 다음 id (Next), 또는 자신(Loop), 또는 빈 문자열(Stop/Hold)
    QString nextIdAfter(const QString& currentId, EndAction action) const;

signals:
    void programAdded(const QString& id);
    void programRemoved(const QString& id);
    void programUpdated(const QString& id);
    void programsReloaded();

private:
    QVector<Program> m_programs;
};
```

### 2.3 JSON 스키마 (`data/programs.json`)

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
      "layers": [ /* Layer 직렬화 — SceneSerializer 로직 재사용 */ ]
    }
  ]
}
```

> **Layer 직렬화 재사용**: `SceneSerializer`에 `static QJsonObject layerToJson(const Layer&)` / `static Layer layerFromJson(const QJsonObject&)`를 public으로 노출. 기존 `saveScene/loadScene` 내부에서도 이 헬퍼를 호출하도록 리팩터.

---

## 3. UI — `ProgramListWidget`

### 3.1 위젯 구성

`src/program/ProgramListWidget.{h,cpp}`:

- `QWidget` 컨테이너 + 상단 작은 툴바 `[+ Add (현재 씬을 Program 으로 저장)]` + 그 아래 `QListWidget`(IconMode, FlowLeftToRight, ResizeAdjust, scroll horizontal).
- 각 아이템: 160×90 썸네일 아이콘 + 이름 텍스트(아래쪽).
- 아이템 데이터: `item->setData(Qt::UserRole, programId)`.
- 컨텍스트 메뉴(우클릭): `Play`, `Rename`, `Edit thumbnail`, `Delete`. (Edit thumbnail은 본 트랙 비-목표 — 후속 메뉴 자리만 추가.)
- 더블클릭 = Play.
- 단일클릭 = 선택(`programSelected(id)` emit).

### 3.2 시그널

```cpp
signals:
    void addRequested();                 // [+ Add] 클릭
    void programSelected(const QString& id);   // 단일 클릭
    void playRequested(const QString& id);     // 더블클릭 또는 컨텍스트 Play
    void renameRequested(const QString& id, const QString& newName);
    void deleteRequested(const QString& id);
```

### 3.3 ControlWindow 통합

**제거할 부분** (`src/windows/ControlWindow.cpp:143-153`):

```cpp
// ----- 하단: Program List (placeholder) -----
auto* programWidget = new QWidget;
programWidget->setMinimumHeight(120);
auto* programLayout = new QGridLayout(programWidget);
programLayout->setContentsMargins(0, 4, 0, 0);
for (int i = 0; i < 8; ++i) {
    m_programButtons[i] = new QPushButton(QString("Program #%1").arg(i + 1));
    m_programButtons[i]->setMinimumHeight(56);
    m_programButtons[i]->setEnabled(false);  // Phase 5 에서 활성화
    programLayout->addWidget(m_programButtons[i], i / 4, i % 4);
}
```

**대체**:

```cpp
// ----- 하단: Program List (Phase 5) -----
m_programList = new ProgramListWidget;
m_programList->setMinimumHeight(140);
```

그리고 `centerCol->addWidget(programWidget);` → `centerCol->addWidget(m_programList);`.

`ControlWindow.h`:
- `QPushButton* m_programButtons[8] {};` 제거
- `ProgramListWidget* m_programList = nullptr;` 추가
- forward declaration: `class ProgramListWidget;`
- getter: `ProgramListWidget* programList() { return m_programList; }` — Application이 시그널 연결 시 접근.

---

## 4. 작업 분할 (3개 sub-track)

### Phase 5a — 데이터 모델 + 저장 + UI 표시 (재생 없음)

**범위**: 데이터 모델, ProgramRepository, JSON I/O, ProgramListWidget UI, ControlWindow 통합, 빈 리스트로도 정상 빌드/실행.

**수용 기준:**
1. 신규 디렉터리 `src/program/`에 `Program.h`, `ProgramRepository.{h,cpp}`, `ProgramListWidget.{h,cpp}` 추가.
2. `SceneSerializer`에 `layerToJson` / `layerFromJson` public 헬퍼 노출. 기존 `saveScene`/`loadScene` 동작 회귀 없음.
3. `Application::initialize()`에서 `ProgramRepository` 생성 + `load(programsPath)` 호출. `data/programs.json` 부재 시 빈 리스트로 시작(에러 아님).
4. `Application::resolveProgramsPath()` helper 추가 (`<appDir>/data/programs.json`).
5. `ControlWindow`의 8칸 placeholder가 `ProgramListWidget`로 교체. 빈 리스트라 위젯은 비어 보임. 빌드 통과.
6. `data/programs.json`을 수동 편집해 1개 program 넣고 실행하면 ProgramListWidget에 아이템 1개가 표시(썸네일은 누락 시 기본 회색 박스).
7. `CMakeLists.txt`에 신규 `src/program/*.{h,cpp}` 4개 추가(`UWP_SOURCES`).
8. 빌드 무경고/무에러.

**변경 파일**:
- 신규: `src/program/Program.h`, `ProgramRepository.{h,cpp}`, `ProgramListWidget.{h,cpp}`
- 변경: `src/scene/SceneSerializer.{h,cpp}` (헬퍼 노출), `src/windows/ControlWindow.{h,cpp}`, `src/app/Application.{h,cpp}`, `CMakeLists.txt`

**커밋 메시지**: `Phase 5a: Program data model + repository + list widget (UI only)`

---

### Phase 5b — 추가/삭제/선택/재생 + 썸네일 생성

**범위**: Add(현재 씬 저장), Delete, Select(Preview 로드), Play(Take), 썸네일 PNG 렌더링.

**수용 기준:**
1. `[+ Add]` 클릭 → 현재 SceneModel의 layers를 값 복사로 신규 Program에 담아 ProgramRepository에 추가. 이름은 `"Program N"` 기본. 즉시 `data/programs.json` 저장.
2. 추가 시 PreviewCanvas의 QGraphicsScene을 캔버스 비율(예: 16:9 또는 캔버스 종횡비)로 렌더링한 QPixmap을 `data/programs/thumbs/<id>.png`에 저장. ProgramListWidget 아이콘 갱신.
   - 구현: `PreviewCanvas` (또는 외부 헬퍼)에 `QPixmap renderThumbnail(QSize size) const` 추가. 내부에서 `scene()->render(painter)`.
   - 디렉터리 자동 생성 (`QDir().mkpath(thumbsDir)`).
3. 컨텍스트 메뉴 **Delete** → 확인 다이얼로그 → ProgramRepository에서 제거 + 썸네일 파일 삭제 + `programs.json` 저장 + 리스트 갱신.
4. 컨텍스트 메뉴 **Rename** → `QInputDialog`로 이름 받기 → update + 저장 + 리스트 갱신.
5. 단일 클릭 → `programSelected(id)` 시그널 → Application이 `SceneModel::replaceAll(program.layers)` 호출. 상태바: `"Loaded: <name>"`. **Live는 무영향**(편집 모드만).
6. 더블클릭 또는 컨텍스트 **Play** → Application이 `replaceAll` + `TakeController::take()` 호출. 상태바: `"Playing: <name>"`. **현재 재생 중인 programId를 Application 멤버 `m_currentProgramId`에 저장**(5c에서 사용).
7. SceneModel이 직접 편집된 뒤 다시 [+ Add]를 누르면 새 program 추가. **현재 program 갱신은 별도 메뉴(이번 트랙 비-목표)**.
8. 빌드 무경고/무에러.

**변경 파일**:
- 변경: `src/editor/PreviewCanvas.{h,cpp}` (renderThumbnail 추가), `src/program/ProgramRepository.{h,cpp}`, `src/program/ProgramListWidget.{h,cpp}`, `src/app/Application.{h,cpp}`, `src/windows/ControlWindow.{h,cpp}` (programList() getter)

**커밋 메시지**: `Phase 5b: program add/delete/select/play + thumbnail rendering`

---

### Phase 5c — 자동 진행 + NovaStar 프로그램 인식

**범위**: Program의 `displayTimeSec`/`endAction`에 따른 자동 진행 타이머, O5 NovaStar의 program 단위 preset 매핑.

**수용 기준:**
1. Application에 `QTimer m_programAdvanceTimer` 추가. Play 직후 `displayTimeSec > 0` 이면 타이머 시작.
2. 타이머 timeout → 현재 `endAction`에 따라:
   - `Next`: `ProgramRepository::nextIdAfter(currentId, Next)` → 결과가 비어있지 않으면 그 program을 Play(`replaceAll` + `take`). 마지막 program이면 처음으로 wrap(또는 stop — D 결정: **next의 마지막은 stop**).
   - `Loop`: 같은 program 다시 Play.
   - `Stop`: Live 클리어(qt 백엔드는 `applyScene({})`, OBS 백엔드는 동일 절단면). 상태바 `"Program stopped"`.
   - `Hold`: 마지막 프레임/상태 유지(타이머 정지만, Live 무변경).
3. 사용자가 도중에 다른 Program을 Play 또는 TAKE 수동 실행 시 → 기존 타이머 즉시 stop + 새로 Play된 program 기준으로 타이머 재시작.
4. NovaStar 폴백: `Application::currentNovaPresetId()` 룩업 순서 변경 — 환경변수 `UWP_NOVASTAR_PRESET` > **현재 program의 `novastarPresetId`** > `settings.novastar.default_preset_id` > 빈 문자열.
5. ProgramListWidget에서 현재 재생 중 program 아이템에 시각적 표시(테두리 강조). `Application::currentProgramChanged(id)` 시그널 또는 ProgramListWidget의 `setActiveProgram(id)` 슬롯.
6. 빌드 무경고/무에러.

**변경 파일**:
- 변경: `src/app/Application.{h,cpp}`, `src/program/ProgramListWidget.{h,cpp}` (setActiveProgram), `src/live/ILiveSink.h` 무변경 (clear는 `applyScene({})`로 표현 가능)

**커밋 메시지**: `Phase 5c: program auto-advance timer + NovaStar program-aware preset`

---

## 5. 작업 순서 (권장)

1. **5a 먼저, 완전히 끝낸 뒤 5b/5c** — 5a가 데이터 모델 토대라 후속 트랙이 흔들리면 비용이 크다.
2. 각 sub-track 끝나면 빌드 통과 + 1회 사용자 측 런타임 스모크 테스트.
3. 푸시는 sub-track 단위 또는 5c까지 끝낸 뒤 일괄 — PM에 보고 후 결정.

---

## 6. 리스크 / 결정 필요 사항

1. **R1: PreviewCanvas 썸네일 렌더 시 라이브 영상 처리** — PreviewCanvas는 SnapshotCache의 정지화상으로만 그리므로(아키텍처 원칙) `scene()->render()`로 그대로 PNG 만들면 됨. 단 SnapshotCache가 아직 채워지지 않은 새 비디오 레이어는 회색 박스로 나옴. → 허용. 운용자가 영상이 캐시될 때까지 잠시 기다린 뒤 [+ Add] 누르면 정상 썸네일.
2. **R2: 동일 이름 program 다수** — 자동 발번된 id가 다르면 이름 충돌 허용. uControl 동작과 일치.
3. **R3: programs.json 손상 시** — 파싱 실패 시 빈 리스트로 시작 + 백업 파일 `data/programs.json.bak` 생성(load 직전 백업). 손상 보고는 상태바 + qWarning.
4. **R4: ControlWindow.h의 `m_programButtons[8]` 멤버 변수** — 5a에서 제거하면서 같은 헤더에 `m_programList`로 대체. include 추가 필요.
5. **R5: 본식 중 자동 진행이 의도치 않게 시작될 위험** — Play 누를 때만 타이머 시작. 단일 클릭(Select)은 타이머 시작 금지(편집 모드). 본식 안전성 D2 정책과 일치.
6. **R6: end_action="next"의 끝 도달 처리** — 마지막 program의 next는 "wrap to first" vs "stop". 본 핸드오프는 **stop**으로 결정(본식 종료 후 무한 루프 방지). 사용자가 첫 program부터 다시 루프하고 싶다면 마지막 program의 `endAction`을 `Loop`(자기 자신 반복) 또는 첫 program으로의 명시적 next 링크가 필요 — 그건 후속 UI 작업.

---

## 7. CLAUDE.md 갱신 필요 사항 (별도 커밋)

본 트랙 완료 후 CLAUDE.md 다음 항목 정정 (사용자/협업자 검토 후 머지):

- "Related Projects > uControl > preset 8칸 그리드" → "uControl의 프리셋 개념 계승(uWP는 동적 리스트로 진화)".
- "Directory Structure" 의 `src/program/` 섹션을 실제 파일 목록과 일치시킴.
- "settings.json 스키마" 의 `programs: []` 부분을 별도 `data/programs.json` 으로 분리 표기 + 스키마 명시.
- "Development Phases" Phase 5 체크박스.
- "Phase 6 NovaStar" 항목에 "Phase 5에서 program 단위 preset 매핑 선행됨" 메모.

---

## 8. GPL 및 코딩 규칙

- 본 트랙은 OBS와 무관. `src/program/*`는 Qt 표준만 사용. libobs 무접촉 유지.
- CLAUDE.md "Coding Conventions" 준수: `m_` 멤버 접두사, PascalCase 클래스, `#pragma once`, Qt new connect syntax.
- 로그: `qDebug`/`qWarning`/`qCritical` 사용.
- 한국어 주석 OK, public API는 영어 권장.

---

## 9. 인계 체크리스트

- [ ] §1 PM 결정 6개(D1–D6) 이해
- [ ] §2 데이터 모델 시그니처 파악
- [ ] §3 UI 위젯 구성 및 시그널 파악
- [ ] §4 sub-track 3개의 수용 기준 분리 인지
- [ ] §6 리스크 6개 — 특히 R5/R6은 본식 안전성 직결
- [ ] §7 CLAUDE.md 갱신은 본 트랙 완료 후 별도 커밋(범위 분리)
- [ ] O5 핸드오프(`HANDOFF_O5_2026-05-21.md`)와의 통합 지점(D6 → §4 Phase 5c 4번) 확인

**핸드오프 종료. Claude Code는 §4 Phase 5a부터 착수.**
