#include "tipwidget.h"
#include "ui_tipwidget.h"
#include <QPainter>
#include <QTimer>
#include <QDebug>

TipWidget::TipWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TipWidget)
{
    ui->setupUi(this);
    setWindowTitle("DogPowTip");
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool); //Qt::Tool弹出时不会抢占焦点
    setWindowFlag(Qt::WindowTransparentForInput); //鼠标穿透
    setAttribute(Qt::WA_TranslucentBackground);
    resize(45, 45);

    timer_cursor = new QTimer(this);
    timer_cursor->setInterval(10);
    timer_cursor->callOnTimeout(this, &TipWidget::traceCursor);

    setNormalStyle();
}

TipWidget::~TipWidget()
{
    delete ui;
}

void TipWidget::setColor(const QColor& brushColor, const QColor& penColor)
{
    this->brushColor = brushColor;
    this->penColor = penColor;
    update();
}

void TipWidget::setNormalStyle()
{
    setColor(Qt::red, Qt::black);
    update();
}

void TipWidget::setFailedStyle()
{
    setColor(Qt::black, Qt::red);
    update();
}

void TipWidget::showNormalStyle()
{
    setNormalStyle();
    this->show();
}

void TipWidget::showFailedStyle()
{
    setFailedStyle();
    this->show();
}

void TipWidget::moveCenter(const QPoint& pos)
{
    int x = pos.x() - this->width() / 2;
    int y = pos.y() - this->height() / 2;
    this->move(x, y);
}

void TipWidget::traceCursor()
{
    this->moveCenter(QCursor::pos() + QPoint(14, -5)); //TODO High DPI support
}

void TipWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this); // 创建QPainter对象，并将其绑定到当前窗口
    painter.setRenderHint(QPainter::Antialiasing); // 设置抗锯齿，让圆更平滑

    painter.setPen(penColor);
    painter.setBrush(QBrush(brushColor));

    int width = this->width();
    int height = this->height();

    int centerX = width / 2;
    int centerY = height / 2;

    int radius = 4;

    painter.drawEllipse(centerX - radius, centerY - radius, radius * 2, radius * 2);
}

void TipWidget::showEvent(QShowEvent*)
{
    timer_cursor->start();
    this->traceCursor(); //立即更新，因为QTimer的第一次触发有延迟
}

void TipWidget::hideEvent(QHideEvent*)
{
    timer_cursor->stop();
}
