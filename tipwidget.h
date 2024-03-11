#ifndef TIPWIDGET_H
#define TIPWIDGET_H

#include <QWidget>
namespace Ui {
class TipWidget;
}

class TipWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TipWidget(QWidget *parent = nullptr);
    ~TipWidget();

    void setColor(const QColor& brushColor, const QColor& penColor);
    void setNormalStyle(void);
    void setFailedStyle(void);
    void showNormalStyle(void);
    void showFailedStyle(void);
private:
    void moveCenter(const QPoint& pos);
    void traceCursor();

private:
    Ui::TipWidget *ui;

    QTimer* timer_cursor = nullptr;
    QColor brushColor = Qt::red;
    QColor penColor = Qt::black;

    // QWidget interface
protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void showEvent(QShowEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
};

#endif // TIPWIDGET_H
