#ifndef LIBRARYPAGE_H
#define LIBRARYPAGE_H

#include <QWidget>

#include "GameScanner.h"

class GameCard;
class QGridLayout;
class QLabel;
class QScrollArea;
class QToolButton;

// Библиотека: сетка карточек игр (2–3 колонки, зависит от ширины окна)
// и кнопка «+ Добавить игру» в шапке.
class LibraryPage : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryPage(QWidget *parent = nullptr);

    void setGames(const QList<GameInfo> &games);
    void refresh();

    // Точечная подстановка обложки на карточку (без пересборки сетки).
    // key — из CoverCache::keyFor(game).
    void setCover(const QString &key, const QString &path);

    // Вызывается MainWindow — после обновления списка карточек.
    void reflow();

signals:
    void launchGameRequested(const GameInfo &game);
    void refreshRequested();
    void addGameRequested();
    void editGameRequested(const GameInfo &game);
    void deleteGameRequested(const GameInfo &game);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildLayout();
    int optimalColumns() const;
    void setEmptyState(bool empty);

    QLabel *titleLabel = nullptr;
    QLabel *hintLabel = nullptr;
    QToolButton *addGameBtn = nullptr;
    QScrollArea *scrollArea = nullptr;
    QWidget *container = nullptr;
    QGridLayout *grid = nullptr;
    QLabel *emptyLabel = nullptr;

    QList<GameInfo> m_games;
    int m_lastColumns = -1;
};

#endif // LIBRARYPAGE_H
