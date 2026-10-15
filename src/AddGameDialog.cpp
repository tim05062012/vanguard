#include "AddGameDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

AddGameDialog::AddGameDialog(const GameInfo &initial, QWidget *parent)
    : QDialog(parent)
    , m_initial(initial)
{
    const bool editing = !initial.name.trimmed().isEmpty();
    setWindowTitle(editing ? tr("Редактирование игры") : tr("Добавить игру"));
    setModal(true);
    setMinimumWidth(460);

    auto *form = formLayout = new QFormLayout;
    form->setSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Имя
    nameEdit = new QLineEdit(initial.name, this);
    nameEdit->setPlaceholderText(tr("например, Mindustry"));
    form->addRow(tr("Имя игры:"), nameEdit);

    // Путь к exe + кнопка обзора
    auto *pathRow = new QHBoxLayout;
    pathRow->setSpacing(6);
    pathEdit = new QLineEdit(initial.path, this);
    pathEdit->setPlaceholderText(tr("C:/Games/game.exe"));
    auto *browseExeBtn = new QPushButton(tr("Обзор…"), this);
    browseExeBtn->setCursor(Qt::PointingHandCursor);
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseExeBtn);
    form->addRow(tr("Путь к exe:"), pathRow);

    // Рабочая папка (опционально)
    auto *folderRow = new QHBoxLayout;
    folderRow->setSpacing(6);
    folderEdit = new QLineEdit(initial.workingDir, this);
    folderEdit->setPlaceholderText(tr("необязательно — берётся каталог exe"));
    auto *browseFolderBtn = new QPushButton(tr("Обзор…"), this);
    browseFolderBtn->setCursor(Qt::PointingHandCursor);
    folderRow->addWidget(folderEdit, 1);
    folderRow->addWidget(browseFolderBtn);
    form->addRow(tr("Рабочая папка:"), folderRow);

    // Лаунчер
    launcherCombo = new QComboBox(this);
    launcherCombo->addItem(tr("Local (exe напрямую)"), QStringLiteral("Local"));
    launcherCombo->addItem(QStringLiteral("Steam"), QStringLiteral("Steam"));
    launcherCombo->addItem(QStringLiteral("Epic"), QStringLiteral("Epic"));
    const int launcherIndex =
        launcherCombo->findData(initial.launcher.isEmpty() ? QStringLiteral("Local")
                                                           : initial.launcher);
    launcherCombo->setCurrentIndex(launcherIndex >= 0 ? launcherIndex : 0);
    form->addRow(tr("Лаунчер:"), launcherCombo);

    // AppID (только Steam/Epic)
    appIdEdit = new QLineEdit(initial.appId, this);
    appIdEdit->setPlaceholderText(tr("например, 1127400"));
    form->addRow(tr("AppID:"), appIdEdit);

    // Обложка
    auto *coverRow = new QHBoxLayout;
    coverRow->setSpacing(8);
    coverPreview = new QLabel(this);
    coverPreview->setObjectName(QStringLiteral("coverPreview"));
    coverPreview->setFixedSize(120, 68);
    coverPreview->setAlignment(Qt::AlignCenter);
    coverPreview->setWordWrap(true);
    coverPreview->setScaledContents(false);

    auto *coverControls = new QVBoxLayout;
    coverControls->setSpacing(6);
    coverCaption = new QLabel(this);
    coverCaption->setObjectName(QStringLiteral("hintLabel"));
    auto *coverButtons = new QHBoxLayout;
    coverButtons->setSpacing(6);
    auto *coverChooseBtn = new QPushButton(tr("Выбрать файл…"), this);
    coverChooseBtn->setCursor(Qt::PointingHandCursor);
    coverAutoBtn = new QPushButton(tr("Авто"), this);
    coverAutoBtn->setCursor(Qt::PointingHandCursor);
    coverButtons->addWidget(coverChooseBtn);
    coverButtons->addWidget(coverAutoBtn);
    coverControls->addWidget(coverCaption);
    coverControls->addLayout(coverButtons);
    coverControls->addStretch(1);

    coverRow->addWidget(coverPreview, 0, Qt::AlignTop);
    coverRow->addLayout(coverControls, 1);
    form->addRow(tr("Обложка:"), coverRow);

    // Кнопки
    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Сохранить"));
    connect(buttonBox, &QDialogButtonBox::accepted, this, &AddGameDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &AddGameDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addLayout(form);
    layout->addWidget(buttonBox);

    // Сигналы
    connect(browseExeBtn, &QPushButton::clicked, this, &AddGameDialog::browseExe);
    connect(browseFolderBtn, &QPushButton::clicked, this,
            &AddGameDialog::browseFolder);
    connect(coverChooseBtn, &QPushButton::clicked, this,
            &AddGameDialog::chooseCover);
    connect(coverAutoBtn, &QPushButton::clicked, this, &AddGameDialog::coverAuto);
    connect(launcherCombo, &QComboBox::currentIndexChanged, this,
            &AddGameDialog::onLauncherChanged);
    connect(nameEdit, &QLineEdit::textChanged, this,
            &AddGameDialog::onNameChanged);

    // Пользовательская обложка: initial.coverPath не пуст и это файл.
    if (!initial.coverPath.isEmpty() && QFileInfo::exists(initial.coverPath))
        m_coverPath = initial.coverPath;

    onLauncherChanged(launcherCombo->currentIndex());
    updateCoverPreview();
    onNameChanged(nameEdit->text());
}

void AddGameDialog::browseExe()
{
    const QString start = pathEdit->text().isEmpty()
        ? QFileInfo(folderEdit->text()).absolutePath()
        : pathEdit->text();

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Выберите исполняемый файл"), start,
        tr("Исполняемый файл (*.exe);;Все файлы (*)"));
    if (file.isEmpty())
        return;

    pathEdit->setText(QDir::toNativeSeparators(file));

    // Подсказки: каталог exe как рабочая папка, имя файла как название игры.
    const QFileInfo info(file);
    if (folderEdit->text().isEmpty())
        folderEdit->setText(QDir::toNativeSeparators(info.absolutePath()));
    if (nameEdit->text().isEmpty())
        nameEdit->setText(info.completeBaseName());
}

void AddGameDialog::browseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Выберите рабочую папку игры"), folderEdit->text());
    if (!dir.isEmpty())
        folderEdit->setText(QDir::toNativeSeparators(dir));
}

void AddGameDialog::chooseCover()
{
    const QString start = m_coverPath.isEmpty()
        ? QFileInfo(folderEdit->text()).absolutePath()
        : m_coverPath;

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Выберите изображение обложки"), start,
        tr("Изображения (*.png *.jpg *.jpeg *.bmp);;Все файлы (*)"));
    if (file.isEmpty())
        return;

    m_coverPath = file;
    updateCoverPreview();
}

void AddGameDialog::coverAuto()
{
    m_coverPath.clear();
    updateCoverPreview();
}

void AddGameDialog::updateCoverPreview()
{
    const QPixmap pixmap(m_coverPath);
    if (m_coverPath.isEmpty() || pixmap.isNull()) {
        coverPreview->setPixmap(QPixmap());
        coverPreview->setText(tr("Авто"));
        const QString launcher = launcherCombo->currentData().toString();
        coverCaption->setText(launcher == QStringLiteral("Steam")
                                  ? tr("Обложка: Steam CDN (header.jpg)")
                                  : tr("Обложка: иконка из exe"));
        coverAutoBtn->setEnabled(false);
        return;
    }

    coverPreview->setText(QString());
    coverPreview->setPixmap(pixmap.scaled(coverPreview->size(),
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    coverCaption->setText(QFileInfo(m_coverPath).fileName());
    coverAutoBtn->setEnabled(true);
}

void AddGameDialog::onLauncherChanged(int index)
{
    const QString launcher = launcherCombo->itemData(index).toString();
    const bool withAppId = launcher == QStringLiteral("Steam")
                        || launcher == QStringLiteral("Epic");

    // Строка AppID имеет смысл только для магазинных лаунчеров.
    formLayout->setRowVisible(appIdEdit, withAppId);
    appIdEdit->setPlaceholderText(launcher == QStringLiteral("Steam")
                                      ? tr("например, 1127400")
                                      : tr("AppName из манифеста Epic"));
    updateCoverPreview();
}

void AddGameDialog::onNameChanged(const QString &name)
{
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!name.trimmed().isEmpty());
}

bool AddGameDialog::validate(QString *reason) const
{
    if (nameEdit->text().trimmed().isEmpty()) {
        *reason = tr("Укажите название игры.");
        return false;
    }

    const QString launcher = launcherCombo->currentData().toString();

    if (launcher == QStringLiteral("Local")) {
        const QString path = pathEdit->text().trimmed();
        if (path.isEmpty()) {
            *reason = tr("Укажите путь к exe (кнопка «Обзор…»).");
            return false;
        }
        if (!QFileInfo::exists(path)) {
            *reason = tr("Файл не найден:\n%1").arg(path);
            return false;
        }
    } else if (launcher == QStringLiteral("Steam")) {
        const QString appId = appIdEdit->text().trimmed();
        if (appId.isEmpty()) {
            *reason = tr("Укажите AppID игры в Steam (число).");
            return false;
        }
        bool isNumber = false;
        appId.toLongLong(&isNumber);
        if (!isNumber) {
            *reason = tr("AppID Steam — это число, например 1127400.");
            return false;
        }
    } else if (appIdEdit->text().trimmed().isEmpty()) {
        *reason = tr("Укажите AppName игры в Epic.");
        return false;
    }

    return true;
}

void AddGameDialog::accept()
{
    QString reason;
    if (!validate(&reason)) {
        QMessageBox::warning(this, tr("Добавить игру"), reason);
        return;
    }
    QDialog::accept();
}

GameInfo AddGameDialog::game() const
{
    GameInfo info;
    info.name = nameEdit->text().trimmed();
    info.path = pathEdit->text().trimmed();
    info.workingDir = folderEdit->text().trimmed();
    info.launcher = launcherCombo->currentData().toString();
    info.appId = info.launcher == QStringLiteral("Local")
        ? m_initial.localId // дополнится localId в хранилище
        : appIdEdit->text().trimmed();
    info.coverPath = m_coverPath;
    info.localId = m_initial.localId;
    info.addedAt = m_initial.addedAt;
    return info;
}
