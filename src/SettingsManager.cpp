#include "SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>

SettingsManager &SettingsManager::instance()
{
    static SettingsManager s;
    return s;
}

QString SettingsManager::settingsPath()
{
    // Конфиги храним в стандартном для приложения месте, а не по абсолютному
    // пути к исходникам — иначе exe не работает после переезда/на другой машине.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("settings.json"));
}

void SettingsManager::load()
{
    m_values.clear();
    m_lastLoadFailed = false;
    m_values.insert(QStringLiteral("autoOptimize"), false);
    m_values.insert(QStringLiteral("closeBrowsersOnOptimize"), true);

    QFile file(settingsPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray raw = file.readAll();
        file.close();

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                m_values.insert(it.key(), it.value().toVariant());
        } else if (parseError.error != QJsonParseError::NoError) {
            // Фаза E: битый JSON — сообщаем наверх (диалог в MainWindow)
            // и оставляем копию, чтобы save() не затёр данные без следа.
            m_lastLoadFailed = true;
            qWarning("SettingsManager: %s повреждён (%s)",
                     qPrintable(settingsPath()),
                     qPrintable(parseError.errorString()));
            const QString backup = settingsPath() + QStringLiteral(".bad");
            QFile::remove(backup);
            QFile::copy(settingsPath(), backup);
        }
    }
    m_loaded = true;
}

void SettingsManager::save()
{
    QJsonObject obj;
    for (auto it = m_values.begin(); it != m_values.end(); ++it)
        obj.insert(it.key(), QJsonValue::fromVariant(it.value()));

    // Каталога конфигов может не существовать на свежей машине.
    const QFileInfo info(settingsPath());
    QDir().mkpath(info.absolutePath());

    QFile file(settingsPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
}

QVariant SettingsManager::get(const QString &key, const QVariant &defaultValue) const
{
    return m_values.value(key, defaultValue);
}

void SettingsManager::set(const QString &key, const QVariant &value)
{
    m_values.insert(key, value);
}
