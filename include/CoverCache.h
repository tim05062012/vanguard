#ifndef COVERCACHE_H
#define COVERCACHE_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

struct GameInfo;
class QNetworkAccessManager;

// Кеш обложек игр: covers/{key}.jpg|png.
//
// Каталог (решение v1.0: гибрид + настройка coversDir):
//   1. настройка coversDir, если задана и в неё удаётся записать;
//   2. dev-режим (рядом с exe лежит ../CMakeLists.txt) -> <project>/covers;
//   3. установленная версия -> %LOCALAPPDATA%/VANGUARD/covers;
//   4. аварийный fallback -> <exe>/covers.
//
// Источники обложек:
//   Steam — CDN (проверено: все хосты отдают 200 для header.jpg);
//   Local — иконка из exe (WinAPI ExtractIconExW -> 32bpp DIB -> PNG);
//   Epic  — публичного CDN нет -> пустой путь, UI использует SVG-иконку.
class CoverCache : public QObject
{
    Q_OBJECT

public:
    static CoverCache &instance();

    // Резолв каталога + mkpath (создаётся при первом обращении).
    QString coverDir();

    // Явный каталог из настроек coversDir (пусто = авто-резолв).
    void setCoverDir(const QString &dir);

    // Путь к файлу обложки в кеше (или пусто — fallback на SVG-иконку).
    QString coverPathFor(const GameInfo &game);

    // Запрашивает обложку, если файла ещё нет. Всегда асинхронно (QNetwork
    // или WinAPI без блокировок UI). Эмитит coverReady(key, path):
    // path пустой — обложку получить не удалось.
    void request(const GameInfo &game);

    // Копия пользовательской обложки в кеш; возвращает путь в кеше или пусто.
    QString importCover(const QString &key, const QString &sourcePath);

    // Стабильный ключ кеша для игры ("steam_1127400", "local-<uuid>").
    // Нужен UI: LibraryPage сопоставляет coverReady с карточками.
    static QString keyFor(const GameInfo &game);

signals:
    void coverReady(const QString &key, const QString &path);

private:
    explicit CoverCache(QObject *parent = nullptr);

    QString resolveCoverDir();
    void requestSteam(const GameInfo &game, const QString &key,
                      const QString &target, int attempt);
    QString extractExeIcon(const QString &exePath, const QString &target);

    QNetworkAccessManager *m_nam = nullptr;
    QSet<QString> m_inFlight;   // защита от дублей запросов
    QString m_dirOverride;
    QString m_dir;              // кеш резолва
};

#endif // COVERCACHE_H