#ifndef _NGX_C_SOCKET_H_
#define _NGX_C_SOCKET_H_

#include <map>
#include <list>
#include <vector>
#include <stdint.h>
#include <ngx_macro.h>
#include <errno.h>
#include <pthread.h>
#include <atomic>
#include <semaphore.h> //sem_t
#include <sys/epoll.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include "ngx_comm.h"  //包头
#include "ngx_pthread_mutex.h"
#include "ngx_c_cstruct.h"

//已完成连接队列
typedef class  CSocket CSocket;
typedef struct ngx_listening_s ngx_listening_type,*ngx_listening_ptr;
typedef struct ngx_connection_s ngx_connection_type,*ngx_connection_ptr;
typedef void(CSocket::*ngx_event_handler_pt)(ngx_connection_ptr c);     //成员函数指针

#pragma pack(1)
/*[消息头]【用于记录数据包接入后的额外信息】*/
typedef struct _MSG_HEADER{
    ngx_connection_ptr pConn;         //记录对应连接
    uint64_t           iCurrsequence; //收到数据包后记录序号,处理完信号后与源序号对比,判断是否是过期包
}MSG_HEADER , *LPMSG_HEADER;
#pragma pack()  

//监听套接字节点
struct ngx_listening_s      //监听端口有关结构体【包含监听端口和对应的套接字】
{
    int                     port;        //服务器正在监听的端口号
    int                     fd;          //监听套接字
    ngx_connection_ptr      connection;  //记录连接池中的对应对象
};

//连接池结点
struct ngx_connection_s{
    ngx_connection_s();                        //构造
    virtual ~ngx_connection_s();               //析构
    void GetOneToUse();
    void PutOneToFree();

    
    int                     fd;
    unsigned                instance:1;        //【位域】只占1位【二进制】,原本无符号整形是占32位二进制,但是只有0和1的数值,1位就够了
    uint64_t                iCurrsequence;     //用于校检是否是当前socket是否是过期连接
    ngx_listening_ptr       listening;         //记录监听套接字对象结构体【struct ngx_listening_s】
    //ngx_connection_ptr    data;              //链【next指针】
    ngx_event_handler_pt    rhandler;          //函数指针【读事件相关】
    ngx_event_handler_pt    whandler;          //函数指针【写事件相关】
    struct sockaddr         s_sockaddr;        //保存对方地址信息用的
    
    //通讯相关
    uint32_t                events;                            //事件类型
    unsigned int            CurStat;                           //当前收包状态
    char                    DataHeadInfo[_DATA_BUFFER_SIZE];   //储存包头字节
    char                    *PrecvBuf;                         //接受缓存区指针,指向待储存缓存区首地址
    char                    *PsendBuf;                         //发送缓存区指针,指向待发缓存区首地址
    
    unsigned int            nexTosendlen;                      //下次需要发送的字节数
    unsigned int            nexTorecvLen;                      //下次需要接受的字节数
    bool                    isnewMemory;                       //是否为客户端报文开辟缓存区
    char                    *pnewMemoryPointer;                //指向收报文缓存区首地址【消息头 + 包头 +包体】【用于释放】
    char                    *psendMemoryPointer;               //指向发报文缓存区首地址【消息头 + 包头 +包体】【用于释放】
    pthread_mutex_t         logiclock;                         //同一个客户端socket的业务逻辑锁
    time_t                  curRecyTime;                       //记录当前延迟回收时间
    bool                    ifsendbyepoll;                     //是否通过epoll驱动发送【false不是,true是】
    time_t                  lastPingtime;                      //最后一次心跳包的接收时间
    bool                    ifInDelayRecyQueue;                //是否入延迟回收池
    uint64_t                FloodTestLastTime;                 //上次Flood检测的时间
    unsigned short          FloodAttackCount;                  //Flood攻击在m_FloodTimeInterval间收到的次数
    std::atomic<int>        iWaitToSendPkgCount;               //待发包数(客户端待接收)【积压一定数量判定客户端恶意拒收数据包】
};

//socket相关类【抽象】
class CSocket
{
public:
    CSocket();                              //构造函数
    virtual ~CSocket();                     //析构函数
    virtual bool Initialize();              //初始化函数【封装GetConfInfo获取配置 ngx_open_listening_sockets()开启监听端口】
    virtual void ThreadRecvProcFunc(char *pMsgBuffer) = 0;                 //处理客户端需求【纯虚函数,由子类重写实现】
    virtual bool Initialize_subproc();                                     //初始化子线程相关条件,并创建子线程
    virtual void shoutdown_subproc();                                      //回收子线程【非业务逻辑线程池】
    virtual void procPingTimeOutChecking(LPMSG_HEADER pMsgHeader , time_t cur_time);   //检测是否接收到心跳包
public:
    int  ngx_epoll_init();                                                 //初始化epoll对象连接池【封装ngx_initconnection】
    void ngx_initconnection();                                             //动态创建连接池
    int  ngx_epoll_process_events(int timer);                              //事件处理
    ngx_connection_ptr ngx_get_connection(int isock);                      //用于连接池分配空闲链接结点
    void ngx_clear_connectionPool();                                       //回收连接池
    void RemoveFromTimerAndShutConn(ngx_connection_ptr _pConn);            //剔除超时连接
    void MonitoringAndOutPutServerInfo();                                  //监视并输出服务器信息
private:
    ssize_t recvproc(ngx_connection_ptr c , char *buffer,ssize_t buflen);  //封装recv,用于收包,返回以接受到的包长
private:
    bool ngx_open_listening_sockets();                        //监听必须的端口【支持多个端口】
    void ngx_close_listening_sockets();                       //关闭监听套接字
    bool setnonblocking(int sockfd);                          //设置非阻塞套接字
    void ngx_event_accept(ngx_connection_ptr oldc);           //监听套接字的读事件,用来接收TCP连接事件
public:
    // int  ngx_epoll_add_event(int fd,                          //epoll对象描述符
    //                          int readevent,int writeevent,    //读权限 , 写权限
    //                          uint32_t otherflag,              //其他标志 0/1
    //                          uint32_t eventtype,              //事件类型
    //                          ngx_connection_ptr c);           //连接池中的一个客户端

    /*替代ngx_epoll_add_event 因为项目中采用的是LT触发,EPOLL中默认即LT模式,所以这里不需要额外设置参数*/
    int  ngx_epoll_Regist_event(int fd,                       //epoll对象描述符
                             uint32_t eventtype,              //事件类型
                             uint32_t flag,                   //其他标志
                             int      otheraction,            //补充动作,补充falg的不足
                             ngx_connection_ptr pConn);       //连接池中的一个客户端
public:
    void  GetConfInfo();                                            //获取epoll配置信息
    void  ngx_free_connection(ngx_connection_ptr pConn);            //立即回收【立即扔到空闲列表】
    void  ngx_close_connection(ngx_connection_ptr pConn);           //封装ngx_free_connection用于立即回收连接池结点
    void  ngx_delay_close_connection(ngx_connection_ptr pConn);     //延迟回收
    void  ngx_inFreeConnectionPool(ngx_connection_ptr pConn);       //入空闲队列
    bool  ngx_judgeIfConnConnecting(char *msgPkg);                  //判断连接是否连接中
public:
    //void  tmpoutMsgRecvQueue();                                   //当消息队列项 > 1000 释放500项 , 保证消息队列数 <= 1000
    void  clearMsgRecvQueue();                                      //服务器关闭时清除残余消息队列内存

//收包
    void  ngx_read_request_handler_prc1(ngx_connection_ptr pConn);              //接受包头
    void  ngx_wait_recv_pkgbody_prc2(ngx_connection_ptr pConn, bool &isflood);  //预处理包体
    void  ngx_put_handle_cinfo(ngx_connection_ptr pConn, bool &isflood);        //入消息队列并通知线程池处理数据
//发包
    void  ngx_write_surplus_pkg_proc(ngx_connection_ptr pConn);                 //发包(一般是还未发完完整的包)【epoll事件驱动调用】
    void  ngx_put_sendMsgQueue(char *SendData);                                 //入发消息队列
    void  SendNoBodyPkgtoClient(LPMSG_HEADER _MsgHeadle , unsigned short _msgcode);//发送空包
    ssize_t sendproc(ngx_connection_ptr c ,char* buff ,ssize_t buflen);         //发包【发包线程主动调用】 
//心跳包
    void  AddtoPingTimerQueue(ngx_connection_ptr pConn);                                //入心跳监队列             
    time_t GetTimerEarlistTime();                                                       //获取最早需要判断的心跳包时长
    LPMSG_HEADER GetOverTimeTimer(time_t curtime);                                      //获取超时发送心跳包的连接
    LPMSG_HEADER RemoveTimerEarlistTime();                                              //删除FirstNode【earlist time】
//Flood攻击检测
    bool SocketFloodTest(ngx_connection_ptr pConn);
private:
/*静态成员函数:相当于一个加了访问权限的全局函数,不属于类但需要通过类或者类对象来访问
因为不属于类,而普通成员函数【参数中隐藏了this指针形参】调用成员变量或者函数其实是this->obj,
而不属于类的静态函数不会存在this指针,所以不能直接调用类成员*/
//延迟回收线程入口
    static void *SeverRecyConnectionThreadFun(void *ThreadData);
//数据发送线程入口
    static void *SeverSendQueueThreadFun(void *ThreadData);
//心跳包线程入口
    static void *SeverMonitorPingPkgFun(void *ThreadData);
private:
    struct ThreadItem   
    {
        pthread_t   _Handle;                                              //线程句柄
        CSocket     *_pThis;                                              //记录类指针【父类指针指向子类对象 CSocket *_pThis = &CLogicSocketobj】	
        bool        ifrunning;                                            //标记是否正式启动起来，启动起来后，才允许调用StopAll()来释放
        
        //构造函数
        ThreadItem(CSocket *pthis):_pThis(pthis),ifrunning(false){};                            
        //析构函数
        ~ThreadItem(){};       
    };
private:

    std::vector<ngx_listening_ptr>  m_ListenSocketList;        //储存包含监听套接字与监听端口结构体数组
    int                             m_ListenPortCount;         //所监听端口的数量
    int                             m_workConnections;         //epoll允许接入的最大客户端数量
    int                             m_epollhandle;             //epoll对象描述符
    int                             m_connections;             //当前进程中所有连接对象总数【连接池大小】
    //ngx_connection_ptr            m_pconnections;            //指向连接池【结构体数组】
    //ngx_connection_ptr            m_pfree_connection;        //始终指向一个空闲结点
    //【关于多线程的容器操作为了线程安全一般都要加锁】
    std::list<ngx_connection_ptr>   m_connectpoollist;         //连接池列表【初始连接池】
    std::list<ngx_connection_ptr>   m_freeconnectpoollist;     //待分配连接列表【空闲连接池】
    std::list<ngx_connection_ptr>   m_delayRecyConnectlist;    //延迟回收列表
    //std::list<ngx_connection_ptr>   m_rateLimitingUserList;    //限速队列
    std::vector<ThreadItem *>       m_subThreadVector;         //子线程队列【发包/延迟回收】
    int                             m_free_connections_num;    //空闲结点数
    std::atomic<int>                m_delayRecyConnum;         //延迟回收队列项数                       
    struct  epoll_event             m_events[NGX_MAX_EVENTS];  //epoll_wait获取到的事件
    pthread_mutex_t                 m_allocConMuext;           //连接池连接 分配/(立即)释放锁
    pthread_mutex_t                 m_delayRecyConMutex;       //延迟回收锁
    std::list<char *>               m_sendDatatQueue;          //发送数据队列
    pthread_mutex_t                 m_insendQueueMutex;        //入发消息队列锁
//读取配置文件中延迟回收时间信息
	time_t                          m_RecyConnectionWaitTime;  //延迟回收时间间隔
public:
    int                             m_MsgHeaderLen;            //消息头长度             
    int                             m_PkgHeaderLen;            //包头长度
//发包相关
    std::atomic<int>                m_sendMsgnum;              //m_sendDatatQueue.size()
    sem_t                           m_semEventSendQueue;       //信号量
protected:
//心跳包相关
    time_t                          m_curtime;                 //当前时间
    time_t                          m_TimerEarlisttime;        //储存定时器当前最小值【begin】
    int                             m_cur_timerQueueSize;      //定时器队列项数
    int                             m_OpenPingTimerEnable;     //是否开启心跳包 1:开启 0:关闭
    int                             m_MaxWaitePingtime;        //心跳最大等待时间
    int                             m_MaxOverTime;             //心跳超时断开时间
    pthread_mutex_t                 m_RefreshTimeMutex;        //定时器刷新锁
    std::multimap<time_t , LPMSG_HEADER>  m_PingTimerQueue;    //心跳包定时器
    int                             m_kickOverUserOnTime;      //到时直接踢出超时连接 
//用户在线人数
    std::atomic<int>                m_CurOnlinUserCount;       //在线人数
    //std::atomic<int>              m_CurRateLimitingUserCount;//限速人数
//Flood攻击
    int                             m_FloodAttackickEnable;    //Flood攻击检测是否开启 1:开启 0:关闭【每次收包完整时检测】
    unsigned int                    m_FloodTimeInterval;       //Flood攻击触发事件间隔
    unsigned short                  m_FloodCounttoKick;        //Flood攻击检测FlooCount达到m_FloodCounttoKick认定为Flood攻击
//服务器状态相关
    time_t                          m_lastoutputinfotime;      //上次打印时间
//网络安全相关
    int                             m_AbandonPkgCount;         //丢弃的待发送数据包数量【发消息队列积压超过一定数量,为了服务器稳定,主动丢包】
    int                             m_RefuseAcceptPkgEnable;   //拒收包恶意用户检测
};


#endif