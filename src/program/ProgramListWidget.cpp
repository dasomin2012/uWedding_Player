#include "ProgramListWidget.h"

#include "Program.h"

#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace uwp {

static const QSize kThumbSize(160, 90);

static QPixmap makeDefaultThumb() {
    QPixmap pm(kThumbSize);
    pm.fill(QColor(48, 48, 54));
    QPainter p(&pm);
    p.setPen(QColor(90, 90, 100));
    p.drawRect(0, 0, pm.width() - 1, pm.height() - 1);
    p.setPen(QColor(130, 130, 140));
    p.drawText(pm.rect(), Qt::AlignCenter, "(no thumb)");
    p.end();
    return pm;
}

ProgramListWidget::ProgramListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* title  = new QLabel("Programs");
    title->setStyleSheet("font-weight: bold;");
    auto* btnAdd = new QPushButton("+ Add");
    btnAdd->setToolTip("현재 씬을 Program 으로 저장");
    connect(btnAdd, &QPushButton::clicked, this, &ProgramListWidget::addRequested);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(title);
    top->addStretch();
    top->addWidget(btnAdd);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(false);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setIconSize(kThumbSize);
    m_list->setSpacing(6);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->addLayout(top);
    lay->addWidget(m_list);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (it) emit programSelected(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it) emit playRequested(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &ProgramListWidget::showContextMenu);
}

QString ProgramListWidget::currentId() const {
    auto* it = m_list->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

void ProgramListWidget::setPrograms(const QVector<Program>& programs,
                                    const QString& dataDir) {
    m_dataDir = dataDir;
    m_list->clear();

    const QPixmap fallback = makeDefaultThumb();
    for (const Program& p : programs) {
        QPixmap thumb;
        if (!p.thumbnailRelPath.isEmpty()) {
            const QString abs = QDir(dataDir).filePath(p.thumbnailRelPath);
            if (QFileInfo::exists(abs)) thumb.load(abs);
        }
        if (thumb.isNull()) thumb = fallback;

        auto* item = new QListWidgetItem(
            QIcon(thumb), p.name.isEmpty() ? p.id : p.name, m_list);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(p.name);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
    setActiveProgram(m_activeId);   // 강조 유지
}

void ProgramListWidget::setActiveProgram(const QString& id) {
    m_activeId = id;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const bool active = (it->data(Qt::UserRole).toString() == id && !id.isEmpty());
        QFont f = it->font();
        f.setBold(active);
        it->setFont(f);
        it->setForeground(active ? QColor(120, 200, 255) : QColor(220, 220, 220));
    }
}

void ProgramListWidget::showContextMenu(const QPoint& pos) {
    QListWidgetItem* it = m_list->itemAt(pos);
    if (!it) return;
    const QString id   = it->data(Qt::UserRole).toString();
    const QString name = it->text();

    QMenu menu(this);
    QAction* playAct   = menu.addAction("Play");
    QAction* renameAct = menu.addAction("Rename…");
    QAction* thumbAct  = menu.addAction("Edit thumbnail…");
    thumbAct->setEnabled(false);   // 후속 트랙 — 자리만
    menu.addSeparator();
    QAction* delAct    = menu.addAction("Delete");

    QAction* chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == playAct) {
        emit playRequested(id);
    } else if (chosen == renameAct) {
        bool ok = false;
        const QString nn = QInputDialog::getText(
            this, "Rename Program", "Name:", QLineEdit::Normal, name, &ok);
        if (ok && !nn.isEmpty()) emit renameRequested(id, nn);
    } else if (chosen == delAct) {
        emit deleteRequested(id);
    }
}

} // namespace uwp
