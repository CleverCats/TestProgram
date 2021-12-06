#include <pthread.h>
#include <fcntl.h>     //open
#include <unistd.h>
#include <errno.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <sys/epoll.h>
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include <sys/socket.h>
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_comm.h"
#include "ngx_pthread_mutex.h"
#include "ngx_c_memory.h"
#include "ngx_c_socket.h"

//关闭问题套接字
void CloseErrorSock(int waitclose,int SockStatu[])
{
    for(int i = 0 ;i <= waitclose ;i++)
    {
        if(SockStatu[i] != 0)
        {
            ngx_log_error_core(NGX_LOG_INFO,0,"关闭客户端套接字 SockStatu[i]= %d\n",SockStatu[i]);
            close(SockStatu[i]);//关闭套接字
        }
    }
    return;
}


CSocket::CSocket()
{
  
    m_PkgHeaderLen      = sizeof(COMM_PKG_HEADER);     //包头长度
    m_MsgHeaderLen      = sizeof(MSG_HEADER);          //消息头长度
   
    m_FloodAttackickEnable = 0;                             //Flood攻击检测【默认关闭】
    m_FloodTimeInterval    = 100;                           //Flood攻击检测触发时间区间
    m_kickOverUserOnTime   = 0;                             //延时心跳回收开关
    //m_pconnections      = NULL;
    //m_pfree_connection  = NULL;

    m_CurOnlinUserCount      = 0;                           //当前在线人数
    m_lastoutputinfotime     = time(NULL);
    m_AbandonPkgCount        = 0;                           //丢弃的待发送数据包
    m_sendMsgnum             = 0;
    m_free_connections_num   = 0;
    m_delayRecyConnum        = 0;                           //初始延迟回收队列项等于0
    m_cur_timerQueueSize     = 0;                           //初始定时器队列项等于0
    m_workConnections        = 1024;                        //默认允许最大TCP连接数1000
    m_ListenPortCount        = 1;                           //默认监听一个端口
    m_RecyConnectionWaitTime = 60;                          //默认延迟回收60s间隔
    m_OpenPingTimerEnable    = 1;                           //默认打开心跳监视器
    m_MaxWaitePingtime       = 10;                          //心跳检测间隔s
    m_cur_timerQueueSize     = 0;                           //当前Timer队列项数
    m_FloodCounttoKick       = 10;
    m_MaxOverTime            = m_MaxWaitePingtime * 3 + 10;
    m_RefuseAcceptPkgEnable  = 1;                           //拒收包用户检测,默认开启
    //m_CurRateLimitingUserCount = 0;                         //当前限速人数
    return;
};
CSocket::~CSocket()
{
    //释放结构体指针
    std::vector<ngx_listening_ptr>::iterator pos;
    for(pos = m_ListenSocketList.begin();pos != m_ListenSocketList.end();++pos)
    {
        delete (*pos);
    }//end for
    m_ListenSocketList.clear();     //NULL

    return;
};

bool CSocket::Initialize_subproc()
{

    if(pthread_mutex_init(&m_RefreshTimeMutex,NULL) != 0)
    {
        ngx_log_stderr(errno,"CSocket::Initialize_subproc() pthread_mutex_init(&m_RefreshTimeMutex)失败");
        return false;
    }

    if(pthread_mutex_init(&m_allocConMuext,NULL) != 0)         //初始化线程池连接分配互斥量【参数2:NULL表示普通锁】
    {
        ngx_log_stderr(errno,"CSocket::Initialize_subproc() pthread_mutex_init(&m_allocConMuext)失败");
        return false;
    }
    if(pthread_mutex_init(&m_delayRecyConMutex,NULL) != 0)
    {
        ngx_log_stderr(errno,"CSocket::Initialize_subproc() pthread_mutex_init(&m_delayRecyConMutex)失败");
        return false;
    }

    if(pthread_mutex_init(&m_insendQueueMutex,NULL) != 0)
    {
         ngx_log_stderr(errno,"CSocket::Initialize_subproc() pthread_mutex_init(&m_insendQueueMutex)失败");
        return false;
    }

    /*互斥锁的属性在创建锁的时候指定，在LinuxThreads实现中仅有一个锁类型属性，不同的锁类型在试图对一个已经被锁定的互斥锁加锁时表现不同。当前（glibc2.2.3,linuxthreads0.9）有四个值可供选择：
    * PTHREAD_MUTEX_TIMED_NP，这是缺省值，也就是普通锁。当一个线程加锁以后，其余请求锁的线程将形成一个等待队列，并在解锁后按优先级获得锁。这种锁策略保证了资源分配的公平性。
    * PTHREAD_MUTEX_RECURSIVE_NP，嵌套锁，允许同一个线程对同一个锁成功获得多次，并通过多次unlock解锁。如果是不同线程请求，则在加锁线程解锁时重新竞争。
    * PTHREAD_MUTEX_ERRORCHECK_NP，检错锁，如果同一个线程请求同一个锁，则返回EDEADLK，否则与PTHREAD_MUTEX_TIMED_NP类型动作相同。这样保证当不允许多次加锁时不出现最简单情况下的死锁。
    * PTHREAD_MUTEX_ADAPTIVE_NP，适应锁，动作最简单的锁类型，仅等待解锁后重新竞争。*/




   /*初始化信号量
   参数一:信号量地址
   参数二:信号量共享方式 0:线程间共享 非0进程间共享
   参数三:信号量初始值: 这里设置0,即可以看作 m_semEventSendQueue = 0
   sem_wait(&m_semEventSendQueue)时如果m_semEventSendQueue > 0那么sem_wait函数立即返回,并且使m_semEventSendQueue - 1,
        如果m_semEventSendQueue = 0,那么线程在这里沉睡,直到m_semEventSendQueue > 0,然后立即返回并且使m_semEventSendQueue - 1.
   sem_post(m_semEventSendQueue),使信号量 +1
   sem_destroy(&m_semEventSendQueue)释放信号量占用相关的内存*/
   if(sem_init(&m_semEventSendQueue,0,0) != 0)
   {
        ngx_log_stderr(errno,"CSocket::Initialize_subproc() sem_init(&m_semEventSendQueue)失败");
        return false;
   }

    int err;
    ThreadItem *pRecyConn;
    m_subThreadVector.push_back(pRecyConn = new ThreadItem(this));
    err = pthread_create(&pRecyConn->_Handle,NULL,SeverRecyConnectionThreadFun,(void *)pRecyConn);
    if(err != 0)
    {
        ngx_log_error_core(NGX_LOG_INFO,errno,"CSocket::Initialize_subproc(),pthread_create(SeverRecyConnectionThreadFun)失败");
        return false;
    }
    
    ThreadItem *pSendConn;
    m_subThreadVector.push_back(pSendConn = new ThreadItem(this));
    err = pthread_create(&pSendConn->_Handle,NULL,SeverSendQueueThreadFun,(void *)pSendConn);
    if(err != 0)
    {
        ngx_log_error_core(NGX_LOG_INFO,errno,"CSocket::Initialize_subproc(),pthread_create(SeverSendQueueThreadFun)失败");
        return false;
    }

    if(m_OpenPingTimerEnable == 1)
    {
        ThreadItem *pMonitorPingConn;
        m_subThreadVector.push_back(pMonitorPingConn = new ThreadItem(this));
        err = pthread_create(&pMonitorPingConn->_Handle,NULL,SeverMonitorPingPkgFun,(void *)pMonitorPingConn);
        if(err != 0)
        {
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocket::Initialize_subproc(),pthread_create(SeverMonitorPingPkgFun)失败");
            return false;
        }
    }
    return true;
}

//读取监听端口数与epoll允许最大TCP连接数
void CSocket::GetConfInfo()
{
    CConfig *pconf         = CConfig::GetInstance();
    m_ListenPortCount      = pconf->GetIntDefault("ListenPortCount",m_ListenPortCount);
    m_workConnections      = pconf->GetIntDefault("worker_connections",m_workConnections);
    m_RecyConnectionWaitTime = (time_t)pconf->GetIntDefault("Sock_RecyConnectionWaitTime",m_RecyConnectionWaitTime);
    m_OpenPingTimerEnable  = pconf->GetIntDefault("Sock_PingTimerEnable",m_OpenPingTimerEnable);
    m_MaxWaitePingtime     = pconf->GetIntDefault("Sock_MaxWaitPingTime",m_MaxWaitePingtime);
    m_FloodAttackickEnable = pconf->GetIntDefault("Sock_FloodAttackKickEnable",m_FloodAttackickEnable);
    m_FloodTimeInterval    = pconf->GetIntDefault("Sock_FloodTimeInterval",m_FloodTimeInterval);
    m_FloodCounttoKick     = pconf->GetIntDefault("Sock_FloodKickCounter",m_FloodCounttoKick);
    m_kickOverUserOnTime   = pconf->GetIntDefault("Sock_KickOverUserOnTime",m_kickOverUserOnTime);
    m_RefuseAcceptPkgEnable = pconf->GetIntDefault("Sock_RefuseAcceptPkgEnable",m_RefuseAcceptPkgEnable);
    m_MaxOverTime = m_MaxOverTime < m_RecyConnectionWaitTime ? m_MaxOverTime : (m_RecyConnectionWaitTime/4*3);
    return;
}

//初始化函数,调用ngx_open_listening_sockets()监听端口【fork子进程之前调用】
bool CSocket::Initialize()
{
    bool reco = ngx_open_listening_sockets();  

    if(m_FloodAttackickEnable == 1)
             ngx_log_error_core(NGX_LOG_INFO,0,"Flood攻击检测开启");

    if(m_RefuseAcceptPkgEnable == 1)
            ngx_log_error_core(NGX_LOG_INFO,0,"拒收包攻击检测开启");

    if(m_OpenPingTimerEnable == 1)
            ngx_log_error_core(NGX_LOG_INFO,0,"心跳监视器开启");

    return reco;
}


//设置socket连接为非阻塞模式【这种函数的写法很固定】：非阻塞，概念在五章四节讲解的非常清楚【不断调用，不断调用这种：拷贝数据的时候是阻塞的】
bool CSocket::setnonblocking(int sockfd) 
{    
    int nb = 1; //0：清除，1：设置  
    if(ioctl(sockfd, FIONBIO, &nb) == -1) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
    {
        return false;
    }
    return true;
}

//监听端口【支持多个端口】【在创建worker进程之前就要调用这个函数】
bool CSocket::ngx_open_listening_sockets()
{
    
    int                isock;                                //socket
    struct sockaddr_in serv_addr;                            //服务器的地址结构体
    int                iport;                                //端口
    char               strinfo[100];                         //临时字符串
    int                OverSock[m_ListenPortCount] = {0};    //储存套接字用于关闭

    memset(&serv_addr,0,sizeof(serv_addr));

    //设置本服务器要监听的地址和端口,使客户端能够连接到该地址并通过端口发送数据
    serv_addr.sin_family = AF_INET;  //选择协议族为IPV4
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//监听本地所有IP地址;INADDR_ANY表示一个服务器上的所有网卡(一个服务器可能不止一个网卡,也就是不止一个IP地址)这里不管服务器有几个IP,全部都进行端口绑定
    //如果指定IP的话,将INADDR_ANY替换成指定IP就可以了
    
    //serv_addr到这里包含信息:1.协议族【IPV4】 2.IP【本机所有IP】

    //中途需要一些配置信息
    CConfig *p_config = CConfig::GetInstance();
    for(int i = 0; i< m_ListenPortCount; i++)
    {
       
         //AF_INET:使用IPV4协议族
         //服务器的socket套接字【文件描述符】【套接字包含IP+端口】【一个socket只能绑定一个端口】
        isock = socket(AF_INET,SOCK_STREAM,0); //SOCK_STREAM:使用TCP协议

        //socket调用生成的套接字中会分配一个空闲的端口号,但作为服务器程序,必须指定固定的端口号,不然客户端无法准确连入
        //所以需要调用bind函数,但客户端因为不需要关心本身的端口号,系统会自动分配一个空闲端口,所以不需要bind
        //第三个参数:表示用于接收任何的IP数据包

        OverSock[i] = isock;//用于关闭出错套接字
        if(isock == -1)
        {
            ngx_log_error_core(NGX_LOG_EMERG,errno,"CSocket::Initialize() socket失败i = %d\n",i);
            //这里不能直接exit因为如果这样,成功创建的socket就得不到释放
            return false;
        }
        //设置一些套接字选项【解决TCP的TIME_WAIT状态问题】
        int reuseaddr = 1;//表示开启某一项的状态【这里开起了SO_REUSEADDR状态】
        //setsocketopt函数用于设置套接字的一些参数选项【【用在服务器端socket之后,bind()之前】】
        //参数一:需要设置参数选项的套接字
        //参数二:表示级别,SOL_SOCKET和SO_REUSEADDR这里配套使用
        //       获取或者设置与某个套接字关联的选项
        //       选项可能存在于多层协议中，它们总会出现在最上面的套接字层。当操作套接字选项时，
        //       选项位于的层和选项的名称必须给出。为了操作套接字层的选项，应该 将层的值指定为SOL_SOCKET
        //参数三:SO_REUSEADDR【(CSDN)笔记有解释,这里不详写】主要用于允许重用本地址【允许TIME_WAIT下bind()成功】
        // if(setsockopt(isock,SOL_SOCKET,SO_REUSEADDR,(const void*) &reuseaddr,sizeof(reuseaddr)) == -1)
        // {
        //     ngx_log_stderr(errno,"CSocket::Initialize() setsockopt失败i = %d\n",i);
        //     CloseErrorSock(i,OverSock);//关闭问题套接字
        //     return false;
        // }

        /*在默认情况下
             1.指定向可以允许任意个socket在绑定一个 ip/prot时, 仅有ip或者port一个相同, 就可以成功绑定
             2.0.0.0.0代表任意ip, 即serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);指定ip为本机任意ip, 操作系统会自动分配一个合适的供套接字bind(),
               提供的这个ip会替换INADDR_ANY, 但是系统会认为本机所有ip都已经bind, 而实际上仅有一个本机ip绑定该监听socket, 由于条件1的情况, 
               如果第一个监听套接字使用INADDR_ANY参数分配的ip/port为192.168.8.126/80 ,那么如果就算本机存在不仅仅只有一个ip的情况下, 其它套接字也无法bind()80端口,
               因为所有的ip都已经是绑定状态了, 如果端口号也相同就会导致bind()失败, SO_REUSEADDR选项可以解决该ip(假)冲突的问题.
             3.因为第四次挥手的TIME_WAIT的状态存在, TCP协议会使监听套接字一般维持这个状态开始进入限定时间等待2MSL【1-4分钟】, 等待客户端最终的ACK置位包,
               如果此时服务器关闭重启, 这个状态还会持续, 也就是当前的ip/port默认正在被之前的监听socket绑定, 直到2MSL后,
               因为条件1的存在, 服务器重启立即bind会导致ip/prot冲突, 从而使得bind失败

        补充理解:SO_REUSEADDR作用:         
            1.解决指定Socket绑定INADDR_ANY(本机所有ip),本机会选择一个合适的ip代替INADDR_ANY, 但是系统会认为本机所有ip都被绑定了, 其实只有一个被绑定了,
              SO_REUSEADDR作用可以解决此类ip冲突, 允许此类情况的绑定
            2.允许TIME_WAIT下的bind(), 因为TIME_WAIT本就是一种残留连接状态, SO_REUSEADDR可以强制bind()【有很小几率产生一些异常, 基本忽略】, 不会导致ip/port冲突
            3.SO_REUSEADDR允许完全重复的捆绑(TCP不支持)：当一个IP地址和端口绑定到某个套接口上时，还允许此IP地址和端口捆绑到另一个套接口上。一般来说，这个特性仅在支持多播的系统上才有，而且只对UDP套接口而言（TCP不支持多播）。
            ps:只要该监听socket指定了SO_REUSEADDR选项, 只要不违背默认下ip/port冲突的协议, 都可以成功绑定ip/port*/
        /*SO_REUSEPORT(端口复用):作用(比SO_REUSEADDR更强大)
            1.囊括SO_REUSEADDR的特性【SO_REUSEPORT在SO_REUSEADDR之后出现, 因此并不是所有操作系统都支持SO_REUSEPORT】
            1.允许完全重复的捆绑【多个套接字绑定同一个IP下的同一个端口】
            ps:使用SO_REUSEPORT时, 必须每个监听socket的指定SO_REUSEPORT选项(SO_REUSEADDR只需要指定当前监听socket,并不会检查其它socket),
               如果存在socket没有指定SO_REUSEPORT那么将会bind()失败*/

        /*这里每个worker进程都单独创建监听socket, 而不是master创建然后fork()worker进程, 如果是master创建fork()worker进程, 
          那么由于所有进程的socket的是相同的, 这就导致了当一个新的客户端接入时, epoll会产生惊群效应, 多个worker都被唤醒, 但是仅仅有一个accept成功, 其它返回-1,
          当指定REUSEPORT参数后, ip/port可以完全重复绑定, 所以可以采用每个worker创建监听socket并绑定相同的ip/port进行监听, 
          由于该情况下每个woker的监听socket都是不同的, 也就避免了惊群效应.
          
          ps:开启监听端口复用后允许多个无亲缘关系的进程监听相同的端口,
             并且由系统内核做负载均衡,决定将socket连接交给哪个进程处理,避免了惊群效应,可以提升多进程短连接应用的性能。*/
        if(setsockopt(isock,SOL_SOCKET,SO_REUSEPORT,(const void*) &reuseaddr,sizeof(reuseaddr)) == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize() setsockopt失败i = %d",i);
            CloseErrorSock(i,OverSock);//关闭问题套接字
            return false;
        }

        //设置socket为非阻塞
        if(setnonblocking(isock) == false)
        {
            ngx_log_stderr(errno,"CSocket::Initialize() setnoblocking失败i = %d",i);
            CloseErrorSock(i,OverSock);
            return false; 
        }
        //设置本服务器要监听的地址和端口,这样客户端才可以连接并发送数据
        strinfo[0] = 0;
        sprintf(strinfo,"ListenPort%d",i);
        iport = p_config->GetIntDefault(strinfo,10000);

        //绑定自定义的端口号,客户端与服务器程序通讯时,就通过此端口传输数据
        serv_addr.sin_port = htons((in_port_t)iport); //in_port_t 其实就是uint16_t,将int4字节整形转换为2字节的unsigned short

        if(bind(isock,(struct sockaddr*)&serv_addr,sizeof(serv_addr)) == -1)
        {
            //char *perrinfo = strerror(errno);//strerror通过标准错误的标号，获得错误的描述字符串
            ngx_log_stderr(errno,"CSocket::Initialize() bind失败  i = %d\n",i);
            CloseErrorSock(i,OverSock);
            return false;
        }
        
        //开始监听
        if(listen(isock,NGX_LISTEN_BLOCKLOG) == -1)
        {
            ngx_log_stderr(errno,"CSocket::Initialize() listen失败 i = %d\n",i);
            CloseErrorSock(i,OverSock);
            return false;
        }

        //放入列表
        ngx_listening_ptr p_listensocketitem = new ngx_listening_type;  //【记录监听套接字与对应的监听端口】
        memset(p_listensocketitem,0,sizeof(ngx_listening_type));
        p_listensocketitem->port = iport;                               //记录端口
        p_listensocketitem->fd   = isock;                               //记录套接字句柄
        m_ListenSocketList.push_back(p_listensocketitem);               //加入到队列

        //ngx_log_error_core(NGX_LOG_INFO,0,"监听%d端口成功!",iport);
        ngx_log_stderr(errno,"监听%d端口成功!",iport);
    }

       
    return true;
}

void CSocket::ngx_put_sendMsgQueue(char *SendData)
{
    CMemory *p_memory = CMemory::GetInstance();

     //ngx_log_stderr(errno,"夺锁");
    CLOCK InSendQueueLock(&m_insendQueueMutex);
    LPMSG_HEADER pMsgHeader     = (LPMSG_HEADER)SendData;
    ngx_connection_ptr p_Conn   = pMsgHeader->pConn;
    if(m_RefuseAcceptPkgEnable == 1)           //检测是否存在恶意拒收包用户【拒收包攻击】
    {
        //用户数据包积压过多,判断为恶意用户拒收包,切断连接
        if(p_Conn->iWaitToSendPkgCount > 400)
        {
            ngx_log_stderr(0,"CSocket::ngx_put_sendMsgQueue中p_Conn->iWaitToSendPkgCount > 400, 可能是恶意用户拒收包, 切断连接");
            ++m_AbandonPkgCount;
            p_memory->FreeMemory(SendData);
            ngx_close_connection(p_Conn);         //直接关闭
            return;
        }
        
        //判断服务器是否可能超载
        if(m_sendMsgnum > 50000)
        {
            ngx_log_stderr(0,"CSocket::ngx_put_sendMsgQueue中m_sendMsgnum > 50000, 服务器可能超载, 主动丢包");
            //发消息队列过大,例如客户端恶意不接受数据,这里干掉一些数据的发送
            //有可能导致客户端异常,但是为了服务器稳定,是有一定必要的
            ++m_AbandonPkgCount;                  //丢弃待数据包
            p_memory->FreeMemory(SendData);
            return;
        }
    }

    //走到这里数据没有问题
    m_sendDatatQueue.push_back(SendData);

    ++m_sendMsgnum;
    ++p_Conn->iWaitToSendPkgCount;

    // ngx_log_stderr(errno,"入发消息队列,sem + 1");
    if(sem_post(&m_semEventSendQueue) == -1)  //信号量+1,使发包线程往下执行
    {
        ngx_log_stderr(errno,"CSocket::ngx_put_sendMsgQueue中sem_post(&m_semEventSendQueue)失败");
    }

    return;
}

//关闭监听socket【暂未调用】
void CSocket::ngx_close_listening_sockets()
{
    for(int i =0; i < m_ListenPortCount; ++i)
    {
        close(m_ListenSocketList[i]->fd);
        ngx_log_error_core(NGX_LOG_INFO,0,"关闭监听端口%d成功!\n",m_ListenSocketList[i]->port);
    }//end close
    return;
}

void CSocket::ngx_clear_connectionPool()
{
    ngx_connection_ptr p_Conn;
    CMemory *p_memory = CMemory::GetInstance();
    while(!m_connectpoollist.empty())
    {
        p_Conn = m_connectpoollist.front();
        m_connectpoollist.pop_front();
        p_Conn->~ngx_connection_s();            //手动调用析构
        p_memory->FreeMemory(p_Conn);
    }
}

int CSocket::ngx_epoll_Regist_event(int fd,                   //epoll对象描述符
                             uint32_t eventtype,              //事件类型
                             uint32_t flag,                   //其他标志【对于监听套接字这里是EPOLLIN|EPOLLRDHUP表示3次握手和4次挥手事件】
                             int      otheraction,            //补充动作,补充falg的不足 0:在原有标记上增加标记 1:去掉原有标记 2:覆盖原有标记
                             ngx_connection_ptr pConn)        //连接池中的一个客户端
{
     struct epoll_event ev;  

     memset(&ev,0,sizeof(ev));
     if(eventtype == EPOLL_CTL_ADD)
     {
         //红黑树从无到有增加节点
         //ev.data.ptr    = (void *)pConn;  //连接节点地址
         ev.events      = flag;             //事件类型
         pConn->events  = flag;             //连接本身也记录事件类型
     }
     else if(eventtype == EPOLL_CTL_MOD)    //MOD事件【修改节点,比如增加发送事件时需要使用】
     {
        //ev.data.ptr    = (void *)pConn; //连接节点地址【因为调用了memset(&ev,0,sizeof(ev))数据被覆盖】
        ev.events  = pConn->events;       //获取原有标记
        if(otheraction == 0)              //增加
        {
             ev.events |= flag;
        }
        else if(otheraction == 1)       //删除
        {
            ev.events &= ~flag;
        }
        else                            //修改
        {
            //ev.data.ptr    = (void *)pConn;
             ev.events = flag;
        }
        pConn->events = ev.events;      //记录当前标记
     }
     else
     {
         /*删除红黑树中的节点,目前没有这个需求,return 1表示成功*/
         return 1;  
     };
     
    ev.data.ptr    = (void *)pConn;                      //无论是什么事件,都在这里指定ev.data.ptr【因为调用了memset(&ev,0,sizeof(ev)) ev.data.ptr数据会被覆盖】
    if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)  //epoll的事件注册函数 , 每个套接字都注册一次
    {
        ngx_log_stderr(errno,"CSocket::ngx_epoll_Regist_event中epoll_ctl(%d,%ud,%ud,%d,pConn)失败\n",fd,eventtype,flag,otheraction);
        return -1;
    }
   
    return 1;
}

// int CSocket::ngx_epoll_add_event(int fd,                      //监听套接字
//                              int readevent,int writeevent,    //读权限 , 写权限
//                              uint32_t otherflag,              //其他标志 0/1
//                              uint32_t eventtype,              //事件类型
//                              ngx_connection_ptr c)            //连接池中的一个客户端
// {

//     struct epoll_event ev;  
    
//     memset(&ev,0,sizeof(ev));
//     if(readevent == 1) //读事件
//     {
//         //系统定义
//         ev.events = EPOLLIN | EPOLLRDHUP;  //EPOLLIN:读事件 read ready【客户端的三次握手,属于可读事件】,EPOLLRDHUP【客户关闭连接,四次挥手,也属于可读事件】
//     }
//     else
//     {
//         //其他类型事件处理
//         //.......
//     }
   
//     if(otherflag != 0) //其他标记
//     {
//         ev.events |= otherflag;
//     }
    
//     //因为指针的最后一位【二进制位】肯定不是1，所以 和 c->instance做 |运算；到时候通过一些编码，既可以取得c的真实地址，又可以把此时此刻的c->instance值取到
//     //把一个指针和一个位域放入ptr后续能把这两个值全部取出来
//     ev.data.ptr = (void*)((uintptr_t)c | c->instance);    //ev.data.ptr 记录了c【连接池对象地址 + instance标记】
//     //int epoll_ctl(int efpd , int op ,in sockid , struct epoll_event *event);
//     if(epoll_ctl(m_epollhandle,eventtype,fd,&ev) == -1)  //epoll的事件注册函数 , 每个套接字都注册一次
//     {
//         ngx_log_stderr(errno,"CSocket::ngx_epoll_add_event中epoll_ctl(%d,%d,%d,%ud,%ud)失败\n",fd,readevent,writeevent,otherflag,eventtype);
         
//         return -1;
//     }
//     return 1;
// }

ngx_connection_s::ngx_connection_s()       //构造初始化互斥量
{
    ifsendbyepoll = false;
    iCurrsequence = 0;
    pthread_mutex_init(&logiclock,NULL);   //初始化线程逻辑互斥量
}

ngx_connection_s::~ngx_connection_s()      //析构释放互斥量
{
    pthread_mutex_destroy(&logiclock);
}

void ngx_connection_s::GetOneToUse()
{
    ++iCurrsequence;
    //isnewMemory       = false;                      //初始未分配报文缓存区
    CurStat             = _PKG_HEAD_INIT;             //设置新连接为待接包状态[状态机]

    pnewMemoryPointer   = NULL;                       //报文缓存区首地址
    PrecvBuf            = DataHeadInfo;               //每个新的C/S链接,PrecvBuf指向缓存区首地址
    nexTorecvLen        = sizeof(COMM_PKG_HEADER);    //待接收包长[主要用于解决缺包装态] 
    lastPingtime        = time(NULL);                 //初始化心跳包最后发送时间
    iWaitToSendPkgCount = 0;                          //积压C/S 待发送/待接收 数据包数量

    FloodTestLastTime   = 0;                          //上次Flood检测的时间
    FloodAttackCount    = 0;                          //Flood攻击检测区间内Flock攻击次数
}

void ngx_connection_s::PutOneToFree()
{
    
    ++iCurrsequence;

    if(pnewMemoryPointer != NULL)
    {
         CMemory::GetInstance()->FreeMemory(pnewMemoryPointer);
         pnewMemoryPointer = NULL;
         isnewMemory       = false;
    }

    if(ifsendbyepoll)
        ifsendbyepoll = false;

}

void CSocket::shoutdown_subproc()
{
    std::vector<ThreadItem *>::iterator iter;
    for(iter = m_subThreadVector.begin(); iter != m_subThreadVector.end(); ++iter)
    {
        pthread_join((*iter)->_Handle,NULL);
    }
    for(iter = m_subThreadVector.begin(); iter != m_subThreadVector.end(); ++iter)
    {
        if(*iter)
            delete *iter;
    }

    m_subThreadVector.clear();
    clearMsgRecvQueue();                         //清空残余消息队列

    sem_destroy(&m_semEventSendQueue);           //释放信号量

    pthread_mutex_destroy(&m_insendQueueMutex);  //释放互斥量占用内存

    pthread_mutex_destroy(&m_allocConMuext);    
    
    pthread_mutex_destroy(&m_delayRecyConMutex);    

    std::multimap<time_t , LPMSG_HEADER>::iterator pos, posend; //释放定时器
TimerRecylabel:
    pos     = m_PingTimerQueue.begin();
    posend  = m_PingTimerQueue.end();
    for(; pos != posend; ++pos)
    {
        if(pos->second)
        {
            delete []pos->second;
            goto TimerRecylabel;
        }
    }
    m_PingTimerQueue.clear();

    pthread_mutex_destroy(&m_RefreshTimeMutex);    
}
    

void CSocket::ngx_initconnection()
{
     ngx_connection_ptr p_Conn;
     CMemory *p_memory = CMemory::GetInstance();
     int ilenconnpoolnode = sizeof(ngx_connection_type);

     for(int i = 0; i < m_workConnections; ++i)                                     //初始最大连接数m_workConnections个
     {
         p_Conn = (ngx_connection_ptr)p_memory->AllocMemory(ilenconnpoolnode,true); //定位new,将ngx_connection_type填充到p_Conn中,不调用构造函数【因为节点需要重复利用,不可能重复调用构造函数初始化】
         p_Conn = new(p_Conn) ngx_connection_type();                                //调用构造函数【构造函数中仅初始化iCurrsequence和LocgicLock】
         p_Conn->GetOneToUse();                      //初始化该连接节点【不放入构造函数因为连接是重复利用,不可能重复调用构造函数】
         m_connectpoollist.push_back(p_Conn);        //链接放入到连接池
         m_freeconnectpoollist.push_back(p_Conn);    //初始状态都是空闲,放入空闲列表
         
     }//end  
     m_free_connections_num = m_connections = m_freeconnectpoollist.size();         //记录空闲连接节点数,初始状态三者相同【右结合】
     //ngx_log_stderr(errno,"CSocket::m_free_connections_num.size() = %d ",m_freeconnectpoollist.size());
}
int CSocket::ngx_epoll_init()
{
    //epoll对象描述符
    m_epollhandle = epoll_create(m_workConnections);
    if(m_epollhandle == -1)
    {
        ngx_log_stderr(errno,"CSocket::epoll_create(m_workConnections)失败!!!");
        exit(2);//致命问题,直接退出,资源让系统释放,没必要自己释放了
    }

    ngx_initconnection();                       //初始化连接池

    if(Initialize_subproc() == false)           //初始化子线程相关信息【m_allocConMuext/】
    {
        ngx_log_stderr(errno,"CSocket::Initialize_subproc()初始化子进程失败");
        exit(2);       //退出
    }


    // //连接池大小
    // m_connections           = m_workConnections;
    // //创建连接池【数组,每个元素是一个对象】【每一个对象有对应的事件处理函数指针】
    // m_pconnections          = new ngx_connection_type[m_connections];
    // int i                   = m_connections;
    // ngx_connection_ptr next = NULL;
    // ngx_connection_ptr c    = m_pconnections;

    // //初始化并生成连接池【单链表】
    // do
    // {
    //     i--;
    //     c[i].fd = -1;
    //     c[i].data = next;
    //     c[i].instance = 1;
    //     c[i].iCurrsequence = 0;
    //     pthread_mutex_init(&c[i].logiclock,NULL);
    //     next = &c[i];   //next前移,串连形成单向链表   
    // }while(i);
    // //next = &c[0]; 指向表头
    // m_pfree_connection = next;              //空闲链表指针
    // m_free_connections_num = m_connections; //开始全部都为空闲结点

    //遍历监听socket【监听端口】,为每一个socket增加一个连接池中的连接【客户端连接】
    std::vector<ngx_listening_ptr>::iterator pos;
    for(pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); ++pos)
    {
        //【fd 监听套接字】
        ngx_connection_ptr c  = ngx_get_connection((*pos)->fd);     //每个监听套接字对应绑定一个连接池中的对象

        if(c == NULL)    //空闲链接内存已用完,溢出,不应该出现,直接结束
        {
            ngx_log_stderr(errno,"CSocket::ngx_epoll_init中ngx_get_connection()失败,链接池为空\n");
            exit(2);//致命问题,系统释放资源,没必要自己释放了
        }

        c->listening = (*pos);  //连接连接对象 和监听对象的关联,方便通过连接对象找到监听对象
        (*pos)->connection = c; //连接监听对象 和连接对象的关联,方便通过监听对象找到监听对象

        //获取事件处理函数指针,记录至连接池对象中【当一个客户端连入本服务器时,触发】 【rhandler读事件相关函数指针】
        c->rhandler = &CSocket::ngx_event_accept;        
        
        //ngx_log_stderr(errno,"注册监听socket读事件 listensocket = %d",c->fd);
        if(ngx_epoll_Regist_event((*pos)->fd,
                                  EPOLL_CTL_ADD,           //注册增加事件
                                  EPOLLIN | EPOLLRDHUP,     
                                  0,
                                  c) == -1)
        {
            exit(2);   //监听套接字注册epoll事件失败,直接退出
        }
        //ngx_log_stderr(errno,"成功注册监听socket读事件 listensocket = %d",c->fd);
        // if(ngx_epoll_add_event((*pos)->fd,           //有几个监听端口就调用几次ngx_epoll_add_event
        //                         1,0,
        //                         0,
        //                         EPOLL_CTL_ADD,
        //                         c) == -1)
        // {
        //     exit(2);  //有问题直接退出
        // }
    }


    return 1;
}

//int epoll_wait(int epfd,struct epoll_event *events,int maxevents,int timeout);
int CSocket::ngx_epoll_process_events(int timer)
{
    //等待事件，事件会返回到m_events里，最多返回NGX_MAX_EVENTS个事件【因为我只提供了这些内存】；
    //如果两次调用epoll_wait()的事件间隔比较长，则可能在epoll的双向链表中，积累了多个事件，所以调用epoll_wait，可能取到多个事件
    //阻塞timer这么长时间除非：a)阻塞时间到达 b)阻塞期间收到事件【比如新用户连入】会立刻返回c)调用时有事件也会立刻返回d)如果来个信号，比如你用kill -1 pid测试
    //如果timer为-1则一直阻塞，如果timer为0则立即返回，即便没有任何事件
    //返回值：有错误发生返回-1，错误在errno中，比如你发个信号过来，就返回-1，错误信息是(4: Interrupted system call)
    //       如果你等待的是一段时间，并且超时了，则返回0；
    //       如果返回>0则表示成功捕获到这么多个事件【返回值里】
    int events = epoll_wait(m_epollhandle,m_events,NGX_MAX_EVENTS,timer);         //【已经注册过epoll事件处理函数且 ev.data.addr记录了一个对应的连接池对象地址】
    //保姆式触发LT,接收TCP连接事件【从已完成监听队列中提取出来客户端连接】【如果不处理,不断触发返回该请求,保证不漏过任何一个已完成监听队列的客户端】
    //ngx_log_stderr(0,"epoll被唤醒... pid = %d , events = %d!",getpid(),events);
    //sleep(1);
    if(events == -1)
    {
        //有错误发生，发送某个信号给本进程就可以导致这个条件成立，而且错误码根据观察是4；
        //#define EINTR  4，EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if(errno == EINTR) 
        {
            //信号所致，直接返回，一般认为这不是毛病，但还是打印下日志记录一下，因为一般也不会人为给worker进程发送消息
            ngx_log_error_core(NGX_LOG_INFO,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 1;  //正常返回
        }
        else
        {
            //这被认为应该是有问题，记录日志
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()失败!"); 
            return 0;  //非正常返回 
        }
        
         if(events == 0) //超时，但没事件来
        {
            if(timer != -1)
            {
                //要求epoll_wait阻塞一定的时间而不是一直阻塞，这属于阻塞到时间了，则正常返回
                return 1;
            }
            //无限等待【所以不存在超时】，但却没返回任何事件，这应该不正常有问题        
            ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!"); 
            return 0; //非正常返回 
        }
    }
    
    //惊群，有时候不一定完全惊动所有4个worker进程，可能只惊动其中2个等等，其中一个成功其余的accept4()都会返回-1；错误 (11: Resource temporarily unavailable【资源暂时不可用】) 
    //所以参考资料：https://blog.csdn.net/russell_tao/article/details/7204260
    //其实，在linux2.6内核上，accept系统调用已经不存在惊群了（至少我在2.6.18内核版本上已经不存在）。大家可以写个简单的程序试下，在父进程中bind,listen，然后fork出子进程，
    //所有的子进程都accept这个监听句柄。这样，当新连接过来时，大家会发现，仅有一个子进程返回新建的连接，其他子进程继续休眠在accept调用上，没有被唤醒。
    //ngx_log_stderr(0,"测试惊群问题，看惊动几个worker进程%d\n",s); 【我的结论是：accept4可以认为基本解决惊群问题，但似乎并没有完全解决，有时候还会惊动其他的worker进程】
    
    //ngx_log_stderr(errno,"惊群测试,events = %d, pid = %d",events, getpid());

    ngx_connection_ptr p_Conn;
    //uintptr_t        instance;
    uint32_t           revents;

    //events != 0，确实接收到了事件
    for(int i = 0; i < events; ++i )
    {
        //获取事件处理指针
        p_Conn = (ngx_connection_ptr)(m_events[i].data.ptr);

        //取出最后一位instance标记  p_Conn &  00000000000001
        // instance = (uintptr_t) p_Conn & 1;
        // //将最后一位归0             p_Conn &  11111111111110
        // p_Conn = (ngx_connection_ptr)((uintptr_t) p_Conn & (uintptr_t) ~1);

        if(p_Conn->fd == -1)
        {
            //...
             ngx_log_error_core(NGX_LOG_ALERT,0,"CSocekt::ngx_epoll_process_events()中遇到了过期事件!\n"); 
             continue;
        }

        // if(c->instance != instance){}

        //取出事件类型
        revents = m_events[i].events;

        /*这里放在EPOLLOUT【中处理】*/
        //例如对方close掉套接字，这里会感应到【换句话说：如果发生了错误或者客户端断连】
        // if(revents & (EPOLLERR | EPOLLHUP))
        // {
        //     //事件类型改为读事件,处理四次挥手【最先处理客户端发来的FIN包】
        //     revents |= EPOLLERR | EPOLLHUP;
        //     //EPOLLERR：表示对应的链接上有数据可以读出（TCP链接的远端主动关闭连接，也相当于可读事件，因为本服务器要处理发送来的FIN包）
        //     //EPOLLHUP:表示对应链接挂起,标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
        //     //(this->* (c->rhandler) )(c);    //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
        //                                       //如果新连接进入，这里执行的应该是CSocekt::ngx_event_accept(c)】 可以开始写数据了   
        // }
        //如果是三次握手,读事件
        if(revents & EPOLLIN)
        {
            //调用事件处理函数【在未accept之前调用的为accept函数,接入之后调用数据处理函数】
            (this->*(p_Conn->rhandler) )(p_Conn);
            //标记可以读；【从连接池拿出一个连接时这个连接的所有成员都是0】            
            //注意括号的运用来正确设置优先级，防止编译出错；【如果是个新客户连入
            //如果新连接进入，这里执行的应该是CSocekt::this->*(p_Conn)】 
        }

        //如果是写事件【客户端断开 或者 发缓存区满send by epoll】
        if (revents & EPOLLOUT)
        {
            if(revents &(EPOLLERR | EPOLLHUP | EPOLLRDHUP)) //客户端关闭【回收在recv中处理】
            {
                //EPOLLERR: 对应连接发送错误
                //EPOLLHUP: 对应连接被挂起
                //EPOLLRDHUP: TCP连接的远端关闭或者半关闭状态
                p_Conn->ifsendbyepoll = false;              //如果当发缓存区满,注册可行通知事件设置send by epoll后,客户端断开,这里将send by epoll标记删除,
                                                            //这里不需要调用移除可写通知,红黑树会直接将这个socket从红黑树中删除
            }
            else
            {
                ngx_log_stderr(0,"pid:%d  socket = %d 可写事件被触发......",getpid(),p_Conn->fd);
                (this->*(p_Conn->whandler) )(p_Conn);
            }
        }
    }
}

bool CSocket::ngx_judgeIfConnConnecting(char *msgPkg)
{
        ngx_connection_ptr  _pConn     = NULL;
        LPMSG_HEADER        pMsgHeader = NULL;
        LPCOMM_PKG_HEADER   pPkgHeader = NULL;
        char                *pMsgBuf   = NULL;

        pMsgBuf     = msgPkg;                   
        pMsgHeader  = (LPMSG_HEADER)pMsgBuf;                                        //消息头
        pPkgHeader  = (LPCOMM_PKG_HEADER)(pMsgBuf + m_MsgHeaderLen);                //包头
        _pConn      = pMsgHeader->pConn;                                            //获取连接
        //即将发包,判断客户端是否已经断开【处理过期包】
        if(pMsgHeader->iCurrsequence != _pConn->iCurrsequence)
        {
            return false;
        }//end if

        return true;
}

void CSocket::ngx_inFreeConnectionPool(ngx_connection_ptr pConn)
{
    std::list<ngx_connection_ptr>::iterator pos, posend;

    pos     =   m_freeconnectpoollist.begin();
    posend  =   m_freeconnectpoollist.end();
    for(; pos != posend; ++pos)
    {
        if(*pos == pConn)           //空闲池已经存在,return
            return;
    }

    //该函数谁调用谁负责互斥
    m_freeconnectpoollist.push_back(pConn);

    //空闲连接+1
    ++m_free_connections_num;

    pConn->ifInDelayRecyQueue  = false;     //延迟回收标注 false:空闲池中 true:延迟回收池中
    if(m_OpenPingTimerEnable == 1 && m_free_connections_num > 0)
    {
         ngx_log_stderr(0,"成功回收异常心跳连接");
         return;
    }

    if(m_OpenPingTimerEnable == 0 && m_free_connections_num > 0)
    {
        ngx_log_stderr(0,"成功延迟回收连接");
        return;
    }
}

void CSocket::RemoveFromTimerAndShutConn(ngx_connection_ptr _pConn)
{
    CMemory *_pMemory  = CMemory::GetInstance();
    std::multimap<time_t , LPMSG_HEADER>::iterator pos, posend;
    ngx_connection_ptr  pConn;

    //实际情况可能比较复杂,这里用循环遍历保证将客户端剔除
    CLOCK RefreshTimeLock(&m_RefreshTimeMutex);
RemoveConnFromTimerLabel:
        pos     = m_PingTimerQueue.begin();
        posend  = m_PingTimerQueue.end();
        for (; pos != posend; ++pos)
        {
            pConn   = pos->second->pConn;
            if(_pConn == pConn)
            {
                ngx_log_stderr(0,"socket回收,踢出Timer队列");
                _pMemory->FreeMemory(pos->second); 
                m_PingTimerQueue.erase(pos);
                --m_cur_timerQueueSize;
                goto RemoveConnFromTimerLabel;
            }
        }//end for

        if(m_cur_timerQueueSize > 0)
        {
            m_TimerEarlisttime = GetTimerEarlistTime();
        }

    return;
}

void CSocket::MonitoringAndOutPutServerInfo()
{
    time_t curtime          = time(NULL);
    if(curtime - m_lastoutputinfotime >= 10)
    {
        //int CurRunningLogicProc     = ThreadsPool.m_isRunningThreadNum;
        int OnlineUsers             = m_CurOnlinUserCount;
        int DelayRecyUserCount      = m_delayRecyConnum;
        int FreeConnPoolSize        = m_free_connections_num;
        int SendQueueSize           = m_sendMsgnum;
        int CurMsgWaitSendCount     = ThreadsPool.m_CurMsgNUM;
      
ngx_log_stderr
(0,"-------------------- ServerStatus --------------------\n当前在线人数:%d 回收池连接数:%d 空闲池大小:%d 连接池容量:%d\n当前发消息队列大小:%d 收消息队列大小:%d 丢弃待发包数:%d",
OnlineUsers, DelayRecyUserCount,  FreeConnPoolSize, m_connections, SendQueueSize, CurMsgWaitSendCount, m_AbandonPkgCount);

        if(SendQueueSize > 100000)
        {
            ngx_log_stderr(0,"当前发消息队列过大,线程繁忙,需要扩充线程或者限速");    
        }
        
        m_lastoutputinfotime = curtime;
    }
    
}

void CSocket::ngx_free_connection(ngx_connection_ptr pConn)
{
    //因为可能多线程调用【主线程,延迟回收线程,心跳包线程】需要进行合理安全的互斥
    /*若两个或以上线程同时调用可能会导致
    m_OpenPingTimerEnable == 1 && pConn->ifOverRecyFromTimer == true*/
    CLOCK FreeLock(&m_allocConMuext);

    //if(pConn->ifOverRecyFromTimer == false && m_OpenPingTimerEnable == 1)
    // if(m_OpenPingTimerEnable == 1)
    // {
       
    //     //回收定时器相关
    //     RemoveFromTimerAndShutConn(pConn);
    
    //     //回收连接内存
    //     pConn->PutOneToFree();
        
    //     //扔到空闲连接列表
    //     ngx_inFreeConnectionPool(pConn);
    //     return;
    // }
    // else if(m_OpenPingTimerEnable == 0)
    // {
    //     //回收连接内存
    //     pConn->PutOneToFree();
    //     //扔到空闲连接列表
    //     ngx_inFreeConnectionPool(pConn);
    //     return;
    // }

    // if(pConn->ifOverRecyFromTimer == false && m_OpenPingTimerEnable == 1)
    // {
    //     ngx_log_stderr(0,"DelayWaitTime < PingTimerWait");
    //     RemoveFromTimerAndShutConn(pConn, 0);
    // }

        //回收连接内存
        pConn->PutOneToFree();

        //扔到空闲连接列表
        ngx_inFreeConnectionPool(pConn);

    // if(pfree->isnewMemory == true)        //释放数据包缓存区内存
    // {
    //     CMemory::GetInstance()->FreeMemory(pfree->pnewMemoryPointer);
    //     pfree->pnewMemoryPointer = NULL;
    //     pfree->isnewMemory       = false;
    // }
    // ngx_log_stderr(0,"连接被客户端正常关闭[4路挥手关闭]...!");
    // m_freeconnectpoollist
    // pfree->data = m_pfree_connection;

    // ++pfree->iCurrsequence;            //用于判断某些网络事件是否过期

    // ++m_free_connections_num;          //空闲结点+1
}
