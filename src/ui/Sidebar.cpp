#include "ui/Sidebar.h"

#include <QButtonGroup>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>

#include "IconFactory.h"

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(220);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 20, 14, 16);
    layout->setSpacing(6);

    auto *caption = new QLabel(tr("НАВИГАЦИЯ"), this);
    caption->setObjectName(QStringLiteral("sidebarCaption"));
    layout->addWidget(caption);
    layout->addSpacing(4);

    group = new QButtonGroup(this);
    group->setExclusive(true);

    libraryBtn = addNavButton(tr("Библиотека"), QStringLiteral("library.svg"),
                              LibraryPage);
    monitorBtn = addNavButton(tr("Мониторинг"), QStringLiteral("monitor.svg"),
                              MonitorPage);
    optimizeBtn = addNavButton(tr("Оптимизация"), QStringLiteral("optimize.svg"),
                               OptimizePage);

    layout->addSpacing(14);

    auto *toolsCaption = new QLabel(tr("ИНСТРУМЕНТЫ"), this);
    toolsCaption->setObjectName(QStringLiteral("sidebarCaption"));
    layout->addWidget(toolsCaption);
    layout->addSpacing(4);

    refreshBtn = new QPushButton(tr("Обновить библиотеку"), this);
    refreshBtn->setObjectName(QStringLiteral("sidebarAction"));
    refreshBtn->setIcon(IconFactory::tinted(QStringLiteral("restore.svg"),
                                            QColor(QStringLiteral("#8A8A99")),
                                            QSize(16, 16)));
    refreshBtn->setIconSize(QSize(16, 16));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(refreshBtn);

    settingsBtn = new QPushButton(tr("Настройки"), this);
    settingsBtn->setObjectName(QStringLiteral("sidebarAction"));
    settingsBtn->setIcon(IconFactory::tinted(QStringLiteral("settings.svg"),
                                             QColor(QStringLiteral("#8A8A99")),
                                             QSize(16, 16)));
    settingsBtn->setIconSize(QSize(16, 16));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(settingsBtn);

    layout->addStretch(1);

    auto *version = new QLabel(tr("v1.0.0"), this);
    version->setObjectName(QStringLiteral("sidebarCaption"));
    layout->addWidget(version);

    // По умолчанию активна библиотека.
    libraryBtn->setChecked(true);

    connect(refreshBtn, &QPushButton::clicked, this, &Sidebar::refreshRequested);
    connect(settingsBtn, &QPushButton::clicked, this, &Sidebar::settingsRequested);
}

QPushButton *Sidebar::addNavButton(const QString &text, const QString &iconPath,
                                   int pageIndex)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("navButton"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(IconFactory::tinted(iconPath,
                                        QColor(QStringLiteral("#8A8A99")),
                                        QSize(20, 20)));
    button->setIconSize(QSize(20, 20));

    group->addButton(button, pageIndex);

    // Важно: кнопку нужно добавить в layout сайдбара, иначе она остаётся
    // на позиции (0,0) с дефолтным размером и «наезжает» на соседей.
    if (auto *box = qobject_cast<QBoxLayout *>(layout()))
        box->addWidget(button);

    connect(button, &QPushButton::toggled, this, [this, pageIndex](bool on) {
        if (on)
            emit pageRequested(pageIndex);
    });

    return button;
}

void Sidebar::setCurrentPage(int pageIndex)
{
    QAbstractButton *button = group->button(pageIndex);
    if (!button)
        return;
    if (!button->isChecked())
        button->setChecked(true);
}

void Sidebar::attach(QStackedWidget *stack)
{
    if (!stack)
        return;

    // При смене страницы извне — подсвечиваем соответствующую кнопку,
    // не порождая обратного вызова pageRequested (setButtonChecked guard).
    connect(stack, &QStackedWidget::currentChanged, this,
            [this](int index) { setCurrentPage(index); });

    // Нажатие кнопки переводит страницу.
    connect(this, &Sidebar::pageRequested, stack, &QStackedWidget::setCurrentIndex);
}
