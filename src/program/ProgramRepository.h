#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "Program.h"

namespace uwp {

// Program 목록의 단일 진실 소스 + data/programs.json I/O.
// 경로는 Application 이 주입(파일/디렉터리 정책은 호출측 책임).
class ProgramRepository : public QObject {
    Q_OBJECT
public:
    explicit ProgramRepository(QObject* parent = nullptr);

    // 파일 I/O. load: 파일 부재는 정상(빈 리스트, false 반환). 파싱 실패 시
    // <path>.bak 백업 후 빈 리스트로 시작.
    bool load(const QString& path);
    bool save(const QString& path) const;

    // CRUD
    const QVector<Program>& programs() const { return m_programs; }
    int                     count()    const { return m_programs.size(); }
    int                     indexOf(const QString& id) const;
    const Program*          find(const QString& id) const;
    void                    add(const Program& p);          // 끝에 추가
    void                    update(const Program& p);       // id 기준 교체
    void                    remove(const QString& id);
    void                    reorder(int from, int to);      // 향후 드래그앤드롭용

    // 중복 없는 신규 id 발번 ("prog_<8hex>")
    QString makeUniqueId() const;

    // 자동 진행 — 현재 program 다음 id.
    //  Next: 다음 program(마지막이면 "" = stop), Loop: 자기 자신, Stop/Hold: "".
    QString nextIdAfter(const QString& currentId, EndAction action) const;

signals:
    void programAdded(const QString& id);
    void programRemoved(const QString& id);
    void programUpdated(const QString& id);
    void programsReloaded();

private:
    QVector<Program> m_programs;
};

} // namespace uwp
