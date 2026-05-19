#include "ObsClient.h"

#include <QtWebSockets/QWebSocket>
#include <QJsonDocument>
#include <QJsonValue>
#include <QCryptographicHash>
#include <QDebug>

namespace uwp {

namespace {
// obs-websocket v5 opcodes
constexpr int kOpHello           = 0;   // server → client
constexpr int kOpIdentify        = 1;   // client → server
constexpr int kOpIdentified      = 2;   // server → client
constexpr int kOpEvent           = 5;   // server → client
constexpr int kOpRequest         = 6;   // client → server
constexpr int kOpRequestResponse = 7;   // server → client

constexpr int kRpcVersion = 1;

// EventSubscription::All (고볼륨 제외) = General..Ui 비트 OR = 2047.
// PoC/이후 단계의 Transition 등 이벤트가 보이도록 넓게 구독.
constexpr int kEventSubAll = 2047;
} // namespace

ObsClient::ObsClient(QObject* parent) : QObject(parent) {}

ObsClient::~ObsClient() {
    if (m_sock) {
        // 소멸 중 disconnected→failAllPending 으로 파괴 직전 객체의
        // 콜백이 호출되는 UAF 를 차단.
        m_sock->blockSignals(true);
        m_sock->abort();
    }
}

void ObsClient::connectToObs(const QString& url, const QString& password) {
    m_password = password;
    m_ready    = false;

    if (!m_sock) {
        m_sock = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(m_sock, &QWebSocket::connected,
                this, &ObsClient::onConnected);
        connect(m_sock, &QWebSocket::textMessageReceived,
                this, &ObsClient::onTextMessage);
        connect(m_sock, &QWebSocket::disconnected,
                this, &ObsClient::onDisconnected);
        // Qt 5.15.2: errorOccurred 없음 → 게터와 이름 겹치는 error 시그널을
        // QOverload 로 디스앰비규에이션.
        connect(m_sock,
                QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                this, [this](QAbstractSocket::SocketError) {
                    emit socketError(m_sock ? m_sock->errorString()
                                            : QStringLiteral("unknown"));
                });
    }
    qInfo() << "ObsClient: connecting to" << url;
    m_sock->open(QUrl(url));
}

void ObsClient::disconnectFromObs() {
    if (m_sock) m_sock->close();
}

void ObsClient::onConnected() {
    qInfo() << "ObsClient: socket connected — awaiting Hello";
    // Identify 는 Hello 수신 후 전송 (challenge/salt 필요).
}

void ObsClient::onTextMessage(const QString& message) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ObsClient: bad message —" << err.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    const int op = root.value(QStringLiteral("op")).toInt(-1);
    const QJsonObject d = root.value(QStringLiteral("d")).toObject();

    switch (op) {
    case kOpHello:
        handleHello(d);
        break;
    case kOpIdentified:
        m_ready = true;
        qInfo() << "ObsClient: Identified (negotiatedRpcVersion="
                << d.value(QStringLiteral("negotiatedRpcVersion")).toInt() << ")";
        emit ready();
        break;
    case kOpEvent:
        emit obsEvent(d.value(QStringLiteral("eventType")).toString(),
                      d.value(QStringLiteral("eventData")).toObject());
        break;
    case kOpRequestResponse:
        handleRequestResponse(d);
        break;
    default:
        // RequestBatchResponse 등 미사용 op 는 무시.
        break;
    }
}

void ObsClient::handleHello(const QJsonObject& d) {
    QJsonObject identify;
    identify[QStringLiteral("rpcVersion")]        = kRpcVersion;
    identify[QStringLiteral("eventSubscriptions")] = kEventSubAll;

    // 서버가 인증을 요구하면 Hello.d.authentication{ challenge, salt } 존재.
    const QJsonObject auth =
        d.value(QStringLiteral("authentication")).toObject();
    if (!auth.isEmpty()) {
        const QString challenge =
            auth.value(QStringLiteral("challenge")).toString();
        const QString salt = auth.value(QStringLiteral("salt")).toString();
        identify[QStringLiteral("authentication")] =
            computeAuth(m_password, salt, challenge);
    }
    sendOp(kOpIdentify, identify);
}

void ObsClient::handleRequestResponse(const QJsonObject& d) {
    const QString reqId = d.value(QStringLiteral("requestId")).toString();
    auto it = m_pending.find(reqId);
    if (it == m_pending.end()) return;

    const ResponseHandler handler = it.value();
    m_pending.erase(it);

    const QJsonObject status =
        d.value(QStringLiteral("requestStatus")).toObject();
    const bool ok = status.value(QStringLiteral("result")).toBool(false);
    const QString comment =
        status.value(QStringLiteral("comment")).toString();
    const QJsonObject data =
        d.value(QStringLiteral("responseData")).toObject();

    if (handler) handler(ok, data, comment);
}

void ObsClient::request(const QString& requestType,
                        const QJsonObject& requestData,
                        ResponseHandler onResponse) {
    if (!m_sock || !m_ready) {
        if (onResponse)
            onResponse(false, {}, QStringLiteral("OBS not connected/ready"));
        return;
    }
    const QString reqId = QString::number(++m_reqSeq);
    if (onResponse) m_pending.insert(reqId, std::move(onResponse));

    QJsonObject d;
    d[QStringLiteral("requestType")] = requestType;
    d[QStringLiteral("requestId")]   = reqId;
    d[QStringLiteral("requestData")] = requestData;
    sendOp(kOpRequest, d);
}

void ObsClient::sendOp(int op, const QJsonObject& d) {
    if (!m_sock) return;
    QJsonObject root;
    root[QStringLiteral("op")] = op;
    root[QStringLiteral("d")]  = d;
    m_sock->sendTextMessage(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void ObsClient::onDisconnected() {
    m_ready = false;
    failAllPending(QStringLiteral("OBS disconnected"));
    qInfo() << "ObsClient: disconnected";
    emit closed();
}

void ObsClient::failAllPending(const QString& reason) {
    const auto pending = m_pending;
    m_pending.clear();
    for (const ResponseHandler& h : pending)
        if (h) h(false, {}, reason);
}

QString ObsClient::computeAuth(const QString& password,
                               const QString& salt,
                               const QString& challenge) {
    // obs-websocket v5:
    //   secret = base64( sha256( password + salt ) )
    //   auth   = base64( sha256( secret + challenge ) )
    const QByteArray secret =
        QCryptographicHash::hash(password.toUtf8() + salt.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toBase64();
    const QByteArray authentication =
        QCryptographicHash::hash(secret + challenge.toUtf8(),
                                 QCryptographicHash::Sha256)
            .toBase64();
    return QString::fromLatin1(authentication);
}

} // namespace uwp
