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
    // 벡터 순서 = z 순서의 단일 진실 소스. 그저 0..n-1 로 재인덱스한다.
    //  과거 구현은 여기서 sortByZ() 를 먼저 불렀는데, 그러면 raise/lower/
    //  toFront/toBack 가 방금 재배치한 벡터를 이전 zIndex 로 되돌려버려
    //  버튼이 시각적으로 아무 것도 하지 않는 버그가 있었다.
    //  로드 경로(replaceAll)는 자체적으로 sortByZ() 를 명시 호출한다.
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

QString SceneModel::addTextLayer(const QString& initialText,
                                 const QRectF& geometry) {
    Layer l;
    l.id        = QString("layer_%1").arg(++m_idSeq, 3, 10, QChar('0'));
    l.mediaType = MediaType::Text;
    l.media.clear();                  // 텍스트는 파일 없음
    // 기본 배치: 캔버스 하단(자막 영역). 사용자 지정 있으면 그걸 우선.
    if (geometry.isValid() && geometry.width() > 0 && geometry.height() > 0) {
        l.geometry = geometry;
    } else {
        const int w = m_canvas.width();
        const int h = m_canvas.height();
        // 레이어 세로 = 텍스트 크기의 약 250% (padding 이 실제 여백처럼 보이도록).
        //   fontSize 는 Layer 기본값(48) 사용. 사용자가 fontSize 를 크게 바꾸면
        //   레이어를 함께 리사이즈해야 하지만, 초기 자막 UX 로는 이 비율이 적정.
        const int boxH = qMax(60, qRound(l.fontSize * 2.5));
        l.geometry = QRectF(0, h - boxH - h / 20, w, boxH);
    }
    l.zIndex    = m_layers.size();   // 맨 위
    l.opacity   = 1.0;
    l.name      = tr("텍스트 %1").arg(m_layers.size() + 1);
    l.text      = initialText.isEmpty() ? tr("텍스트를 입력하세요") : initialText;
    m_layers.push_back(l);
    normalizeZ();
    emit layerAdded(l.id);
    select(l.id);
    return l.id;
}

void SceneModel::removeLayer(const QString& idIn) {
    // 방어적 로컬 복사 필수.
    //  호출자가 참조 세만틱스로 자신의 QString 필드를 넘기는 경우(예:
    //  PropertyPanel::m_id) 아래 select("") 발화가 그 필드를 "" 로 재대입하면
    //  참조 파라미터 idIn 도 함께 "" 로 무효화된다. 이 상태로 emit
    //  layerRemoved(idIn) 가 나가면 뷰의 onLayerRemoved 가 빈 id 로 no-op —
    //  레이어가 캔버스에 영구히 남는 버그.
    const QString id = idIn;
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
    // 로드된 데이터는 임의의 순서/zIndex 를 가질 수 있으므로 명시적으로
    // zIndex 로 정렬 후 재인덱스. (normalizeZ 는 sort 안 함 — 아래 z-order
    // 편집이 벡터 순서를 진실 소스로 다루기 위함.)
    sortByZ();
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

// ---- Text 위젯 setter --------------------------------------------
//  값 비교 후 실제 변경일 때만 emit — PropertyPanel 이 다중 필드를 갱신할 때
//  onLayerChanged 콜백이 자기 자신을 loadFrom 으로 되돌리는 루프를 최소화.
void SceneModel::setText(const QString& id, const QString& text) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].text == text) return;
    m_layers[i].text = text;
    emit layerChanged(id);
}

void SceneModel::setTextColor(const QString& id, const QString& color) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].textColor == color) return;
    m_layers[i].textColor = color;
    emit layerChanged(id);
}

void SceneModel::setFontSize(const QString& id, int px) {
    const int i = indexOf(id);
    if (i < 0) return;
    px = qMax(1, px);
    if (m_layers[i].fontSize == px) return;
    m_layers[i].fontSize = px;
    emit layerChanged(id);
}

void SceneModel::setFontFamily(const QString& id, const QString& family) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].fontFamily == family) return;
    m_layers[i].fontFamily = family;
    emit layerChanged(id);
}

void SceneModel::setFontWeight(const QString& id, int weight) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].fontWeight == weight) return;
    m_layers[i].fontWeight = weight;
    emit layerChanged(id);
}

void SceneModel::setTextAlign(const QString& id, int align) {
    const int i = indexOf(id);
    if (i < 0) return;
    align = qBound(0, align, 2);
    if (m_layers[i].textAlign == align) return;
    m_layers[i].textAlign = align;
    emit layerChanged(id);
}

void SceneModel::setTextVAlign(const QString& id, int align) {
    const int i = indexOf(id);
    if (i < 0) return;
    align = qBound(0, align, 2);
    if (m_layers[i].textVAlign == align) return;
    m_layers[i].textVAlign = align;
    emit layerChanged(id);
}

void SceneModel::setBgColor(const QString& id, const QString& color) {
    const int i = indexOf(id);
    if (i < 0 || m_layers[i].bgColor == color) return;
    m_layers[i].bgColor = color;
    emit layerChanged(id);
}

void SceneModel::setBgOpacity(const QString& id, double op) {
    const int i = indexOf(id);
    if (i < 0) return;
    op = qBound(0.0, op, 1.0);
    if (qFuzzyCompare(m_layers[i].bgOpacity, op)) return;
    m_layers[i].bgOpacity = op;
    emit layerChanged(id);
}

void SceneModel::setPadding(const QString& id, int px) {
    const int i = indexOf(id);
    if (i < 0) return;
    px = qMax(0, px);
    if (m_layers[i].padding == px) return;
    m_layers[i].padding = px;
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
