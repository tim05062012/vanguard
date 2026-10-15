#include "ui/LibraryPage.h"

#include <QEnterEvent>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "CoverCache.h"
#include "SettingsManager.h"
#include "ui/GameCard.h"

namespace {

// Ширина карточки, от которой считаем число колонок (сетка 2–3 колонки).
constexpr int kCardMinWidth = 300;

} // namespace

LibraryPage::LibraryPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(6);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);

    titleLabel = new QLabel(tr("Библиотека"), this);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(titleLabel);
    header->addStretch(1);

    // Главный вход в фичу «добавление игр» (v1.0).
    addGameBtn = new QToolButton(this);
    addGameBtn->setObjectName(QStringLiteral("addGameBtn"));
    addGameBtn->setText(QStringLiteral("+  ") + tr("Добавить игру"));
    addGameBtn->setCursor(Qt::PointingHandCursor);
    addGameBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    header->addWidget(addGameBtn, 0, Qt::AlignTop);
    connect(addGameBtn, &QToolButton::clicked,
            this, &LibraryPage::addGameRequested);

    emptyLabel = new QLabel(this);
    emptyLabel->setObjectName(QStringLiteral("pageHint"));
    header->addWidget(emptyLabel);

    root->addLayout(header);

    hintLabel = new QLabel(
        tr("Наведите на карточку, чтобы запустить игру. Двойной клик — тот же "
           "эффект."),
        this);
    hintLabel->setObjectName(QStringLiteral("pageHint"));
    root->addWidget(hintLabel);
    root->addSpacing(14);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    container = new QWidget;
    container->setAttribute(Qt::WA_StyledBackground, true);

    grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 8, 8);
    grid->setSpacing(16);

    scrollArea->setWidget(container);
    root->addWidget(scrollArea, 1);

    setEmptyState(true);
}

void LibraryPage::setGames(const QList<GameInfo> &games)
{
    m_games = games;
    rebuildLayout();
}

void LibraryPage::refresh()
{
    rebuildLayout();
}

void LibraryPage::reflow()
{
    // Пересобираем сетку только когда реально меняется число колонок,
    // иначе перерисовка мерцает при каждом resize.
    if (optimalColumns() != m_lastColumns)
        rebuildLayout();
}

int LibraryPage::optimalColumns() const
{
    const int available = scrollArea->viewport()->width();
    if (available <= 0)
        return 2;

    const int columns = available / kCardMinWidth;
    return qBound(2, columns, 3);
}

void LibraryPage::rebuildLayout()
{
    // Полная очистка сетки: сначала освобождаем память виджетов.
    while (QLayoutItem *item = grid->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    setEmptyState(m_games.isEmpty());
    if (m_games.isEmpty()) {
        m_lastColumns = 0;
        return;
    }

    const int columns = optimalColumns();
    m_lastColumns = columns;

    for (int i = 0; i < m_games.size(); ++i) {
        auto *card = new GameCard(m_games.at(i), container);
        grid->addWidget(card, i / columns, i % columns);
        connect(card, &GameCard::playRequested,
                this, &LibraryPage::launchGameRequested);
        connect(card, &GameCard::launchRequested,
                this, &LibraryPage::launchGameRequested);
        connect(card, &GameCard::editRequested,
                this, &LibraryPage::editGameRequested);
        connect(card, &GameCard::deleteRequested,
                this, &LibraryPage::deleteGameRequested);
    }

    grid->setRowStretch(grid->rowCount(), 1);
    grid->setColumnStretch(columns - 1, 1);

    // Уже скачанные обложки показываем сразу (кеш на диске).
    for (int i = 0; i < grid->count(); ++i) {
        auto *card = qobject_cast<GameCard *>(grid->itemAt(i)->widget());
        if (!card)
            continue;
        const QString path = CoverCache::instance().coverPathFor(card->game());
        if (!path.isEmpty())
            card->setCover(path);
    }
}

void LibraryPage::setCover(const QString &key, const QString &path)
{
    if (key.isEmpty() || !grid)
        return;

    for (int i = 0; i < grid->count(); ++i) {
        auto *card = qobject_cast<GameCard *>(grid->itemAt(i)->widget());
        if (card && CoverCache::keyFor(card->game()) == key) {
            card->setCover(path);
            return;
        }
    }
}

void LibraryPage::setEmptyState(bool empty)
{
    emptyLabel->setText(empty ? tr("Игры не найдены") : tr("Карточек: %1")
                                                          .arg(m_games.size()));
}

void LibraryPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflow();
}
