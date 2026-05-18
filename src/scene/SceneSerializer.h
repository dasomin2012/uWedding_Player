#pragma once

#include <QJsonArray>
#include <QVector>
#include <QString>

#include "Layer.h"

namespace uwp {

class SceneModel;

// SceneModel <-> JSON. 레이어 표현은 CLAUDE.md setup.json 스키마와 호환
// (Phase 5 의 Program 저장/불러오기가 동일 포맷 재사용).
class SceneSerializer {
public:
    static QJsonArray     layersToJson(const QVector<Layer>& layers);
    static QVector<Layer> layersFromJson(const QJsonArray& arr);

    // 스크래치 단일 씬 파일: { version, canvas:{w,h}, layers:[...] }
    static bool saveScene(const SceneModel& model, const QString& path);
    static bool loadScene(SceneModel& model, const QString& path);
};

} // namespace uwp
