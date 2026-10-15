#ifndef GAMESCANNER_H
#define GAMESCANNER_H

#include <QString>
#include <QList>

struct GameInfo {
    QString name;
    QString path;
    QString launcher;   // Steam / Epic / Local (ручная запись может быть любым)
    QString appId;      // Steam appid / Epic AppName / localId для Local

    // --- поля v1.0 (добавление игр вручную + обложки) ---
    QString localId;    // стабильный id локальной записи ("local-...")
    QString workingDir; // рабочая директория (опционально, для запуска)
    QString coverPath;  // путь к обложке в кеше covers/ (если есть)
    QString addedAt;    // дата добавления, ISO-8601 (только Local)
};

class GameScanner {
public:
    GameScanner();
    QList<GameInfo> scanSteam();
    QList<GameInfo> scanEpic();
    QList<GameInfo> scanLocal();
    QList<GameInfo> scanAll();

    // Фаза E: диагностика для пустой библиотеки — установлены ли лаунчеры
    // (используется в сообщении «Игры не найдены»).
    bool steamInstalled() const;
    bool epicInstalled() const;
};

#endif // GAMESCANNER_H
