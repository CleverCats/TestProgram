#include "c_global.h"
#include "workerthread.h"
workerthread::workerthread()
{

}
void workerthread::run()
{
    std::string  str   = g_socket.ui->s_pkg->toPlainText().toStdString();
    g_socket.m_SendData     = (char *)str.c_str();

    std::list<std::list<lconn>>::iterator itr = g_socket.m_ConnectionPool.begin();
    g_socket.m_ThreadRun.acquire();
    int code = g_socket.m_SocketCode;

    qDebug("workerthread running");
    for(int i = 0; i < code; ++i)
        ++itr;

    ++g_socket.m_SocketCode;
    g_socket.m_ThreadRun.release();

    std::list<lconn> Sockpool = *itr;
    while(!g_shutdown)
    {
        //usleep(5000);
        for(auto Con: Sockpool)
        {
            char *DataBuf = g_socket.GetSendBuffer(Con);
            g_socket.InputSendDataQueue(DataBuf);

        }
    }
}
