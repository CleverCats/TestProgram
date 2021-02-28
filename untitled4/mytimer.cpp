#include <QDebug>
#include <QTimerEvent>
#include "c_global.h"
#include "mytimer.h"

#define TIMER_TIMEOUT   (5*100)

MyTimer::MyTimer(QObject *parent):QObject(parent)
{
    m_nTimerID = this->startTimer(TIMER_TIMEOUT);
}

MyTimer::~MyTimer()
{
    if (m_nTimerID != 0 )
        killTimer(m_nTimerID);
}

void MyTimer::timerEvent(QTimerEvent *event)
{
    if(event->timerId() == m_nTimerID){
        handleTimeout();
    }
}

void MyTimer::handleTimeout()
{
    g_socket.ui->CSStatu->setItem(2, 0, new QTableWidgetItem(QString::number(g_socket.m_onlineCount,10)));
    g_socket.ui->CSStatu->setItem(2, 1, new QTableWidgetItem(QString::number(g_socket.m_shutCount,10)));
    g_socket.ui->CSStatu->setItem(4, 0, new QTableWidgetItem(QString::number(g_socket.m_pkgcount,10)));
    g_socket.ui->CSStatu->setItem(4, 1, new QTableWidgetItem(QString::number(0)));
    g_socket.ui->CSStatu->setItem(4, 2, new QTableWidgetItem(QString::number(g_socket.SendPkgRate,10) + " pcs/s"));
    if(IfResetRate == false)
    {
        g_socket.ui->CSStatu->setItem(2, 2, new QTableWidgetItem(QString::number(g_socket.SendRate,10) + " kb/s"));
        g_socket.SendRate    = 0;
        g_socket.SendPkgRate = 0;
        IfResetRate          = false;
    }
    else
    {
        IfResetRate = true;
    }

    //qDebug()<<"Enter timeout processing function\n";
}
