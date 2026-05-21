#pragma once

#include <QObject>
#include <QString>

#include "app/Settings.h"   // NovaStarConfig

namespace uwp {

// NovaStar H 시리즈 LED 컨트롤러 동기 제어.
//
// 본 단계(O5)는 인터페이스 + no-op 스텁이다. 실 TCP/UDP 프로토콜 구현은
// 후속(Phase 6 별도 트랙). OBS/Qt 어느 백엔드에서도 take 완료 시점에
// callPreset() 을 부르면 된다. NovaStar 는 우리 IP — GPL 무관.
class NovaStarController : public QObject {
    Q_OBJECT
public:
    explicit NovaStarController(const NovaStarConfig& cfg,
                                QObject* parent = nullptr);

    bool isEnabled() const { return m_cfg.enabled; }

public slots:
    // presetId 가 빈 문자열이면 no-op. 실제 LED 전환은 후속 구현.
    void callPreset(const QString& presetId);

signals:
    void presetCalled(const QString& presetId);
    void presetFailed(const QString& presetId, const QString& reason);

private:
    NovaStarConfig m_cfg;   // 값 보관 (Settings 수명과 분리)
};

} // namespace uwp
