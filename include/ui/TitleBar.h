#ifndef TITLEBAR_H
#define TITLEBAR_H

#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

// Кастомный топбар для frameless-окна: перетаскивание, минимизация,
// максимизация/восстановление и закрытие.
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    // Обновляет иконку maximize/restore после изменения состояния окна.
    void setWindowMaximized(bool maximized);

signals:
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void startDrag(const QPoint &globalPos);
    void moveDrag(const QPoint &globalPos);

    QLabel *logoLabel = nullptr;
    QLabel *titleLabel = nullptr;
    QLabel *subtitleLabel = nullptr;
    QPushButton *minBtn = nullptr;
    QPushButton *maxBtn = nullptr;
    QPushButton *closeBtn = nullptr;

    bool dragging = false;
    QPoint dragOffset;
};

#endif // TITLEBAR_H
