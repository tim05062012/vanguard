#include "GameScanner.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "LocalGamesStore.h"

GameScanner::GameScanner() = default;

namespace {

QString steamInstallPath()
{
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                  QSettings::NativeFormat);
    const QString path = reg.value(QStringLiteral("SteamPath")).toString();
    if (!path.isEmpty())
        return QDir::cleanPath(path);

    QSettings reg32(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"),
                    QSettings::NativeFormat);
    return QDir::cleanPath(reg32.value(QStringLiteral("InstallPath")).toString());
}

QList<QString> steamLibraryFolders(const QString &steamPath)
{
    QList<QString> folders;
    folders.append(steamPath);

    const QString vdfPath = QDir(steamPath).filePath(QStringLiteral("steamapps/libraryfolders.vdf"));
    QFile file(vdfPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return folders;

    QTextStream stream(&file);
    const QString content = stream.readAll();
    file.close();

    static const QRegularExpression re(QStringLiteral("\"path\"\\s*\"([^\"]+)\""));
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const auto match = it.next();
        QString p = match.captured(1);
        p.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        folders.append(QDir::cleanPath(p));
    }

    // Путь из реестра Steam может отличаться по регистру от пути в
    // libraryfolders.vdf — без case-insensitive дедупликации одна и та же
    // игра попадала в список дважды.
    QList<QString> unique;
    QStringList seen;
    for (const QString &folder : folders) {
        const QString cleaned = QDir::cleanPath(folder);
        const QString key = cleaned.toLower();
        if (seen.contains(key))
            continue;
        seen.append(key);
        unique.append(cleaned);
    }
    return unique;
}

QString readValueFromVdf(const QString &content, const QString &key)
{
    const QRegularExpression re(QStringLiteral("\"%1\"\\s*\"([^\"]*)\"")
                                    .arg(QRegularExpression::escape(key)));
    const auto match = re.match(content);
    return match.hasMatch() ? match.captured(1) : QString();
}

QJsonObject readJsonObject(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.isObject() ? doc.object() : QJsonObject();
}

}

QList<GameInfo> GameScanner::scanSteam()
{
    QList<GameInfo> games;

    const QString steamPath = steamInstallPath();
    if (steamPath.isEmpty() || !QDir(steamPath).exists())
        return games;

    const QList<QString> libraries = steamLibraryFolders(steamPath);

    for (const QString &library : libraries) {
        const QString appsDir = QDir(library).filePath(QStringLiteral("steamapps"));
        QDir dir(appsDir);
        if (!dir.exists())
            continue;

        const QStringList manifests = dir.entryList({QStringLiteral("appmanifest_*.acf")},
                                                    QDir::Files);
        for (const QString &manifest : manifests) {
            QFile file(dir.filePath(manifest));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            QTextStream stream(&file);
            const QString content = stream.readAll();
            file.close();

            GameInfo info;
            info.name = readValueFromVdf(content, QStringLiteral("name"));
            info.appId = readValueFromVdf(content, QStringLiteral("appid"));
            info.launcher = QStringLiteral("Steam");

            const QString installDir = readValueFromVdf(content, QStringLiteral("installdir"));
            if (!installDir.isEmpty())
                info.path = QDir(appsDir).filePath(QStringLiteral("common/") + installDir);

            if (!info.name.isEmpty())
                games.append(info);
        }
    }

    return games;
}

QList<GameInfo> GameScanner::scanEpic()
{
    QList<GameInfo> games;

    const QString manifestsDir =
        QStringLiteral("C:/ProgramData/Epic/EpicGamesLauncher/Data/Manifests");
    QDir dir(manifestsDir);
    if (!dir.exists())
        return games;

    const QStringList items = dir.entryList({QStringLiteral("*.item")}, QDir::Files);
    for (const QString &item : items) {
        const QJsonObject obj = readJsonObject(dir.filePath(item));
        if (obj.isEmpty())
            continue;

        GameInfo info;
        info.name = obj.value(QStringLiteral("DisplayName")).toString();
        info.path = obj.value(QStringLiteral("InstallLocation")).toString();
        info.appId = obj.value(QStringLiteral("AppName")).toString();
        if (info.appId.isEmpty())
            info.appId = obj.value(QStringLiteral("CatalogItemId")).toString();
        info.launcher = QStringLiteral("Epic");

        if (!info.name.isEmpty())
            games.append(info);
    }

    return games;
}

QList<GameInfo> GameScanner::scanLocal()
{
    // Хранение и CRUD вынесены в LocalGamesStore (v1.0) — сканер только читает.
    return LocalGamesStore::instance().list();
}

QList<GameInfo> GameScanner::scanAll()
{
    QList<GameInfo> games;
    games.append(scanSteam());
    games.append(scanEpic());

    // Ручная запись из local_games.json может дублировать уже найденную
    // игру (один и тот же appId у лаунчера) — карточка должна быть одна.
    for (const GameInfo &local : scanLocal()) {
        bool duplicate = false;
        for (const GameInfo &existing : games) {
            if (existing.launcher == local.launcher && !local.appId.isEmpty()
                && existing.appId == local.appId) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            games.append(local);
    }
    return games;
}

bool GameScanner::steamInstalled() const
{
    const QString path = steamInstallPath();
    return !path.isEmpty() && QDir(path).exists();
}

bool GameScanner::epicInstalled() const
{
    // scanEpic() читает каталог манифестов — он появляется при установке
    // Epic Games Launcher (фаза E, диагноз пустой библиотеки).
    return QDir(QStringLiteral("C:/ProgramData/Epic/EpicGamesLauncher/"
                               "Data/Manifests"))
        .exists();
}
