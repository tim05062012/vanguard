#ifndef GAMECARD_H
#define GAMECARD_H

#include <QFrame>

#include "GameScanner.h"

class QLabel;
class QPushButton;

// Карточка игры в библиотеке: обложка (cover) или иконка лаунчера,
// название, бейдж лаунчера, кнопка Play при наведении. У локальных игр
// есть контекстное меню: Запустить / Редактировать / Удалить.
class GameCard : public QFrame
{
    Q_OBJECT

public:
    explicit GameCard(const GameInfo &game, QWidget *parent = nullptr);

    const GameInfo &game() const { return m_game; }

    // Подменяет SVG-заглушку на обложку (путь к файлу в кеше CoverCache).
    void setCover(const QString &path);

signals:
    void playRequested(const GameInfo &game);
    void launchRequested(const GameInfo &game);
    void editRequested(const GameInfo &game);
    void deleteRequested(const GameInfo &game);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void showContextMenu(const QPoint &pos);

    GameInfo m_game;
    QString m_coverPath;

    QLabel *iconLabel = nullptr;
    QLabel *titleLabel = nullptr;
    QLabel *pathLabel = nullptr;
    QLabel *badgeLabel = nullptr;
    QPushButton *playButton = nullptr;
};

#endif // GAMECARD_H
