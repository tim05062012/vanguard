#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QString>
#include <QVariant>
#include <QMap>

class SettingsManager {
public:
    static SettingsManager &instance();

    void load();
    void save();

    QVariant get(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void set(const QString &key, const QVariant &value);

    static QString settingsPath();

    // Фаза E: true, если settings.json существовал, но JSON был повреждён
    // (копия сохранена в settings.json.bad, значения — дефолтные).
    bool lastLoadFailed() const { return m_lastLoadFailed; }

private:
    SettingsManager() = default;
    SettingsManager(const SettingsManager &) = delete;
    SettingsManager &operator=(const SettingsManager &) = delete;

    QMap<QString, QVariant> m_values;
    bool m_loaded = false;
    bool m_lastLoadFailed = false;
};

#endif // SETTINGSMANAGER_H
