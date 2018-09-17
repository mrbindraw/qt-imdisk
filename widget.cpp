#include "widget.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget)
{
    ui->setupUi(this);

    qDebug() << Q_FUNC_INFO;
    CRamDisk::getInstance()->init();
}

void Widget::on_btn_mountDisk_clicked()
{
    if(CRamDisk::getInstance()->wasMounted())
        return;

    qDebug() << Q_FUNC_INFO;

    CRamDisk::getInstance()->mount();
}

void Widget::on_btn_unmountDisk_clicked()
{
    qDebug() << Q_FUNC_INFO;

    CRamDisk::getInstance()->unmount();
}

Widget::~Widget()
{
    qDebug() << Q_FUNC_INFO;

    CRamDisk::destroyInstance();
    delete ui;
}
