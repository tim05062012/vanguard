#include "CoverCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QStandardPaths>
#include <QUrl>

#include "GameScanner.h"
#include "SettingsManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// Цепочка URL Steam CDN: новые хосты надёжнее, старый akamaihd — как
// последний запасной (все три отдают 200 на header.jpg, проверено).
QStringList steamUrls(const QString &appId)
{
    return {
        QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/header.jpg").arg(appId),
        QStringLiteral("https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%1/header.jpg").arg(appId),
        QStringLiteral("https://steamcdn-a.akamaihd.net/steam/apps/%1/header.jpg").arg(appId),
        QStringLiteral("https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900.jpg").arg(appId),
    };
}

} // namespace

CoverCache &CoverCache::instance()
{
    static CoverCache s;
    return s;
}

CoverCache::CoverCache(QObject *parent)
    : QObject(parent)
{
}

void CoverCache::setCoverDir(const QString &dir)
{
    m_dirOverride = dir.trimmed();
    m_dir.clear(); // форсируем перерезолв
}

QString CoverCache::resolveCoverDir()
{
    // 1. Явный каталог из настроек (проверяем записью — isWritable на
    //    Windows неточно, а отказ UI покажет при сохранении настроек).
    const QString configured = m_dirOverride.isEmpty()
        ? SettingsManager::instance().get(QStringLiteral("coversDir")).toString().trimmed()
        : m_dirOverride;
    if (!configured.isEmpty()) {
        QDir dir(configured);
        QFile probe(dir.filePath(QStringLiteral(".probe")));
        if (probe.open(QIODevice::Append)) {
            probe.close();
            probe.remove();
            return QDir::cleanPath(configured);
        }
    }

    // 2. Dev-режим: exe в build/, рядом лежит CMakeLists.txt -> <project>/covers.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFile::exists(QDir(appDir).filePath(QStringLiteral("../CMakeLists.txt")))) {
        const QString projectCovers =
            QDir(appDir).filePath(QStringLiteral("../covers"));
        QDir().mkpath(projectCovers);
        return QDir::cleanPath(projectCovers);
    }

    // 3. Установленная версия: %LOCALAPPDATA%/VANGUARD/covers (тот же
    //    каталог, что у settings.json, — без прав администратора).
    const QString localAppData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/covers");
    QDir().mkpath(localAppData);
    return QDir::cleanPath(localAppData);
}

QString CoverCache::coverDir()
{
    if (m_dir.isEmpty())
        m_dir = resolveCoverDir();
    QDir().mkpath(m_dir);
    return m_dir;
}

QString CoverCache::keyFor(const GameInfo &game)
{
    // Обложка привязана к идентификатору лаунчера, а не к имени игры:
    // переименование игры не должно сбрасывать кеш.
    if (game.launcher == QStringLiteral("Steam") && !game.appId.isEmpty())
        return QStringLiteral("steam_") + game.appId;
    if (game.launcher == QStringLiteral("Epic") && !game.appId.isEmpty())
        return QStringLiteral("epic_") + game.appId;
    if (game.launcher == QStringLiteral("Local")) {
        const QString id = game.localId.isEmpty() ? game.appId : game.localId;
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

QString CoverCache::coverPathFor(const GameInfo &game)
{
    const QString key = keyFor(game);
    if (key.isEmpty())
        return QString();

    const QString dir = coverDir();

    // Канонические имена в кеше (importCover кладёт .png, Steam — .jpg).
    for (const QString &ext : {QStringLiteral(".png"), QStringLiteral(".jpg")}) {
        const QString path = QDir(dir).filePath(key + ext);
        if (QFile::exists(path))
            return path;
    }

    // Пользовательский файл ещё не скопирован, но существует — отдаём его.
    if (!game.coverPath.isEmpty() && QFile::exists(game.coverPath))
        return game.coverPath;

    return QString();
}

QString CoverCache::importCover(const QString &key, const QString &sourcePath)
{
    if (key.isEmpty() || !QFile::exists(sourcePath))
        return QString();

    const QString target = QDir(coverDir()).filePath(key + QStringLiteral(".png"));
    if (QFile::exists(target)) {
        // Тот же файл — уже импортирован. Другой — заменяем (смена
        // обложки в диалоге редактирования).
        if (QFileInfo(target).canonicalFilePath()
            == QFileInfo(sourcePath).canonicalFilePath())
            return target;
        QFile::remove(target);
    }

    const QPixmap source(sourcePath);
    if (source.isNull())
        return QString();

    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!source.save(target, "PNG"))
        return QString();
    return target;
}

void CoverCache::request(const GameInfo &game)
{
    const QString key = keyFor(game);
    if (key.isEmpty())
        return;

    const QString target = QDir(coverDir()).filePath(
        key + (game.launcher == QStringLiteral("Steam") ? QStringLiteral(".jpg")
                                                        : QStringLiteral(".png")));

    if (QFile::exists(target)) {
        emit coverReady(key, target);
        return;
    }
    if (m_inFlight.contains(key))
        return; // уже качается

    // Пользовательская обложка из диалога: копируем в кеш.
    if (!game.coverPath.isEmpty() && QFile::exists(game.coverPath)) {
        const QString imported = importCover(key, game.coverPath);
        if (!imported.isEmpty()) {
            emit coverReady(key, imported);
            return;
        }
    }

    if (game.launcher == QStringLiteral("Steam") && !game.appId.isEmpty()) {
        m_inFlight.insert(key);
        requestSteam(game, key, target, 0);
        return;
    }

    if (game.launcher == QStringLiteral("Local")) {
        const QString exe = game.path.isEmpty() ? QString() : game.path;
        if (!exe.isEmpty() && QFileInfo::exists(exe)) {
            const QString icon = extractExeIcon(exe, target);
            emit coverReady(key, icon); // пусто — фолбэк на SVG-иконку
            return;
        }
    }

    // Epic: публичного CDN обложек нет (в манифесте только CatalogItemId) —
    // фолбэк на SVG-иконку лаунчера, попытка CDN отложена до v1.1.
    emit coverReady(key, QString());
}

void CoverCache::requestSteam(const GameInfo &game, const QString &key,
                              const QString &target, int attempt)
{
    const QStringList urls = steamUrls(game.appId);
    if (attempt >= urls.size()) {
        m_inFlight.remove(key);
        emit coverReady(key, QString());
        return;
    }

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(urls.at(attempt))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, game, key, target, attempt]() {
                reply->deleteLater();

                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = reply->readAll();

                // Валидация: 200, картинка, а не HTML-заглушка CDN.
                if (reply->error() == QNetworkReply::NoError && status == 200
                    && body.size() > 1024) {
                    QDir().mkpath(QFileInfo(target).absolutePath());
                    QFile out(target);
                    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        out.write(body);
                        out.close();
                        m_inFlight.remove(key);
                        emit coverReady(key, target);
                        return;
                    }
                }

                // Следующий хост из цепочки.
                requestSteam(game, key, target, attempt + 1);
            });
}

QString CoverCache::extractExeIcon(const QString &exePath, const QString &target)
{
#ifdef Q_OS_WIN
    // Иконка 32x32 (ExtractIconEx big icon). Для современных exe с
    // 256px-иконками Windows отдаёт уменьшенную копию — для карточки
    // 150x84 этого хватает; качество не устроило — ручной выбор PNG.
    const QString native = QDir::toNativeSeparators(exePath);
    HICON icon = nullptr;
    const UINT count = ExtractIconExW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                      0, &icon, nullptr, 1);
    if (count == 0 || !icon)
        return QString();

    const int w = GetSystemMetrics(SM_CXICON);
    const int h = GetSystemMetrics(SM_CYICON);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
    QString result;
    if (dib && bits) {
        HDC memDc = CreateCompatibleDC(screenDc);
        HGDIOBJ old = SelectObject(memDc, dib);
        DrawIconEx(memDc, 0, 0, icon, w, h, 0, nullptr, DI_NORMAL);

        // 32bpp DIB: BGRA в памяти — это и есть Format_ARGB32.
        QImage image(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y)
            memcpy(image.scanLine(y), static_cast<const char *>(bits) + y * w * 4,
                   static_cast<size_t>(w) * 4);

        SelectObject(memDc, old);
        DeleteDC(memDc);

        QDir().mkpath(QFileInfo(target).absolutePath());
        if (image.save(target, "PNG"))
            result = target;
    }
    if (dib)
        DeleteObject(dib);
    ReleaseDC(nullptr, screenDc);
    DestroyIcon(icon);
    return result;
#else
    Q_UNUSED(exePath)
    Q_UNUSED(target)
    return QString();
#endif
}
