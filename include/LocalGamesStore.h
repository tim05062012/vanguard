#ifndef LOCALGAMESSTORE_H
#define LOCALGAMESSTORE_H

#include <QList>
#include <QString>

struct GameInfo;

// Хранилище локальных игр — CRUD поверх local_games.json.
//
// Путь к файлу раньше искал GameScanner (рядом с exe, в родительской папке
// build/ -> корень проекта, в рабочей директории и в AppData) — логика
// перенесена сюда, чтобы сканер и CRUD не расходились. Если файла нет —
// берём первый каталог, в который реально удаётся записать: в dev-режиме
// это каталог exe, установленной версии — %LOCALAPPDATA% (запись в
// Program Files без прав администратора падала бы).
//
// Формат: JSON-массив объектов {name, path, launcher, appId, localId,
// workingDir, coverPath, addedAt}. Старые записи без localId получают
// детерминированный id из md5(имя|путь): qHash сидируется на процесс
// и менялся бы между запусками, а обложки привязаны к localId.
class LocalGamesStore
{
public:
    static LocalGamesStore &instance();

    // Все локальные записи. При битом JSON — пусто + hadParseError() == true.
    QList<GameInfo> list();

    // add() возвращает localId созданной записи (пусто при ошибке).
    QString add(const GameInfo &game);
    bool update(const QString &localId, const GameInfo &game);
    bool remove(const QString &localId);

    // Абсолютный путь к local_games.json (резолвится один раз на сессию).
    QString storePath();

    QString lastError() const;  // детали последней ошибки (для UI)
    bool hadParseError() const; // файл существовал, но JSON был повреждён

private:
    LocalGamesStore() = default;
    LocalGamesStore(const LocalGamesStore &) = delete;
    LocalGamesStore &operator=(const LocalGamesStore &) = delete;

    QList<GameInfo> readAll();
    bool writeAll(const QList<GameInfo> &items);
    QString resolveStorePath();

    QString m_path;   // пусто, пока не зарезолвился
    QString m_lastError;
    bool m_hadParseError = false;
};

#endif // LOCALGAMESSTORE_H