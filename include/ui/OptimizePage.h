#ifndef OPTIMIZEPAGE_H
#define OPTIMIZEPAGE_H

#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

class OptimizeReport;

// Страница оптимизации: кнопки Optimize/Restore и живой отчёт вместо
// всплывающих QMessageBox (см. OptimizeReport в Optimizer.h).
class OptimizePage : public QWidget
{
    Q_OBJECT

public:
    explicit OptimizePage(QWidget *parent = nullptr);

    void showReport(const OptimizeReport &report);

signals:
    void optimizeRequested();
    void restoreRequested();

private:
    QLabel *makeStatBlock(const QString &caption);

    QLabel *headline = nullptr;
    QLabel *statProcesses = nullptr;
    QLabel *statMemory = nullptr;
    QLabel *statPower = nullptr;
    QLabel *statBodies[3] = {nullptr, nullptr, nullptr};
    QTextEdit *reportLog = nullptr;
    QPushButton *optimizeBtn = nullptr;
    QPushButton *restoreBtn = nullptr;
};

#endif // OPTIMIZEPAGE_H
