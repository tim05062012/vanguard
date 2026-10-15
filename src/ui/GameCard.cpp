#include "ui/GameCard.h"

#include <QEnterEvent>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QVBoxLayout>

#include "IconFactory.h"

namespace {

// Габариты обложки (16:9 header.jpg из Steam CDN). Если картинка меньше
// бокса — показываем целиком без масштабирования (иконка из exe 32x32).
constexpr int kCoverWidth = 120;
constexpr int kCoverHeight = 68;
constexpr int kIconBox = 64;

// Иконка лаунчера — плейсхолдер, когда обложка ещё не загружена.
QString launcherIcon(const QString &launcher)
{
    if (launcher == QLatin1String("Steam"))
        return QStringLiteral("steam.svg");
    if (launcher == QLatin1String("Epic"))
        return QStringLiteral("epic.svg");
    return QStringLiteral("local.svg");
}

QColor launcherAccent(const QString &launcher)
{
    if (launcher == QLatin1String("Steam"))
        return QColor(QStringLiteral("#66C0F4"));
    if (launcher == QLatin1String("Epic"))
        return QColor(QStringLiteral("#E4E4E4"));
    return QColor(QStringLiteral("#00E5FF"));
}

} // namespace

GameCard::GameCard(const GameInfo &game, QWidget *parent)
    : QFrame(parent)
    , m_game(game)
{
    setObjectName(QStringLiteral("gameCard"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(14);

    iconLabel = new QLabel(this);
    iconLabel->setObjectName(QStringLiteral("coverLabel"));
    iconLabel->setFixedSize(kIconBox, kIconBox);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(IconFactory::tinted(launcherIcon(game.launcher),
                                             launcherAccent(game.launcher),
                                             QSize(48, 48)).pixmap(48, 48));
    root->addWidget(iconLabel, 0, Qt::AlignTop);

    auto *column = new QVBoxLayout;
    column->setSpacing(4);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(8);

    titleLabel = new QLabel(game.name, this);
    titleLabel->setObjectName(QStringLiteral("gameTitle"));
    titleLabel->setWordWrap(true);
    topRow->addWidget(titleLabel, 1);

    badgeLabel = new QLabel(game.launcher.isEmpty()
                                ? tr("LOCAL")
                                : game.launcher.toUpper(),
                            this);
    badgeLabel->setObjectName(QStringLiteral("launcherBadge"));
    badgeLabel->setProperty("launcher", game.launcher);
    topRow->addWidget(badgeLabel, 0, Qt::AlignTop);

    column->addLayout(topRow);

    pathLabel = new QLabel(game.path.isEmpty() ? tr("Путь не задан") : game.path,
                           this);
    pathLabel->setObjectName(QStringLiteral("gamePath"));
    // Обрезка длинного пути до двух строк без разрастания карточки.
    QFontMetrics fm(pathLabel->font());
    if (!game.path.isEmpty() && fm.horizontalAdvance(game.path) > 320)
        pathLabel->setText(fm.elidedText(game.path, Qt::ElideMiddle, 340));
    column->addWidget(pathLabel);

    column->addStretch(1);
    root->addLayout(column, 1);

    playButton = new QPushButton(tr("▶  PLAY"), this);
    playButton->setObjectName(QStringLiteral("playBtn"));
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setProperty("flat", true);
    root->addWidget(playButton, 0, Qt::AlignVCenter);

    // Кнопка невидима, пока курсор не наведён на карточку.
    auto *effect = new QGraphicsOpacityEffect(playButton);
    effect->setOpacity(0.0);
    playButton->setGraphicsEffect(effect);

    auto *fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(160);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    playButton->setProperty("fadeIn", QVariant::fromValue(fade));

    connect(playButton, &QPushButton::clicked, this, [this]() {
        emit playRequested(m_game);
    });
    connect(this, &GameCard::playRequested,
            this, [this](const GameInfo &) {
                // Обработка запуска лежит в LibraryPage.
            });
    connect(this, &QWidget::customContextMenuRequested,
            this, &GameCard::showContextMenu);
}

void GameCard::setCover(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    const QPixmap pixmap(path);
    if (pixmap.isNull())
        return;

    m_coverPath = path;
    iconLabel->setFixedSize(kCoverWidth, kCoverHeight);

    // Маленькая иконка из exe — целиком; обложка из CDN — масштаб + кадрирование.
    if (pixmap.width() <= kCoverWidth && pixmap.height() <= kCoverHeight)
        iconLabel->setPixmap(pixmap);
    else
        iconLabel->setPixmap(pixmap.scaled(iconLabel->size(),
                                           Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation));
}

void GameCard::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);

    QAction *playAction = menu.addAction(tr("Запустить"));

    QAction *editAction = nullptr;
    QAction *deleteAction = nullptr;

    // Edit/Delete только у Local: у Steam/Epic запись — результат скана,
    // «удаление» вернётся после следующего обновления библиотеки.
    if (m_game.launcher == QLatin1String("Local")) {
        menu.addSeparator();
        editAction = menu.addAction(tr("Редактировать…"));
        deleteAction = menu.addAction(tr("Удалить"));
    }

    QAction *chosen = menu.exec(mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == playAction)
        emit playRequested(m_game);
    else if (chosen == editAction)
        emit editRequested(m_game);
    else if (chosen == deleteAction)
        emit deleteRequested(m_game);
}

void GameCard::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    if (auto *fade = qobject_cast<QPropertyAnimation *>(
            playButton->property("fadeIn").value<QPropertyAnimation *>())) {
        fade->setDirection(QPropertyAnimation::Forward);
        fade->start();
    }
}

void GameCard::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    if (auto *fade = qobject_cast<QPropertyAnimation *>(
            playButton->property("fadeIn").value<QPropertyAnimation *>())) {
        fade->setDirection(QPropertyAnimation::Backward);
        fade->start();
    }
}

void GameCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit launchRequested(m_game);
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}
