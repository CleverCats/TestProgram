#ifndef WIDGET_H
#define WIDGET_H
#include <QWidget>
#include "ui_widget.h"
#pragma warning (disable:4819)
QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE
class Widget :public QWidget
{
    Q_OBJECT
public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
public:
    void Init();
    void InitSignalAndSlots();
    void InitMenu();
public:
    Ui::Widget *ui;
private slots:
    void BegintoEntablish(bool checked);
    void BegintoSend(bool checked);
    void ShutDownCon(bool checked);          //断开所有连接
    void Show();

};
#endif // WIDGET_H

