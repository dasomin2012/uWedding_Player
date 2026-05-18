#include "SceneModel.h"

#include <QFileInfo>
#include <algorithm>

namespace uwp {

SceneModel::SceneModel(QObject* parent) : QObject(parent) {}

void SceneModel::setCanvasSize(const QSize& s) {
    if (s.isValid()) m_canvas = s;
}

int SceneModel::indexOf(const QString& id) const {
    for (int i = 0; i < m_layers.size(); ++i)
        if (m_layers[i].id == id) return i;
    return -1;
}

const Layer* SceneModel::layer(const QString& id) const {
    const int i = indexOf(id);
    return (i >= 0) ? &m_layers[i] : nullptr;
}

void SceneModel::sortByZ() {
    std::stable_sort(m_layers.begin(), m_layers.end(),
                     [](const Layer& a, const Layer& b) {
                         return a.zIndex < b.zIndex;
                     });
}

void SceneModel::normalizeZ() {
    sortByZ();
    for (int i = 0; i < m_layers.size(); ++i)
        m_layers[i].zIndex = i;
}

QString SceneModel::addLayer(const QString& mediaPath, const QRectF& geometry) {
    Layer l;
    l.id        = QString("layer_%1").arg(++m_idSeq, 3, 10, QChar('0'));
    l.media     = mediaPath;
    l.mediaType = guessMediaType(mediaPath);
    l.geometry  = geometry;
    l.zIndex    = m_layers.size();   // 맨 위
    l.opacity   = 1.0;
    l.name      = QFileInfo(mediaPath).fileName();
    m_layers.push_back(l);
    normalizeZ();
    emit layerAdded(l.id);
    select(l.id);
    return l.id;
}

void SceneModel::removeLayer(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    m_layers.removeAt(i);
    normalizeZ();
    if (m_selected == id) select(QString());
    emit layerRemoved(id);
    emit zOrderChanged();
}

void SceneModel::clear() {
    m_layers.clear();
    m_selected.clear();
    emit sceneReset();
    emit selectionChanged(QString());
}

void SceneModel::replaceAll(const QVector<Layer>& layers) {
    m_layers = layers;
    normalizeZ();
    m_selected.clear();
    // 가장 큰 시퀀스 추정 (id 충돌 방지)
    m_idSeq = m_layers.size();
    emit sceneReset();
    emit selectionChanged(QString());
}

void SceneModel::setGeometry(const QString& id, const QRectF& g) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].geometry == g) return;
    m_layers[i].geometry = g;
    emit layerChanged(id);
}

void SceneModel::setOpacity(const QString& id, double op) {
    const int i = indexOf(id);
    if (i < 0) return;
    op = qBound(0.0, op, 1.0);
    if (qFuzzyCompare(m_layers[i].opacity, op)) return;
    m_layers[i].opacity = op;
    emit layerChanged(id);
}

void SceneModel::setDisplayTime(const QString& id, int sec) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].displayTimeSec == sec) return;
    m_layers[i].displayTimeSec = qMax(0, sec);
    emit layerChanged(id);
}

void SceneModel::setEndAction(const QString& id, EndAction a) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].endAction == a) return;
    m_layers[i].endAction = a;
    emit layerChanged(id);
}

void SceneModel::setName(const QString& id, const QString& name) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].name == name) return;
    m_layers[i].name = name;
    emit layerChanged(id);
}

void SceneModel::raise(const QString& id) {
    const int i = indexOf(id);
    if (i < 0 || i + 1 >= m_layers.size()) return;
    std::swap(m_layers[i], m_layers[i + 1]);
    normalizeZ();
    emit zOrderChanged();
}

void SceneModel::lower(const QString& id) {
    const int i = indexOf(id);
    if (i <= 0) return;
    std::swap(m_layers[i], m_layers[i - 1]);
    normalizeZ();
    emit zOrderChanged();
}

void SceneModel::toFront(const QString& id) {
    const int i = indexOf(id);
    if (i < 0 || i == m_layers.size() - 1) return;
    Layer l = m_layers.takeAt(i);
    m_layers.push_back(l);
    normalizeZ();
    emit zOrderChanged();
}

void SceneModel::toBack(const QString& id) {
    const int i = indexOf(id);
    if (i <= 0) return;
    Layer l = m_layers.takeAt(i);
    m_layers.push_front(l);
    normalizeZ();
    emit zOrderChanged();
}

void SceneModel::select(const QString& id) {
    if (m_selected == id) return;
    if (!id.isEmpty() && indexOf(id) < 0) return;
    m_selected = id;
    emit selectionChanged(m_selected);
}

} // namespace uwp
