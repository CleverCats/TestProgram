#ifndef CSOCKET_H
#define CSOCKET_H
#include <QTcpSocket>
#include <QSemaphore>
#include "mytimer.h"
#include "c_macro.h"
#include "sendthread.h"
#include "workerthread.h"
#include "pingthread.h"
#include "recvthread.h"
#include "widget.h"
#pragma warning (disable:4819)
typedef struct connstruct{
public:
    connstruct():_conn(nullptr){};
    void GetOnetoUse();
    void PutOnetoFree()
    {
        if(pnewMemoryPointer != NULL)
          {
              delete []pnewMemoryPointer;
              pnewMemoryPointer = NULL;
              isnewMemory       = false;
          }
    }
    QTcpSocket *_conn;
    time_t  lastPingtime;
    std::mutex logiclock;
    bool                    SnedSlotOverlable;                 //发包槽函数结束标识 false:未结束 true:结束
    unsigned int            CurStat;                           //当前收包状态
    char                    DataHeadInfo[_DATA_BUFFER_SIZE];   //储存包头字节
    char                    *PrecvBuf;                         //接受缓存区指针,指向待储存缓存区首地址
    char                    *PsendBuf;                         //发送缓存区指针,指向待发缓存区首地址

    int                     issendlen;                         //已发送字节数
    unsigned int            nexTosendlen;                      //下次需要发送的字节数
    unsigned int            nexTorecvLen;                      //下次需要接受的字节数
    bool                    isnewMemory;                       //是否为客户端报文开辟缓存区
    char                    *pnewMemoryPointer;                //指向收报文缓存区首地址【消息头 + 包头 +包体】【用于释放】
    char                    *psendMemoryPointer;               //指向发报文缓存区首地址【消息头 + 包头 +包体】【用于释放】

}conn,*lconn;;
class CSocket :public QObject
{
     Q_OBJECT
public:
    CSocket(QObject *parent = nullptr);
    ~CSocket(){};
public:
    void        GetInitConifg();                                        //获取配置项
    void        Init();                                                 //封装InitConnPool,InitSubProcess
    void        InitConnPool();                                         //初始化连接池
    void        InitSubProcess();                                       //初始化线程
    lconn       GetOneConnection();                                     //获取一个连接
    void        PutOneToTimerQueue(lconn Con);                          //入监听队列
    time_t      GetNowTime();                                           //更新时间
    void        UpdateErrlistTime();                                    //更新最早ping时间
    void        EntablishConnection();                                  //建立连接
    lconn       GetOneReadyPing(time_t curtime);                        //取出即将心跳连接
    lconn       RemoveTimerEarlistTime();                               //移除待检测连接
    lconn       OutToTimerQueue(QTcpSocket *sock);                      //移除Timer队列
    void        PutOneToFreeQueue(lconn Con);                           //入空闲队列
    char        *GetSendBuffer(lconn Con);                              //获取代发消息文本
    void        InputSendDataQueue(char *databuff);                     //入发消息队列
    void        SendPingPkg(lconn Con);                                 //发送心跳包
    void        CheckAndUpadtePingTimerInfo(lconn Con, time_t curtime); //检查更新Timer队列
    void        RecyQTcpSocket(QTcpSocket *qsock);                      //处理断开回收连接
public:
    QString     m_ip;
    int         m_port;
    int         m_connfreecount;                        //空闲池大小
    int         m_conntotalcount;                       //连接池大小
    int         m_timercount;                           //心跳队列大小
    time_t      errlisttime;                            //当前定时器最小时间
    Ui::Widget  *ui;                                    //ui
    int m_workerscount;                                 //逻辑业务线程数
    int m_SendQueueSize;                                //发包队列大小
    std::atomic<int> m_onlineCount;                     //在线人数
    std::atomic<int> m_shutCount;                       //断线人数
public:
    workerthread *m_LogicThread;                        //发包线程
    sendthread   *m_SendThread;                         //发包线程
    pingthread   *m_PingThread;                         //心跳线程
    recvthread   *m_RecvThread;                         //收包线程
public:
    std::list<std::list<lconn>> m_ConnectionPool;       //连接池

    std::list<lconn> m_FreeConnectionPool;              //空闲池
    std::list<char*> m_SendDataQueue;                   //发消息队列
    std::vector<workerthread*> m_LogicThreadQueue;      //逻辑业务线程
    std::multimap<time_t, lconn>  m_PingTimerQueue;     //心跳包定时器
    std::map<QTcpSocket* ,lconn>  m_OnlineQueue;        //在线队列(用于寻找lconn进行回收)
public:
    std::mutex m_RefreshConPoolMutex;                   //连接池刷新锁
    std::mutex m_RefreshTimeMutex;                      //定时器刷新锁
    std::mutex m_RecyConMutex;                          //回收锁
    std::mutex m_SendDataQueueMutex;                    //发消息队列锁
    QSemaphore                      m_SendSem;          //发包信号量
    QSemaphore                      m_ThreadRun;        //线程启动信号
    int                             m_PingInterval;     //心跳间隔时间
    int                             m_MsgHeaderLen;     //消息头长度
    int                             m_PkgHeaderLen;     //包头长度
    char                            *m_SendData;        //发包文本
    int                             m_PkgbodySize;      //包体长
    int                             m_SocketCode;       //socket队列识别码
    int                             ifopenping;         //是否开启心跳包 1:开启 0关闭 默认关闭
    int                             SendRate;           //传输速率(kb/s)
    int                             SendPkgRate;        //发包速率(个/s)
    int                             m_pkgcount;         //发包数
};
#endif // CSOCKET_H
