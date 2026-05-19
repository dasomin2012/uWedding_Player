#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QHash>
#include <functional>

class QWebSocket;

namespace uwp {

// obs-websocket v5 클라이언트 — 순수 전송 계층.
//
// QtWebSockets(LGPL) + Qt Core 만 사용한다. libobs 를 링크하지 않고
// 별도 프로세스의 OBS 를 네트워크 프로토콜로만 제어 → GPL 격리 유지
// (자세한 근거는 third_party/obs/README.md).
//
// 책임: 연결 / Hello·Identify 핸드셰이크(+SHA256 인증) / requestId 상관
// / RequestResponse 콜백 / Event 패스스루. 재연결·프로세스 수명은
// 상위(O3 ObsProcessManager)의 몫이며 여기서는 다루지 않는다.
class ObsClient : public QObject {
    Q_OBJECT
public:
    explicit ObsClient(QObject* parent = nullptr);
    ~ObsClient() override;

    // url 예: "ws://127.0.0.1:4455". password 가 비어도 서버가 인증을
    // 요구하면 Hello 의 challenge/salt 로 계산해 응답한다.
    void connectToObs(const QString& url, const QString& password);
    void disconnectFromObs();

    bool isReady() const { return m_ready; }   // Identified 완료 여부

    // ok = requestStatus.result, data = responseData, comment = 실패 사유.
    using ResponseHandler = std::function<void(bool ok,
                                               const QJsonObject& data,
                                               const QString& comment)>;

    // 미연결/미준비/소켓오류 시에도 핸들러는 ok=false 로 반드시 1회 호출
    // (호출자가 영구 대기에 빠지지 않도록 보장).
    void request(const QString& requestType,
                 const QJsonObject& requestData,
                 ResponseHandler onResponse);

signals:
    void ready();                                          // Identified
    void closed();
    void socketError(const QString& message);
    void obsEvent(const QString& eventType, const QJsonObject& eventData);

private slots:
    void onConnected();
    void onTextMessage(const QString& message);
    void onDisconnected();

private:
    void sendOp(int op, const QJsonObject& d);
    void handleHello(const QJsonObject& d);
    void handleRequestResponse(const QJsonObject& d);
    void failAllPending(const QString& reason);

    static QString computeAuth(const QString& password,
                               const QString& salt,
                               const QString& challenge);

    QWebSocket*                     m_sock     = nullptr;
    QString                         m_password;
    bool                            m_ready    = false;
    quint64                         m_reqSeq   = 0;
    QHash<QString, ResponseHandler> m_pending;   // requestId → handler
};

} // namespace uwp
