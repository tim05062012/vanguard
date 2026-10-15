#include "ui/TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

#include "IconFactory.h"

namespace {

// Пресет для кнопки окна: свой objectName, без рамки, размер задаётся в QSS.
QPushButton *makeWindowButton(const QString &objectName,
                              const QString &iconPath,
                              const QColor &color,
                              QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(IconFactory::tinted(iconPath, color, QSize(12, 12)));
    button->setIconSize(QSize(12, 12));
    button->setToolTip(QString());
    return button;
}

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(44);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 8, 0);
    layout->setSpacing(10);

    logoLabel = new QLabel(this);
    logoLabel->setPixmap(IconFactory::tinted(QStringLiteral("logo.svg"),
                                             QColor(QStringLiteral("#00E5FF")),
                                             QSize(20, 20)).pixmap(20, 20));
    layout->addWidget(logoLabel);

    titleLabel = new QLabel(tr("VANGUARD"), this);
    titleLabel->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(titleLabel);

    subtitleLabel = new QLabel(tr("GAME LAUNCHER // v1.0"), this);
    subtitleLabel->setObjectName(QStringLiteral("appSubtitle"));
    layout->addWidget(subtitleLabel);
    layout->addStretch(1);

    minBtn = makeWindowButton(QStringLiteral("winBtn"),
                              QStringLiteral("win-min.svg"),
                              QColor(QStringLiteral("#8A8A99")), this);
    maxBtn = makeWindowButton(QStringLiteral("winBtn"),
                              QStringLiteral("win-max.svg"),
                              QColor(QStringLiteral("#8A8A99")), this);
    closeBtn = makeWindowButton(QStringLiteral("winCloseBtn"),
                                QStringLiteral("win-close.svg"),
                                QColor(QStringLiteral("#8A8A99")), this);

    minBtn->setToolTip(tr("Свернуть"));
    maxBtn->setToolTip(tr("Развернуть"));
    closeBtn->setToolTip(tr("Закрыть"));

    layout->addWidget(minBtn);
    layout->addWidget(maxBtn);
    layout->addWidget(closeBtn);

    connect(minBtn, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
    connect(maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeRequested);
    connect(closeBtn, &QPushButton::clicked, this, &TitleBar::closeRequested);
}

void TitleBar::setWindowMaximized(bool maximized)
{
    maxBtn->setIcon(IconFactory::tinted(
        maximized ? QStringLiteral("win-max.svg") : QStringLiteral("win-max.svg"),
        QColor(QStringLiteral("#8A8A99")), QSize(12, 12)));
    maxBtn->setToolTip(maximized ? tr("Восстановить") : tr("Развернуть"));
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        startDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging && (event->buttons() & Qt::LeftButton)) {
        moveDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Двойной клик по пустой области — как у стандартного топбара.
    if (event->button() == Qt::LeftButton) {
        emit maximizeRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::startDrag(const QPoint &globalPos)
{
    QWidget *host = window();
    if (!host)
        return;

    dragging = true;
    // Смещение от левого верхнего угла окна, чтобы окно не прыгало под курсор.
    dragOffset = globalPos - host->frameGeometry().topLeft();
    moveDrag(globalPos);
}

void TitleBar::moveDrag(const QPoint &globalPos)
{
    QWidget *host = this->window();
    if (!host)
        return;

    QPoint target = globalPos - dragOffset;

    // Не даём окну полностью уйти за пределы экрана.
    const QRect available = screen() ? screen()->availableGeometry()
                                     : QRect();
    if (available.isValid()) {
        target.setX(qBound(available.left() - host->frameGeometry().width() + 80,
                           target.x(),
                           available.right() - 80));
        target.setY(qBound(available.top(), target.y(), available.bottom() - 44));
    }

    host->move(target);
}
