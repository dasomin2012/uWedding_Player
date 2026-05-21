#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>
#include <QString>

#include "Layer.h"

namespace uwp {

class SceneModel;

// SceneModel <-> JSON. 레이어 표현은 CLAUDE.md setup.json 스키마와 호환
// (Phase 5 의 Program 저장/불러오기가 동일 포맷 재사용).
class SceneSerializer {
public:
    // 단건 레이어 직렬화 헬퍼 (Phase 5 ProgramRepository 가 재사용).
    // layerFromJson 은 항상 Layer 를 반환(검증은 호출측 — id/media 비면 무효).
    static QJsonObject layerToJson(const Layer& l);
    static Layer       layerFromJson(const QJsonObject& o);

    static QJsonArray     layersToJson(const QVector<Layer>& layers);
    static QVector<Layer> layersFromJson(const QJsonArray& arr);

    // 스크래치 단일 씬 파일: { version, canvas:{w,h}, layers:[...] }
    static bool saveScene(const SceneModel& model, const QString& path);
    static bool loadScene(SceneModel& model, const QString& path);
};

} // namespace uwp
