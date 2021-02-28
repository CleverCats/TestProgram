#include <WinSock2.h>
#include <QTcpSocket>
#include <QDebug>
#include "c_crc32.h"
#include "c_global.h"
#include "c_struct.h"
#include "csocket.h"
void connstruct::GetOnetoUse()
{
    issendlen           = 0;
    SnedSlotOverlable   = false;
    CurStat             = _PKG_HEAD_INIT;             //设置新连接为待接包状态[状态机]
    pnewMemoryPointer   = NULL;                       //报文缓存区首地址
    PrecvBuf            = DataHeadInfo;               //每个新的C/S链接,PrecvBuf指向缓存区首地址
    nexTorecvLen        = sizeof(COMM_PKG_HEADER);    //待接收包长[主要用于解决缺包装态]
    lastPingtime        = time(NULL);                 //初始化心跳包最后发送时间
    PrecvBuf            = DataHeadInfo;               //接受缓存区指针,指向待储存缓存区首地

    nexTorecvLen        = sizeof(COMM_PKG_HEADER);    //下次需要接受的字节数
}

CSocket::CSocket(QObject *parent):QObject(parent)
{
    ifopenping      = 1;
    SendRate        = 0;
    SendPkgRate     = 0;
    m_workerscount  = 0;
    m_pkgcount      = 0;
    m_onlineCount   = 0;
    m_shutCount     = 0;
    m_timercount    = 0;
    m_SendQueueSize = 0;
    m_SocketCode    = 0;
    m_port          = 0;
    m_PingInterval  = 10;
    m_MsgHeaderLen  = sizeof(MSG_HEADER);
    m_PkgHeaderLen  = sizeof(COMM_PKG_HEADER);
    m_ip            = nullptr;
    ui              = nullptr;
    m_SendThread    = nullptr;
    m_PingThread    = nullptr;
    m_RecvThread    = nullptr;
    errlisttime     = time(NULL);
}

void CSocket::Init()
{
    InitConnPool();
}

void CSocket::InitSubProcess()
{

    int ithread = m_workerscount/20;
        if(ithread == 0)
            ithread = 1;

    for(int i = 0; i < ithread; ++i)
    {
        m_LogicThreadQueue.push_back(m_LogicThread = new workerthread());
    }

    m_SendThread = new sendthread();
    m_PingThread = new pingthread();
    m_RecvThread = new recvthread();

}

void CSocket::InitConnPool()
{
    int icreat   =  m_workerscount;
    int incount  = 20;
    int ithread  = icreat/incount;
    int imod     = icreat%incount;

    if(ithread == 0)
    {
        std::list<lconn> socketpool;
        for(int j = 0; j < imod; ++j)
        {
            lconn Con  = new conn;
            Con->GetOnetoUse();
            socketpool.push_back(Con);
            m_FreeConnectionPool.push_back(Con);
        }
        m_ConnectionPool.push_back(socketpool);
    }
    else
    {
        for(int i = ithread; i > 0; --i)
        {
            if(i == 1)
                incount+=imod;

            std::list<lconn> socketpool;
            for(int j = 0; j < incount; ++j)
            {
                lconn Con  = new conn;
                Con->GetOnetoUse();
                socketpool.push_back(Con);
                m_FreeConnectionPool.push_back(Con);
            }
            m_ConnectionPool.push_back(socketpool);
        }
    }

    qDebug()<<"ithread:"<<m_ConnectionPool.size()<<"endsize:"<<(--m_ConnectionPool.end())->size()<<endl;
    m_connfreecount = m_conntotalcount = m_FreeConnectionPool.size();

    m_ThreadRun.release();
    qDebug() <<"连接池初始化完成:" << m_conntotalcount << endl;
}

lconn CSocket::RemoveTimerEarlistTime()
{
    if(m_timercount == 0 || m_PingTimerQueue.empty())
        return nullptr;

    lconn Con = m_PingTimerQueue.begin()->second;
    m_PingTimerQueue.erase(m_PingTimerQueue.begin());
    --m_timercount;
    return Con;
}

void CSocket::CheckAndUpadtePingTimerInfo(lconn Con, time_t curtime)
{
     if(Con->_conn != nullptr)
     {
         if(Con->lastPingtime + m_PingInterval < curtime)
         {

         }
         else
         {
             time_t now = time(NULL);
             m_PingTimerQueue.insert(std::make_pair(now, Con));
         }
     }
}

void CSocket::RecyQTcpSocket(QTcpSocket *qsock)
{
    qDebug("OK");
    std::lock_guard<std::mutex>RecyLoock(m_RecyConMutex);
    --g_socket.m_onlineCount;
    ++g_socket.m_shutCount;
    auto itr  = g_socket.m_OnlineQueue.find(qsock);
    lconn Con = itr->second;
    if(Con->_conn != nullptr)
    {
        Con->PutOnetoFree();
        g_socket.m_OnlineQueue.erase(itr);
        qDebug("OK2");
    }
}

char *CSocket::GetSendBuffer(lconn Con)
{
    CCRC32* Get_CRC32 = CCRC32::GetInstance();
    char *emptyBodyPkgBuf   = new char[m_MsgHeaderLen + m_PkgHeaderLen + m_PkgbodySize]();
    char *sendbuffer        = emptyBodyPkgBuf;
    LPMSG_HEADER msgbuffer  = (LPMSG_HEADER)emptyBodyPkgBuf;
    msgbuffer->Con          = Con;

    sendbuffer              = sendbuffer + m_MsgHeaderLen;
    LPCOMM_PKG_HEADER Pkgheader = (LPCOMM_PKG_HEADER)sendbuffer;

    Pkgheader->pkgLen       = htons(m_PkgHeaderLen + m_PkgbodySize);                                        //包长=包头 + 包体
    Pkgheader->msgCode      = htons(7);                                                                     //消息代码7
    char *Pkgbody           = (char *)Pkgheader + m_PkgHeaderLen;
    memcpy(Pkgbody, m_SendData, m_PkgbodySize);
    Pkgheader->crc32        = htonl(Get_CRC32->Get_CRC((unsigned char*)Pkgbody, g_socket.m_PkgbodySize));   //crc32验证码
    //qDebug("Pkgbody:%s ,m_PkgbodySize: %d ", m_SendData, g_socket.m_PkgbodySize);
    return emptyBodyPkgBuf;
}

void  CSocket::InputSendDataQueue(char *databuff)
{
    std::lock_guard<std::mutex>InPutSendQueuLock(m_SendDataQueueMutex);
    m_SendDataQueue.push_back(databuff);
    ++m_SendQueueSize;      //发包项数+1
    m_SendSem.release();    //信号量+1
    //qDebug("m_SendSem.release()");
    //qDebug()<<"m_SendDataQueue.size = "<<m_SendDataQueue.size()<<endl;
}


void  CSocket::SendPingPkg(lconn Con)
{

    char *emptyBodyPkgBuf   = new char[m_MsgHeaderLen + m_PkgHeaderLen]();
    char *sendbuffer        = emptyBodyPkgBuf;
    LPMSG_HEADER msgbuffer  = (LPMSG_HEADER)emptyBodyPkgBuf;
    msgbuffer->Con          = Con;

//    if(msgbuffer->Con->_conn != nullptr)
//        qDebug("ok");

    sendbuffer              = sendbuffer + m_MsgHeaderLen;
    LPCOMM_PKG_HEADER Pkgheader = (LPCOMM_PKG_HEADER)sendbuffer;

    Pkgheader->pkgLen       = htons(m_PkgHeaderLen);       //包长 = 包头长【空包】
    Pkgheader->msgCode      = htons(0);                    //消息代码0
    Pkgheader->crc32        = htonl(0);                    //crc32验证码0

    InputSendDataQueue(emptyBodyPkgBuf);                   //入队列

}

lconn CSocket::GetOneReadyPing(time_t curtime)
{
    if(m_timercount == 0 || m_PingTimerQueue.empty())
        return nullptr;

    UpdateErrlistTime();
    //遍历取当前待检测socket
    if(curtime >= errlisttime)
    {
        lconn Con = RemoveTimerEarlistTime();
        time_t newendtime = time(NULL) + m_PingInterval;
        m_PingTimerQueue.insert(std::make_pair(newendtime, Con));
        ++m_timercount;

        if(m_timercount == 0 || m_PingTimerQueue.empty())
            UpdateErrlistTime();

        return Con;
    }
    return nullptr;
}

void CSocket::UpdateErrlistTime()
{
    if(!m_PingTimerQueue.empty())
        errlisttime =  m_PingTimerQueue.begin()->first;
}
void CSocket::EntablishConnection()
{
    for(int i = 0; i < m_workerscount; ++i)
    {

        lconn Con = GetOneConnection();     //获取new_socket并分配连接内存
        if(Con->_conn != nullptr)
        {
            PutOneToTimerQueue(Con);        //心跳队列
            m_OnlineQueue.insert(std::make_pair(Con->_conn, Con));
            UpdateErrlistTime();            //获取最后socket连接时间
        }
    }
    return;
}
void CSocket::GetInitConifg()
{
    m_workerscount  = ui->s_socketsCount->toPlainText().toUInt();
    m_ip            = ui->s_ip->toPlainText();
    m_port          = ui->s_port->toPlainText().toInt();

    std::string str = ui->s_pkg->toPlainText().toStdString();
    m_SendData      = (char *)str.c_str();
    m_PkgbodySize   = strlen(m_SendData) + 1;
    g_socket.ui->CSStatu->setItem(0, 0, new QTableWidgetItem(g_socket.m_ip));
    g_socket.ui->CSStatu->setItem(0, 1, new QTableWidgetItem(QString::number(g_socket.m_port,10)));
    qDebug("m_SendDataSize:%d ",m_PkgbodySize);
    QThread::usleep(5000);
}

time_t CSocket::GetNowTime()
{
    time_t newt = time(NULL);
    return newt;
}

void CSocket::PutOneToTimerQueue(lconn Con)
{
    std::lock_guard<std::mutex>RefreshLock(m_RefreshTimeMutex);
    --m_connfreecount;
    ++m_onlineCount;
     time_t endtime = time(NULL);                                 //更新时间
     endtime += m_PingInterval;
     m_PingTimerQueue.insert(std::make_pair(endtime, Con));       //入心跳队列
     ++m_timercount;
     //qDebug("Input TimerQueue");
}

void CSocket::PutOneToFreeQueue(lconn Con)
{

}

lconn CSocket::OutToTimerQueue(QTcpSocket *sock)
{/*
    for(auto itr : m_PingTimerQueue)
    {

    }*/
    return nullptr;
}

lconn CSocket::GetOneConnection()
{
    lconn Con;
    std::lock_guard<std::mutex>RefreshPoolLoock(m_RefreshConPoolMutex);

    if(!m_FreeConnectionPool.empty())
    {
        Con = m_FreeConnectionPool.front();
        m_FreeConnectionPool.pop_front();
        Con->GetOnetoUse();
    }
    else
    {

    }

    QTcpSocket *sock = new QTcpSocket();
    QTcpSocket::connect(sock,&QTcpSocket::connected,[](){
        //qDebug("建立连接成功...");
    });
    QTcpSocket::connect(sock,&QTcpSocket::disconnected,[=](){
        if(sock != nullptr)
         g_socket.RecyQTcpSocket(sock);

        //if(Con == nullptr)
        //qDebug("sock:%d 断开连接...",(int)sock);
    });

    sock->connectToHost(m_ip, m_port, QTcpSocket::ReadWrite);
    Con->_conn = sock;
    return Con;
}
