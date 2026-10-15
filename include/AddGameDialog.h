#ifndef ADDGAMEDIALOG_H
#define ADDGAMEDIALOG_H

#include <QDialog>

#include "GameScanner.h"

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

// Диалог добавления/редактирования игры: имя, путь к exe (QFileDialog),
// рабочая папка (опционально), выбор лаунчера и обложка.
// initial пустой — режим добавления; с данными — режим Edit (localId и
// addedAt наследуются, чтобы не терялись id и обложки).
class AddGameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddGameDialog(const GameInfo &initial = GameInfo(),
                           QWidget *parent = nullptr);

    // Заполненная запись (валидация уже пройдена в accept()).
    GameInfo game() const;

    void accept() override;

private:
    void browseExe();
    void browseFolder();
    void chooseCover();
    void coverAuto();
    void updateCoverPreview();
    void onLauncherChanged(int index);
    void onNameChanged(const QString &name);
    bool validate(QString *reason) const;

    GameInfo m_initial;
    QString m_coverPath; // пусто = «Авто» (Steam CDN / иконка из exe)

    QLineEdit *nameEdit = nullptr;
    QLineEdit *pathEdit = nullptr;
    QLineEdit *folderEdit = nullptr;
    QComboBox *launcherCombo = nullptr;
    QLineEdit *appIdEdit = nullptr;
    QFormLayout *formLayout = nullptr;
    QLabel *coverPreview = nullptr;
    QLabel *coverCaption = nullptr;
    QPushButton *coverAutoBtn = nullptr;
    QDialogButtonBox *buttonBox = nullptr;
};

#endif // ADDGAMEDIALOG_H