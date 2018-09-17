#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QDebug>

#include "ramdisk.h"

namespace Ui {
class Widget;
}

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_btn_mountDisk_clicked();

    void on_btn_unmountDisk_clicked();

private:
    Ui::Widget *ui;
};

#endif // WIDGET_H
