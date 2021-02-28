#include "widget.h"
#include <QApplication>
#include "clogicsocket.h"
bool          g_shutdown = false;
CLogicSocket  g_socket;
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Widget w;
    w.Init();
    w.show();
    MyTimer     m_ReflashStatu;
    return a.exec();
}
