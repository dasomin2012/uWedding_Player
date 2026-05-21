#include "NovaStarController.h"

#include <QDebug>

namespace uwp {

NovaStarController::NovaStarController(const NovaStarConfig& cfg,
                                       QObject* parent)
    : QObject(parent), m_cfg(cfg) {}

void NovaStarController::callPreset(const QString& presetId) {
    if (presetId.isEmpty()) {
        // 매핑 미설정 — 조용히 무시(Phase 5 ProgramRepository 도입 전 정상 상태).
        return;
    }
    if (!m_cfg.enabled) {
        qDebug() << "NovaStar: disabled — skipping preset" << presetId;
        return;
    }
    // O5: no-op 스텁. 후속 트랙에서 TCP/UDP 전송 구현(QTcpSocket/QUdpSocket).
    qInfo() << "NovaStar: callPreset(stub) presetId=" << presetId
            << "→ host=" << m_cfg.host << ":" << m_cfg.port
            << "protocol=" << m_cfg.protocol;
    emit presetCalled(presetId);
}

} // namespace uwp
