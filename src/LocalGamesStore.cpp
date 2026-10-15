#include "LocalGamesStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
#include <QUuid>

#include "GameScanner.h"

namespace {

// Стабильный id для старых записей без localId. md5, а не qHash:
// QCryptographicHash детерминирован между запусками, qHash сидируется
// на процесс — обложка «переехала» бы после перезапуска.
QString stableId(const QString &name, const QString &path)
{
    const QByteArray raw = (name + QLatin1Char('|') + path).toUtf8();
    const QString hex = QString::fromLatin1(
        QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex().left(16));
    return QStringLiteral("local-") + hex;
}

} // namespace

LocalGamesStore &LocalGamesStore::instance()
{
    static LocalGamesStore s;
    return s;
}

QString LocalGamesStore::resolveStorePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("local_games.json")),
        QDir(appDir).filePath(QStringLiteral("../local_games.json")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("local_games.json")),
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(QStringLiteral("local_games.json")),
    };

    // Существующий файл — приоритет (как раньше в GameScanner).
    for (const QString &path : candidates) {
        if (QFile::exists(path))
            return QDir::cleanPath(path);
    }

    // Файла нет: первый каталог, в который удаётся запись. Проба пишет
    // и удаляет .probe — QFileInfo::isWritable на Windows неточно.
    for (const QString &path : candidates) {
        QFile probe(path + QStringLiteral(".probe"));
        if (probe.open(QIODevice::Append)) {
            probe.close();
            probe.remove();
            return QDir::cleanPath(path);
        }
    }
    return QDir::cleanPath(candidates.first());
}

QString LocalGamesStore::storePath()
{
    if (m_path.isEmpty())
        m_path = resolveStorePath();
    return m_path;
}

QString LocalGamesStore::lastError() const
{
    return m_lastError;
}

bool LocalGamesStore::hadParseError() const
{
    return m_hadParseError;
}

QList<GameInfo> LocalGamesStore::list()
{
    return readAll();
}

QList<GameInfo> LocalGamesStore::readAll()
{
    QList<GameInfo> items;
    m_hadParseError = false;
    m_lastError.clear();

    QFile file(storePath());
    if (!file.exists())
        return items; // пустая библиотека — не ошибка

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = file.errorString();
        return items;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        m_hadParseError = true;
        m_lastError = QStringLiteral("local_games.json повреждён: %1")
                          .arg(parseError.errorString());
        return items;
    }

    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject())
            continue;
        const QJsonObject obj = value.toObject();

        GameInfo info;
        info.name = obj.value(QStringLiteral("name")).toString();
        info.path = obj.value(QStringLiteral("path")).toString();
        info.appId = obj.value(QStringLiteral("appId")).toString();
        info.launcher =
            obj.value(QStringLiteral("launcher")).toString(QStringLiteral("Local"));
        info.workingDir = obj.value(QStringLiteral("workingDir")).toString();
        info.coverPath = obj.value(QStringLiteral("coverPath")).toString();
        info.addedAt = obj.value(QStringLiteral("addedAt")).toString();
        info.localId = obj.value(QStringLiteral("localId")).toString();

        if (info.name.trimmed().isEmpty())
            continue;

        if (info.launcher.trimmed().isEmpty())
            info.launcher = QStringLiteral("Local");
        // Старые записи (v0.2) — стабильный синтезированный id.
        if (info.localId.isEmpty())
            info.localId = stableId(info.name, info.path);
        if (info.appId.isEmpty())
            info.appId = info.localId;

        items.append(info);
    }
    return items;
}

bool LocalGamesStore::writeAll(const QList<GameInfo> &items)
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Бэкап перед каждой заменой: tmp пишем атомарно, чтобы сбой не
    // оставил обрезанный JSON (битый файл молча читается как пустой).
    if (QFile::exists(path)) {
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::copy(path, path + QStringLiteral(".bak"));
    }

    QJsonArray arr;
    for (const GameInfo &game : items) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), game.name);
        obj.insert(QStringLiteral("path"), game.path);
        obj.insert(QStringLiteral("launcher"), game.launcher);
        obj.insert(QStringLiteral("appId"), game.appId);
        obj.insert(QStringLiteral("localId"), game.localId);
        obj.insert(QStringLiteral("workingDir"), game.workingDir);
        obj.insert(QStringLiteral("coverPath"), game.coverPath);
        obj.insert(QStringLiteral("addedAt"), game.addedAt);
        arr.append(obj);
    }

    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_lastError = out.errorString();
        return false;
    }
    out.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    out.close();

    if (QFile::exists(path))
        QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        m_lastError = QStringLiteral("Не удалось записать %1").arg(path);
        return false;
    }
    return true;
}

QString LocalGamesStore::add(const GameInfo &game)
{
    if (game.name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Имя игры не может быть пустым.");
        return QString();
    }

    QList<GameInfo> items = readAll();

    GameInfo entry = game;
    if (entry.localId.trimmed().isEmpty())
        entry.localId = QStringLiteral("local-") +
                        QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (entry.launcher.trimmed().isEmpty())
        entry.launcher = QStringLiteral("Local");
    if (entry.addedAt.isEmpty())
        entry.addedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (entry.appId.trimmed().isEmpty())
        entry.appId = entry.localId;

    items.append(entry);
    if (!writeAll(items))
        return QString();
    return entry.localId;
}

bool LocalGamesStore::update(const QString &localId, const GameInfo &game)
{
    QList<GameInfo> items = readAll();

    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).localId != localId)
            continue;

        const GameInfo previous = items.at(i);
        GameInfo entry = game;

        entry.localId = localId; // id неизменяем: от него зависят обложки
        if (entry.addedAt.isEmpty())
            entry.addedAt = previous.addedAt;
        if (entry.launcher.trimmed().isEmpty())
            entry.launcher = previous.launcher;
        if (entry.appId.trimmed().isEmpty())
            entry.appId = entry.localId;

        items[i] = entry;
        return writeAll(items);
    }

    m_lastError = QStringLiteral("Запись %1 не найдена.").arg(localId);
    return false;
}

bool LocalGamesStore::remove(const QString &localId)
{
    QList<GameInfo> items = readAll();

    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).localId == localId) {
            items.removeAt(i);
            return writeAll(items);
        }
    }

    m_lastError = QStringLiteral("Запись %1 не найдена.").arg(localId);
    return false;
}

