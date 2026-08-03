#include "ProgramRepository.h"

#include "scene/Layer.h"
#include "scene/SceneSerializer.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QDebug>

namespace uwp {

ProgramRepository::ProgramRepository(QObject* parent) : QObject(parent) {}

int ProgramRepository::indexOf(const QString& id) const {
    for (int i = 0; i < m_programs.size(); ++i)
        if (m_programs[i].id == id) return i;
    return -1;
}

const Program* ProgramRepository::find(const QString& id) const {
    const int i = indexOf(id);
    return (i >= 0) ? &m_programs[i] : nullptr;
}

QString ProgramRepository::makeUniqueId() const {
    for (;;) {
        const quint32 r = QRandomGenerator::global()->generate();
        const QString id = QString("prog_%1").arg(r, 8, 16, QChar('0'));
        if (indexOf(id) < 0) return id;
    }
}

// Phase A: 새 Page 에 부여할 UUID 8-hex. 프로그램 스코프에서 고유하면 충분 —
// 구조체 유효성이 프로그램 내부라 전역 유일성 대신 확률적 유일성으로 충분.
QString ProgramRepository::makePageId() {
    const quint32 r = QRandomGenerator::global()->generate();
    return QString("page_%1").arg(r, 8, 16, QChar('0'));
}

void ProgramRepository::add(const Program& p) {
    m_programs.push_back(p);
    emit programAdded(p.id);
}

void ProgramRepository::update(const Program& p) {
    const int i = indexOf(p.id);
    if (i < 0) return;
    m_programs[i] = p;
    emit programUpdated(p.id);
}

void ProgramRepository::remove(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    m_programs.removeAt(i);
    emit programRemoved(id);
}

void ProgramRepository::reorder(int from, int to) {
    if (from < 0 || from >= m_programs.size()) return;
    if (to   < 0 || to   >= m_programs.size()) return;
    if (from == to) return;
    m_programs.move(from, to);
    emit programsReloaded();
}

QString ProgramRepository::nextIdAfter(const QString& currentId,
                                       EndAction action) const {
    switch (action) {
    case EndAction::Loop:
        return currentId;                       // 자기 자신 반복
    case EndAction::Next: {
        const int i = indexOf(currentId);
        if (i < 0 || i + 1 >= m_programs.size())
            return QString();                   // 마지막 → stop (R6)
        return m_programs[i + 1].id;
    }
    case EndAction::Stop:
    case EndAction::Hold:
    default:
        return QString();
    }
}

// ---- JSON I/O ------------------------------------------------
bool ProgramRepository::load(const QString& path) {
    m_programs.clear();

    QFile f(path);
    if (!f.exists()) {                          // 부재는 정상(빈 리스트)
        emit programsReloaded();
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "ProgramRepository::load: cannot open" << path;
        emit programsReloaded();
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        // R3: 손상 시 백업 후 빈 리스트로 시작
        const QString bak = path + ".bak";
        QFile::remove(bak);
        QFile::copy(path, bak);
        qWarning() << "ProgramRepository::load: parse error -" << err.errorString()
                   << "→ backed up to" << bak;
        emit programsReloaded();
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonArray  arr  = root.value("programs").toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Program p;
        p.id               = o.value("id").toString();
        p.name             = o.value("name").toString();
        p.thumbnailRelPath = o.value("thumbnail").toString();
        p.novastarPresetId = o.value("novastar_preset_id").toString();
        p.endAction        = endActionFromString(o.value("end_action").toString());
        // BGM (신 필드, 없으면 기본값 유지)
        p.bgmPath   = o.value("bgm_path").toString();
        if (o.contains("bgm_volume"))
            p.bgmVolume = qBound(0, o.value("bgm_volume").toInt(80), 100);
        if (o.contains("bgm_loop"))
            p.bgmLoop = o.value("bgm_loop").toBool(true);
        if (p.id.isEmpty()) continue;

        // UI-D Phase A: pages 배열 파싱. 구 포맷(program 레벨 layers +
        // display_time_sec) 자동 마이그레이션 → 단일 페이지로 감싸기.
        p.pages.clear();   // 기본 invariant 페이지 제거 (아래에서 최소 1개 보장)
        if (o.contains("pages") && o.value("pages").isArray()) {
            const QJsonArray pagesArr = o.value("pages").toArray();
            for (const QJsonValue& pv : pagesArr) {
                const QJsonObject po = pv.toObject();
                Page pg;
                pg.id               = po.value("id").toString();
                if (pg.id.isEmpty()) pg.id = makePageId();
                pg.name             = po.value("name").toString();
                pg.thumbnailRelPath = po.value("thumbnail").toString();
                pg.displayTimeSec   = po.value("display_time_sec").toInt(0);
                pg.layers           = SceneSerializer::layersFromJson(
                                          po.value("layers").toArray());
                p.pages.push_back(pg);
            }
        }
        if (p.pages.isEmpty()) {
            // 구 포맷 마이그레이션 또는 완전 빈 프로그램 → 단일 페이지.
            Page pg;
            pg.id             = makePageId();
            pg.displayTimeSec = o.value("display_time_sec").toInt(0);
            pg.layers         = SceneSerializer::layersFromJson(
                                    o.value("layers").toArray());
            // 프로그램 대표 썸네일을 초기 페이지에도 공유(마이그레이션 편의).
            pg.thumbnailRelPath = p.thumbnailRelPath;
            p.pages.push_back(pg);
        }
        m_programs.push_back(p);
    }
    qInfo() << "ProgramRepository: loaded" << m_programs.size()
            << "program(s) <-" << path;
    emit programsReloaded();
    return true;
}

bool ProgramRepository::save(const QString& path) const {
    QJsonArray arr;
    for (const Program& p : m_programs) {
        QJsonObject o;
        o["id"]                 = p.id;
        o["name"]               = p.name;
        o["thumbnail"]          = p.thumbnailRelPath;
        o["novastar_preset_id"] = p.novastarPresetId;
        o["end_action"]         = endActionToString(p.endAction);
        o["bgm_path"]           = p.bgmPath;
        o["bgm_volume"]         = p.bgmVolume;
        o["bgm_loop"]           = p.bgmLoop;

        // UI-D Phase A: pages 배열로 저장. 구 포맷 layers/display_time_sec 은
        // 저장하지 않음 — 다음 load 부터는 순수 신 포맷.
        QJsonArray pagesArr;
        for (const Page& pg : p.pages) {
            QJsonObject po;
            po["id"]               = pg.id;
            po["name"]             = pg.name;
            po["thumbnail"]        = pg.thumbnailRelPath;
            po["display_time_sec"] = pg.displayTimeSec;
            po["layers"]           = SceneSerializer::layersToJson(pg.layers);
            pagesArr.append(po);
        }
        o["pages"] = pagesArr;
        arr.append(o);
    }
    QJsonObject root;
    root["version"]  = "1.0.0";
    root["programs"] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ProgramRepository::save: cannot write" << path;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    qInfo() << "ProgramRepository: saved" << m_programs.size()
            << "program(s) ->" << path;
    return true;
}

} // namespace uwp
