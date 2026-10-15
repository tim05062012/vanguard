#ifndef SIDEBAR_H
#define SIDEBAR_H

#include <QWidget>

class QButtonGroup;
class QPushButton;
class QStackedWidget;

// Навигационная панель 220px: иконка + текст, активная кнопка помечена
// циановой полосой слева (см. QSS #navButton:checked).
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    // Индексы страниц должны совпадать с порядком в QStackedWidget.
    enum PageIndex {
        LibraryPage = 0,
        MonitorPage = 1,
        OptimizePage = 2
    };

    explicit Sidebar(QWidget *parent = nullptr);

    // Выделяет нужную кнопку (вызывается и программно, и из MainWindow
    // после внешнего переключения страницы).
    void setCurrentPage(int pageIndex);

    // Синхронизация с QStackedWidget — MainWindow вызывает после переходов.
    void attach(QStackedWidget *stack);

signals:
    void pageRequested(int pageIndex);
    void settingsRequested();
    void refreshRequested();

private:
    QPushButton *addNavButton(const QString &text, const QString &iconPath,
                              int pageIndex);

    QButtonGroup *group = nullptr;
    QPushButton *libraryBtn = nullptr;
    QPushButton *monitorBtn = nullptr;
    QPushButton *optimizeBtn = nullptr;
    QPushButton *settingsBtn = nullptr;
    QPushButton *refreshBtn = nullptr;
};

#endif // SIDEBAR_H
