#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <string.h>
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_memory.h"
#include "ngx_c_cstruct.h"      
#include "ngx_pthread_mutex.h"
#include "ngx_c_pthreadpool.h"
/*--------------------------------------------------------------------------
为什么ET只触发一次:  [事件只被扔向双向链表一次,epoll_wait接受后就从双向链表移除]
LT触发多次:          [实践如果没有处理完,那么事件会多次往双向链表扔]
ps: LT写好了不一定比ET差【ET循环取出数据也需要不小消耗】
    如果收发数据包有固定格式,那么建议采用LT模式【编程简单,清晰,且写好效率不一定比ET差】
    如果无固定格式,那么建议采用ET模式
---------------------------------------------------------------------------*/


ssize_t CSocket::recvproc(ngx_connection_ptr c , char *buffer, ssize_t buflen)
{
    ssize_t n;
    
    //接受字节数 <=  buflen (BIT)
    n = recv(c->fd, buffer , buflen , 0);  //recv()系统函数， 最后一个参数flag，一般为0；     
    //ngx_log_stderr(0,"recv n = %d (bit)  pid = %d\n",n,getpid());
   
    if(n == 0)
    {
        //ngx_log_stderr(errno,"准备回收socket = %d , pid = %d",c->fd,getpid()); 
        if(c->fd != -1)
        {
            if(close(c->fd) == -1)
            {
                ngx_log_stderr(0,"CSocket::recvproc(),recv = 0,close(%d)失败...",c->fd);
            }
        }
         
        // //客户端主动关闭【应该是正常完成了4次挥手】,立即关闭socket,延迟回收连接
        //ngx_free_connection(c);
        ngx_log_stderr(0,"客户端正常关闭 n = %d",n);
        ngx_delay_close_connection(c);  //延迟回收
        return -1;
    }
    //客户端没断，走这里 
    if(n < 0) //这被认为有错误发生
    {
        //EAGAIN和EWOULDBLOCK[【这个应该常用在hp上】应该是一样的值，表示没收到数据，一般来讲，在ET模式下会出现这个错误，因为ET模式下是不停的recv肯定有一个时刻收到这个errno，但LT模式下一般是来事件才收，所以不该出现这个返回值
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            ngx_log_stderr(errno,"LT模式下 CSocekt::recvproc()中errno == EAGAIN || errno == EWOULDBLOCK成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }
        //EINTR错误的产生：当阻塞于某个慢系统调用的一个进程捕获某个信号且相应信号处理函数返回时，该系统调用可能返回一个EINTR错误。
        //例如：在socket服务器端，设置了信号捕获机制，有子进程，当在父进程阻塞于慢系统调用时由父进程捕获到了一个有效信号时，内核会致使accept返回一个EINTR错误(被中断的系统调用)。
        if(errno == EINTR)  //这个不算错误，是我参考官方nginx，官方nginx这个就不算错误；
        {
            //我认为LT模式不该出现这个errno，而且这个其实也不是错误，所以不当做错误处理
            ngx_log_stderr(errno,"CSocekt::recvproc()中errno == EINTR成立，出乎我意料！");//epoll为LT模式不应该出现这个返回值，所以直接打印出来瞧瞧
            return -1; //不当做错误处理，只是简单返回
        }

        //所有从这里走下来的错误，都认为异常：意味着我们要关闭客户端套接字要回收连接池中连接；

        //errno参考：http://dhfapiran1.360drm.com        
        if(errno == ECONNRESET)  //#define ECONNRESET 104 /* Connection reset by peer */
        {
            //如果客户端没有正常关闭socket连接，却关闭了整个运行程序【粗暴无理的，应该是直接给服务器发送rst包而不是4次挥手包完成连接断开】，那么会产生这个错误            
            //10054(WSAECONNRESET)--远程程序正在连接的时候关闭会产生这个错误--远程主机强迫关闭了一个现有的连接
            //算常规错误吧【普通信息型】，日志都不用打印，没啥意思，太普通的错误
            //do nothing

            //....一些大家遇到的很普通的错误信息，也可以往这里增加各种，代码要慢慢完善，一步到位，不可能，很多服务器程序经过很多年的完善才比较圆满；
        }
        else
        {
            //能走到这里的，都表示错误，我打印一下日志，希望知道一下是啥错误，我准备打印到屏幕上
            if(errno == EBADF)  // #define EBADF   9 /* Bad file descriptor */
            {
                //因为多线程，偶尔会干掉socket，所以不排除产生这个错误的可能性
            }
            else
            {
                ngx_log_stderr(errno,"CSocekt::recvproc()中发生错误，我打印出来看看是啥错误！");  //正式运营时可以考虑这些日志打印去掉
            }
        } 
        
        ngx_log_stderr(0,"连接被客户端非正常关闭！");
        //这种真正的错误就要，直接关闭套接字，释放连接池中连接了
        // ngx_log_stderr(0,"客户端异常关闭 n = %d",n);
        if(c->fd != -1)
        {
            if(close(c->fd) == -1)
            {
                ngx_log_stderr(0,"CSocket::recvproc(),recv < 0,close(%d)失败...",c->fd);
            }
        }

        ngx_delay_close_connection(c);  //延迟回收   
        return -1;
    }

    //能走到这里的，就认为收到了有效数据
    return n; //返回收到的字节数
}

void CSocket::ngx_write_surplus_pkg_proc(ngx_connection_ptr pConn)
{
    CMemory *_pMemory   = CMemory::GetInstance();
    
    ssize_t issendlen   = sendproc(pConn , pConn->PsendBuf , pConn->nexTosendlen);
    if(issendlen > 0)
    {
        if(issendlen == pConn->nexTosendlen)
        {
            ngx_log_stderr(0,"socket = %d 通过epoll事件驱动成功完整发送数据包",pConn->fd);

            //删除socket可写事件通知
            if(ngx_epoll_Regist_event(
                                        pConn->fd,         //Socketfd
                                        EPOLL_CTL_MOD,      //MOD修改事件【添加发数据事件】
                                        EPOLLOUT,           //EPOLLOUT:可写
                                        1,                  //移除EPOLLOUT
                                        pConn) == -1)
                                {
                                    ngx_log_stderr(errno,"CSocket::ngx_write_surplus_pkg_proc中ngx_epoll_Regist_event(EPOLL_CTL_MOD,EPOLLOUT,1)失败 issendlen > 0");
                                    //可写事件注册失败,暂时不做处理
                                }

        }
        else
        {
            pConn->PsendBuf     = pConn->PsendBuf + issendlen;
            pConn->nexTosendlen = pConn->nexTosendlen - issendlen;
            return;     //没发完,继续由epoll驱动处理
        }
    }
    else if(issendlen == -1)
    {
        //不太可能,通知我可以发数据,但是发送时返回发送缓冲区满的错误,不过还是打印一下,等待epoll下次处理
        ngx_log_stderr(errno,"ngx_write_surplus_pkg_proc中,send()失败,返回值 -1");
        return;         //没发出去,再次由epoll驱动处理
    }
   
        
    //走到这里代表是客户端断开(返回-2)或者已经成功发送完成,这里在sem_post(&m_semEventSendQueue)一次,
    //因为如果A用户有两个或多个写事件被同时获取,第一个写事件因为发送缓存区满了,后续写事件数据会被continue,
    //所以本次发消息队列的遍历就会遗漏这些数据,如果后续没有其它socket发送数据(调用sem_post),
    //在无新消息的这段真空期sem_wait(&m_semEventSendQueue)会导致发消息线程沉睡,而消息队列实际上还有剩余消息未处理
    //所有这里的sem_post(&m_semEventSendQueue),使消息队列再次遍历,保证后续数据被成功处理

    _pMemory->FreeMemory(pConn->psendMemoryPointer);       //释放消息内存
    //ps: 如果客户端在服务端发缓存区满send by epoll后直接断开了,ngx_write_surplus_pkg_proc()不能被执行,
    //这里就不会执行到_pMemory->FreeMemory(pConn->psendMemoryPointer),分配的发缓存内存不能被释放,但没有关系
    //在断开处理中,会检测连接的分配发缓存区是否分配有数据,有则delete.
    //【ngx_free_connection()中PutOneToFree,if(psendMemoryPointer) delete []psendMemoryPointer】
        
    pConn->psendMemoryPointer = NULL;
    pConn->PsendBuf           = NULL;
    pConn->nexTosendlen       = 0;
    pConn->ifsendbyepoll      = false;                     //删除epoll事件驱动处理发包标记

    if(sem_post(&m_semEventSendQueue) == -1)
        ngx_log_stderr(errno,"ngx_write_surplus_pkg_proc中,send(),em_post(&m_semEventSendQueue),返回值 -1");

    return;
}
void CSocket::ngx_read_request_handler_prc1(ngx_connection_ptr pConn)
{
    /*---------------------  Demo  ------------------*/
    // // int n = -1;
    // // // /*ET模式的数据收发demo*/
    // // do{
    // //     unsigned char buffer[2 + 1] = {0};
    // //     n = recv(c->fd,buffer,2,0); 
    // //     if(n == -1 && errno == EAGAIN) break;
    // //     else if(n == 0) break;  /*当客户端断开连接时 n == -1 && errno == EAGAIN 并不能解决客户端关闭事件*/    
    // //     //ps:客户端关闭服务器处于可读状态,当客户端断开时读取的字节n == 0

    // //      /*每次只收发两个字节,直到收完【recv返回 -1 表示没有数据可接受,
    // //      错误通知(可以当作一个提示信息,不算错误)->EAGAIN表示当前没有收到数据,建议再试一次】*/

    // //     ngx_log_stderr(0,"接受缓冲区字符: %s\n",buffer);
    // // }while(1);


    /* LT模式  也采取一次性接受 【demo】*/
    // int n ;
    // //do{
    //     unsigned char buffer[20 + 1] = {0};
    //     n = recv(c->fd,buffer,20,0); 

    //     ngx_log_stderr(0,"%s",buffer); 
    // //}while(1);

    // if(n == 0)                  /*当客户端主动断开连接,释放已连接套接字,更新连接池*/
    // {
    //     ngx_log_stderr(0,"检测到客户端断开连接 close(_socket)\n");  
    //     ngx_free_connection(c);
    //     close(c->fd);           /*服务器主动断开客户端连接*/
    //     c->fd = -1;             /*fd置为-1*/
    // }
    // return;

    /*------------------------- 收包 ----------------*/
    bool isflood = false;
    ssize_t reco = recvproc(pConn, pConn->PrecvBuf , pConn->nexTorecvLen);

    if(reco <= 0)
    {
        //有问题已经处理过了
        return; 
    }

    //成功收到一些字节
    if(pConn->CurStat == _PKG_HEAD_INIT)   //预接包头状态
    {
        //ngx_log_stderr(0,"收到完整包头");   
        //write(STDERR_FILENO,"Begin Recv Header Data!!\n",38);
        if(reco == m_PkgHeaderLen)     //已经完整接收到包头
        {
             //write(STDERR_FILENO,"recv Body Data success!!",23);
            ngx_wait_recv_pkgbody_prc2(pConn, isflood);     //预接收包体
        }
        else                           //包头不全
        {
            pConn->CurStat      = _PKG_HEAD_RECVING;            //续接包头状态
            pConn->PrecvBuf     = pConn->PrecvBuf + reco;       //指向续待缓存区
            pConn->nexTorecvLen = pConn->nexTorecvLen - reco;   //待续接包头长
        }
        
    }
    else if(pConn->CurStat == _PKG_HEAD_RECVING)            //续接包头状态
    {
        if(pConn->nexTorecvLen == reco)                     //已经续接全部包头
        {
             //write(STDERR_FILENO,"recv Body Data success!!\n",23);
             //ngx_log_stderr(0,"收到完整包头");   
             ngx_wait_recv_pkgbody_prc2(pConn, isflood);  //预接收包体
        }
        else                                              //包头还没接收完整
        {
            //c->CurStat      = _PKG_BODY_RECVING;        //继续接收包头
            pConn->PrecvBuf     = pConn->PrecvBuf + reco;         //指向续待缓存区
            pConn->nexTorecvLen = pConn->nexTorecvLen - reco;     //待续接包头长
        } //end if(c->CurStat == _PKG_HEAD_RECVING)
        
    }
    else if(pConn->CurStat == _PKG_BODY_INIT)               //预接受包体状态
    {
        //write(STDERR_FILENO,"Begin Recv Body Data!!\n",38);
        if(reco == pConn->nexTorecvLen)                     //收到完整包体
        {
            isflood = SocketFloodTest(pConn);
            //ngx_log_stderr(0,"收到完整包体");   
            ngx_put_handle_cinfo(pConn, isflood);
        }
        else
        {
            pConn->CurStat      = _PKG_BODY_RECVING;
            pConn->PrecvBuf     = pConn->PrecvBuf + reco;
            pConn->nexTorecvLen = pConn->nexTorecvLen - reco;
        }
    }
    else if(pConn->CurStat == _PKG_BODY_RECVING)            //续接包头
    {
        if(reco == pConn->nexTorecvLen)
        {
             //ngx_log_stderr(0,"收到完整包体");
             isflood = SocketFloodTest(pConn); 
             ngx_put_handle_cinfo(pConn, isflood);
        }
        else
        {
            pConn->PrecvBuf     = pConn->PrecvBuf + reco;
            pConn->nexTorecvLen = pConn->nexTorecvLen - reco;
        }
        
    }

    if(isflood == true)
    {
        ngx_log_stderr(0,"检测到FLood攻击,断开flood恶意Connection");
        ngx_close_connection(pConn);
    }

    return;
}



void  CSocket::ngx_wait_recv_pkgbody_prc2(ngx_connection_ptr pConn, bool &isflood)
{
    CMemory *p_memory = CMemory::GetInstance();

    LPCOMM_PKG_HEADER pPkgHeader;                           //包头指针
    pPkgHeader = (LPCOMM_PKG_HEADER)(pConn->DataHeadInfo);  //强制装换为包头结构


    unsigned short pkg_len;
    pkg_len = ntohs(pPkgHeader->pkgLen);            //同步大小端模式【从网络上传输过来的数据需要调用该函数使网络序列转本地序】   
    //ngx_log_stderr(0,"Recv pkg_len = %d (bit) !!!",pkg_len); 
    /*------------- 判断恶意包 -------------*/
    if(pkg_len < m_PkgHeaderLen)                    //包体长度 < 包体 + 包头 【认定为废包】
    {
        //ngx_log_stderr(0,"pkg_len < m_PkgHeaderLen , pkg_len = %d !!!\n",pkg_len);    
        //不处理,初始收包状态状态
        pConn->CurStat      = _PKG_HEAD_INIT;
        pConn->nexTorecvLen = m_PkgHeaderLen;
        pConn->PrecvBuf     = pConn->DataHeadInfo;
    }
    else if(pkg_len > (_PKG_MAX_LENGTH - 1000))     //包体长度 > 29000 【过大,判断为恶意包】
    {
        write(STDERR_FILENO,"pkg_len > (_PKG_MAX_LENGTH - 1000)!!\n",38);
        //不处理,初始收包状态状态
        pConn->CurStat      = _PKG_HEAD_INIT;
        pConn->nexTorecvLen = m_PkgHeaderLen;
        pConn->PrecvBuf     = pConn->DataHeadInfo;
    }
    else                                            //有效包【分配内存:消息头 + 包头 + 包体】
    {
        //合法的包头
        char *pTempBuffer               = (char*)p_memory->AllocMemory(pkg_len + m_MsgHeaderLen,false);
        pConn->isnewMemory              = true;                   //分配内存标识【用来处理发送恶意缺包的数据包,及时释放系统资源】
        pConn->pnewMemoryPointer        = pTempBuffer;            //指向待填充收包缓存区首地址【消息头 + 包头 + 包体】

        //填写消息头内容
        LPMSG_HEADER pMsgHeader     = LPMSG_HEADER(pTempBuffer);
        pMsgHeader->pConn           = pConn;                      //记录当前连接
        pMsgHeader->iCurrsequence   = pConn->iCurrsequence;       //记录当前连接序号,判断连接是否过期

        //填写包头内容
        pTempBuffer += m_MsgHeaderLen;
        memcpy(pTempBuffer,pPkgHeader,m_PkgHeaderLen);

        //整包长 == 包头【无包体】【心跳包】
        if(pkg_len == m_PkgHeaderLen)
        {
            isflood = SocketFloodTest(pConn);

            //write(STDERR_FILENO,"recv  Full Data success No Body!!\n",23);
            //收完整了,放入消息队列,等待业务逻辑线程处理
            ngx_put_handle_cinfo(pConn, isflood);
        }
        else
        {
            //不是空包【预接包体】
            //ngx_log_stderr(0,"预接包体");
            pConn->CurStat      = _PKG_BODY_INIT;               
            pConn->PrecvBuf     = pTempBuffer + m_PkgHeaderLen;
            pConn->nexTorecvLen = pkg_len - m_PkgHeaderLen;
            
        }
        
    }
    
}
void  CSocket::ngx_put_handle_cinfo(ngx_connection_ptr pConn, bool &isflood)
{
    
    if(isflood == true)
    {
       CMemory *p_memory = CMemory::GetInstance();
       p_memory->FreeMemory(pConn->pnewMemoryPointer);       //释放内存,不做断开处理
    }
    else
    {
        //ngx_log_stderr(0,"入消息处理队列");
        //将这段内存放入消息队列,并通知线程处理 【消息头 + 包头 + 包体】
        ThreadsPool.ProcessDataAndSignal(pConn->pnewMemoryPointer);
    }
    
    pConn->isnewMemory          = false;            //放入消息队列中后内存释放由业务线程处理,不需要在客户端断开时处理了
    pConn->pnewMemoryPointer    = NULL;             //报文缓存区指针头置空

    
    pConn->CurStat              = _PKG_HEAD_INIT;           //初始化成预收包头状态
    pConn->PrecvBuf             = pConn->DataHeadInfo;      //包头缓存区
    pConn->nexTorecvLen         = m_PkgHeaderLen;           //包头长

    return;
}


/*-------------------------------------------------------
void CSocket::tmpoutMsgRecvQueue()
{
    if(ThreadsPool.m_MsgRecvQueue.empty())                  //消息队列空,直接退出
    {
        return;
    }
    int size = ThreadsPool.m_MsgRecvQueue.size();
    
    if(size < 1000)                             //消息队列项数 < 1000 暂时不处理
    {
        return;
    }

    CMemory *p_memory = CMemory::GetInstance();
    int freesize =  size - 500;
    for(int i = 0; i < freesize; ++i)
    {
        //干掉一部分报文数据内存
        char *sTmpMsgBuf = ThreadsPool.m_MsgRecvQueue.front();
        ThreadsPool.m_MsgRecvQueue.pop_front();

        p_memory->FreeMemory(sTmpMsgBuf);
    }

    return;
}
---------------------------------------------------------*/

time_t CSocket::GetTimerEarlistTime()
{
    return (m_PingTimerQueue.begin()->first);
}

void CSocket::AddtoPingTimerQueue(ngx_connection_ptr pConn)
{   

    CMemory *p_memory   = CMemory::GetInstance();
    time_t endtime      = time(NULL);
    endtime             += m_MaxWaitePingtime;
    //监听器添加事件【互斥】
    CLOCK RefreshTimerLock(&m_RefreshTimeMutex);
    LPMSG_HEADER Msgheader      = (LPMSG_HEADER)p_memory->AllocMemory(m_MsgHeaderLen,false);
    Msgheader->pConn            = pConn;
    Msgheader->iCurrsequence    = pConn->iCurrsequence;

    m_PingTimerQueue.insert(std::make_pair(endtime , Msgheader));
    ++m_cur_timerQueueSize;
    m_TimerEarlisttime = GetTimerEarlistTime();

    //ngx_log_stderr(0,"成功加入心跳队列,size = %d",m_cur_timerQueueSize);
    return;
}


bool CSocket::SocketFloodTest(ngx_connection_ptr pConn)
{
    if(m_FloodAttackickEnable == 0)
        return false;

    struct timeval                  CurTime;
    uint64_t                        FloodTestTime;                   //当前时间【毫秒】

// struct timeval结构体在time.h中的定义为：
// struct timeval
// {
// __time_t tv_sec;        /* Seconds. */
// __suseconds_t tv_usec;  /* Microseconds. */
// };
// 其中，tv_sec为Epoch到创建struct timeval时的秒数，tv_usec为微秒数，即秒后面的零头。

    gettimeofday(&CurTime, NULL);
    FloodTestTime = (CurTime.tv_sec*1000 + CurTime.tv_usec /1000);  //(毫秒)

    if(pConn->FloodTestLastTime - FloodTestTime < m_FloodTimeInterval)
    {
        pConn->FloodTestLastTime = FloodTestTime;
        ++pConn->FloodAttackCount;
    }
    else
    {
        pConn->FloodTestLastTime =  FloodTestTime;
        pConn->FloodAttackCount  = 0;
    }
    
    if(pConn->FloodAttackCount >= m_FloodCounttoKick)               //判断为Flood攻击,return true
        return true;
    
    return false;
}

LPMSG_HEADER  CSocket::RemoveTimerEarlistTime()
{
    LPMSG_HEADER result;

    if(m_cur_timerQueueSize <= 0)
    {
        return NULL;
    }

    result  =  m_PingTimerQueue.begin()->second;

    //删除源迭代器
    m_PingTimerQueue.erase(m_PingTimerQueue.begin());

    //心跳包项数-1
    --m_cur_timerQueueSize;

    //返回超时连接消息头
    return result;
}

LPMSG_HEADER CSocket::GetOverTimeTimer(time_t curtime)
{
     CMemory *p_memory = CMemory::GetInstance();
     LPMSG_HEADER ptmp;

     if(m_cur_timerQueueSize == 0 || m_PingTimerQueue.empty())
        return NULL;
    
    //定时器中存在数据项【谁调用GetOverTimeTimer谁负责互斥】
    time_t earlisttime = GetTimerEarlistTime();
    if(earlisttime <= curtime)
    {
        /*存在超时连*/
        ptmp            = RemoveTimerEarlistTime(); //删除超时链接,返回MsgHeader

        //if m_kickOverUserOnTime == 1,超时这里不在添加到Timer
        //不立即回收
        // if(m_kickOverUserOnTime != 1)
        // {
        //     time_t newtime  = time(NULL);
        //     newtime         += m_MaxWaitePingtime;
        //     LPMSG_HEADER    tmpMsgHeader    = (LPMSG_HEADER)p_memory->AllocMemory(m_MsgHeaderLen,false);
        //     tmpMsgHeader->iCurrsequence     = ptmp->iCurrsequence;
        //     tmpMsgHeader->pConn             = ptmp->pConn;

        //     //更新定时器
        //     m_PingTimerQueue.insert(std::make_pair(newtime , tmpMsgHeader));
        //     ++m_cur_timerQueueSize;
        // }
        
        if(m_kickOverUserOnTime != 1)
        {
            time_t newtime  = time(NULL);
            newtime         += m_MaxWaitePingtime;
            LPMSG_HEADER    tmpMsgHeader    = (LPMSG_HEADER)p_memory->AllocMemory(m_MsgHeaderLen,false);
            tmpMsgHeader->iCurrsequence     = ptmp->iCurrsequence;
            tmpMsgHeader->pConn             = ptmp->pConn;

            //更新定时器
            m_PingTimerQueue.insert(std::make_pair(newtime , tmpMsgHeader));
            ++m_cur_timerQueueSize;
        }

        // else
        // {
        //     ngx_log_stderr(0,"存在超时Conn,准备回收");
        // }
        
        //更新记录TimerEarlistTime
        if(m_cur_timerQueueSize > 0)
        {
           m_TimerEarlisttime   = GetTimerEarlistTime();
        }

        
        return ptmp;
    }

    return NULL;
}

void CSocket::clearMsgRecvQueue()
{
      char *sTmpMsgBuf;
      CMemory *p_memory = CMemory::GetInstance();
      while(!ThreadsPool.m_MsgRecvQueue.empty())
      {
          sTmpMsgBuf = ThreadsPool.m_MsgRecvQueue.front();
          ThreadsPool.m_MsgRecvQueue.pop_front();
          p_memory->FreeMemory(sTmpMsgBuf);
      }
}