#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <list>
#include <time.h>
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_pthreadpool.h"
#include "ngx_c_memory.h"
#include "ngx_c_cstruct.h"
#include "ngx_c_socket.h"

/*返回 > 0,发送了一些数据
0: 对方断开
-1 , errno ==EAGAIN:socket发送缓存区满了 
-2 , errno != EAGAIN != EWOULDBLOCK != EINIR :一般认为是客户端断开的错误
*/
ssize_t CSocket::sendproc(ngx_connection_ptr c ,char* buff ,ssize_t buflen)
{
    //ngx_log_stderr(0,"开始发包");
    ssize_t isendlen = 0;
    while(true)
    {
        isendlen = send(c->fd , buff , buflen , 0);

        //ngx_log_stderr(errno,"isendlen == %d",isendlen);
        
        if(isendlen > 0)              //成功发送一些数据
        {
            return isendlen;
        }
        else if(isendlen == 0)
        {
            //一般recv返回0表示客户端断开,send返回0表示超时,对方主动关闭了连接过程
            return 0;
        }
        else if(isendlen == EAGAIN)   //socket发送缓存区满了,一个字节都没有发送出去
        {
            return -1;
        }
        else if(errno == EINTR)       //因为信号引发的错误,不算真正的错误,等待下次while再次尝试
        {
             ngx_log_stderr(errno,"CSocket::sendproc()中send()失败");
        }
        else                          //其它错误【因客户端主动关闭连接等】
        {
            return -2;
        }  
        
    }//end for
}


 void CSocket::SendNoBodyPkgtoClient(LPMSG_HEADER _MsgHeadle , unsigned short _msgcode)
 {
        CMemory *_pMemory       = CMemory::GetInstance();
        char *emptyBodyPkgBuf   = (char*)_pMemory->AllocMemory(m_MsgHeaderLen + m_PkgHeaderLen , false);
        char *sendbuffer        = emptyBodyPkgBuf;
        memcpy(sendbuffer , _MsgHeadle , m_MsgHeaderLen);
        sendbuffer              = sendbuffer + m_MsgHeaderLen;
        LPCOMM_PKG_HEADER Pkgheader = (LPCOMM_PKG_HEADER)sendbuffer;

        Pkgheader->pkgLen       = htons(m_PkgHeaderLen);       //包长 = 包头长【空包】
        Pkgheader->msgCode      = htons(_msgcode);             //消息代码0
        Pkgheader->crc32        = htonl(0);                    //crc32验证码0

        ngx_put_sendMsgQueue(emptyBodyPkgBuf);                 //入队列
        
 }


void CSocket::procPingTimeOutChecking(LPMSG_HEADER pMsgHeader , time_t cur_time)
{
    CMemory *_pMemory  = CMemory::GetInstance();
    _pMemory->FreeMemory(pMsgHeader);
}
                  

void *CSocket::SeverMonitorPingPkgFun(void *ThreadData)
{
    int err;
    ThreadItem *pThread = static_cast<ThreadItem *>(ThreadData);
    pThread->ifrunning  = true;                     //标志运行状态
    CMemory *_pMemory   = CMemory::GetInstance();
    CSocket *pCSocketObj = pThread->_pThis;
    std::multimap<time_t , LPMSG_HEADER>::iterator pos,posend,postmp;
    time_t curtime , earlisttime;
    while(!m_shutdown)
    {
        //这里不互斥只做粗略判断【省略一次互斥】
        if(pCSocketObj->m_cur_timerQueueSize > 0)
        {
            earlisttime  = pCSocketObj->m_TimerEarlisttime;
            curtime = time(NULL);

            if(earlisttime < curtime)
            {
                //基本判断可以开始处理
                std::list<LPMSG_HEADER> overtimelist;
                LPMSG_HEADER    result;
                
                err = pthread_mutex_lock(&pCSocketObj->m_RefreshTimeMutex);
                if(err != 0) ngx_log_stderr(err,"CSocket::SeverMonitorPingPkgFun中pthread_mutex_lock(&m_RefreshTimeMutex)失败");
                while((result = pCSocketObj->GetOverTimeTimer(curtime)) != NULL)
                {
                    overtimelist.push_back(result);    //入超时队列,开始检测是否发送心跳包
                }
                err = pthread_mutex_unlock(&pCSocketObj->m_RefreshTimeMutex);
                if(err != 0) ngx_log_stderr(err,"CSocket::SeverMonitorPingPkgFun中pthread_mutex_unlock(&m_RefreshTimeMutex)失败");

                LPMSG_HEADER tmpmsg;
                while(!overtimelist.empty())
                {
                    tmpmsg = overtimelist.front();
                    overtimelist.pop_front();
                    pCSocketObj->procPingTimeOutChecking(tmpmsg , curtime);         //调用子类procPingTimeOutChecking
                }
            }
            usleep(500*1000);       //sleep 500 ms
        }
    }
}

 

void *CSocket::SeverSendQueueThreadFun(void *ThreadData)
{
    ngx_log_stderr(0,"发包线程成功运行");
    ThreadItem *pThread = static_cast<ThreadItem *>(ThreadData);
    pThread->ifrunning  = true;                     //标志运行状态
    CMemory *_pMemory   = CMemory::GetInstance();
    CSocket *pCSocketObj = pThread->_pThis;
    std::list<char *>::iterator pos,posend,postmp;
    ngx_connection_ptr  _pConn     = NULL;
    LPMSG_HEADER        pMsgHeader = NULL;
    LPCOMM_PKG_HEADER   pPkgHeader = NULL;
    char                *pMsgBuf   = NULL;
    int err                        = 0;
    ssize_t issendlen              = 0;        
    unsigned short _pkglen         = 0;
    
    while(m_shutdown == false)
    {
        //ngx_log_stderr(0,"begin wait sem1...");
        //信号量初始值是0,线程沉睡
        if(sem_wait(&pCSocketObj->m_semEventSendQueue) == -1)
        {   
            if(errno != EINTR)
                ngx_log_stderr(errno,"CSocket::SeverSendQueueThreadFun中sem_wait(&m_semEventSendQueue)失败");
        }
        
        //走到这里信号量非0,有数据,再次判断进程是否要求退出
        if(m_shutdown == true)
            break;
        
        if(pCSocketObj->m_sendMsgnum > 0)
        {
            //因为入消息队列会有多个线程调用，所以需要互斥
            err =  pthread_mutex_lock(&pCSocketObj->m_insendQueueMutex);
            if(err != 0) 
            {
                ngx_log_stderr(errno,"CSocket::SeverSendQueueThreadFun中pthread_mutex_lock(&m_insendQueueMutex)失败");
            }
            else
            {
                //ngx_log_stderr(errno," pthread_mutex_lock(&pCSocketObj->m_insendQueueMutex)");
            }
            
            pos      = pCSocketObj->m_sendDatatQueue.begin();
            posend   = pCSocketObj->m_sendDatatQueue.end();
            while(pos != posend)
            {
                if(pCSocketObj->ngx_judgeIfConnConnecting(*pos) == false)
                {
                    postmp = pos;
                    ++pos;
                    _pMemory->FreeMemory(*postmp);                 //释放发包所分配的内存【其它断开处理在recv处已经处理】
                    pCSocketObj->m_sendDatatQueue.erase(postmp);   //移除失效迭代器
                    --pCSocketObj->m_sendMsgnum;                   //消息数-1
                    //ngx_log_stderr(errno,"即将发包,但客户端断开");
                    continue;
                }

                pMsgBuf     = (*pos);                   
                pMsgHeader  = (LPMSG_HEADER)pMsgBuf;                                        //消息头
                pPkgHeader  = (LPCOMM_PKG_HEADER)(pMsgBuf + pCSocketObj->m_MsgHeaderLen);   //包头
                _pConn      = pMsgHeader->pConn;                                            //获取连接
                
                // //即将发包,判断客户端是否已经断开【处理过期包】
                // if(pMsgHeader->iCurrsequence != _pConn->iCurrsequence)
                // {
                //       postmp = pos;
                //       ++pos;
                //       _pMemory->FreeMemory(*postmp);                 //释放发包所分配的内存【其它断开处理在recv处已经处理】
                //       pCSocketObj->m_sendDatatQueue.erase(postmp);   //移除失效迭代器
                //       --pCSocketObj->m_sendMsgnum;                   //消息数-1
                //       ngx_log_stderr(errno,"即将发包,但客户端断开");
                //       continue;
                // }//end if

                /*判断该连接是否正在依靠epoll事件驱动进行发包
                如果同一个连接连续获取到多个写事件,其中一个send发生了发缓存区满现象,ifsendbyepoll会被置为true
                此处判断会跳过此连接后续的写事件,但不会在发消息队列中删除消息,也不会影响其它连接的事件处理
                该连接的此次(仅send满的一次)写事件会交给epoll事件驱动处理,事件被处理后,
                这里的ifsendbyepoll会被置false,此连接允许继续处理写事件*/
                if(_pConn->ifsendbyepoll == true)
                {
                    ++pos;
                    continue;    //依靠系统驱动,跳过
                }
                
                //可以开始正常发送
                --_pConn->iWaitToSendPkgCount;                //消息积压数量-1
                --pCSocketObj->m_sendMsgnum;                  //消息数减一
                _pConn->psendMemoryPointer = pMsgBuf;         //记录新开辟内存首地址
                _pConn->PsendBuf = (char *)pPkgHeader;        //记录发缓存首地址
                postmp = pos;                                 //记录用于erase                                             
                ++pos;                                        //后移到下一个待处理项
                pCSocketObj->m_sendDatatQueue.erase(postmp);  //移除
                _pkglen = ntohs(pPkgHeader->pkgLen);          
                _pConn->nexTosendlen = _pkglen;               //初始长度 包头+包体
                //LP_RECVPKG p_sendbody = (LP_RECVPKG)( _pConn->PsendBuf + pCSocketObj->m_PkgHeaderLen);

                //ngx_log_stderr(errno,"开始发包...");

                issendlen = pCSocketObj->sendproc(_pConn , _pConn->PsendBuf , _pConn->nexTosendlen); //发包,返回已发包长
                if(issendlen > 0)
                {
                    if(issendlen == _pConn->nexTosendlen)              
                    {
                        //包头+包体 全部发送成功,释放消息内存
                        _pMemory->FreeMemory(_pConn->psendMemoryPointer);
                        _pConn->psendMemoryPointer = NULL;
                        _pConn->PsendBuf           = NULL;
                        _pConn->ifsendbyepoll      = false;    //这行可有可无,这里本来就是0
                        //ngx_log_stderr(errno,"发包完毕 issendlen = %d",issendlen);
                       
                    }
                    else    
                    {
                        //发送缓存区满,发送了部分数据,其它交给epoll事件驱动处理
                         _pConn->PsendBuf       =  _pConn->PsendBuf + issendlen;
                         _pConn->nexTosendlen   =  _pConn->nexTosendlen  - issendlen;
                         _pConn->ifsendbyepoll  =  true;                //true表示交给epoll事件驱动通知处理
                        if(pCSocketObj->ngx_epoll_Regist_event(
                                                    _pConn->fd,         //Socketfd
                                                    EPOLL_CTL_MOD,      //MOD修改事件【添加发数据事件】
                                                    EPOLLOUT,           //EPOLLOUT:可写
                                                    0,
                                                    _pConn) == -1)
                                {
                                    ngx_log_stderr(errno,"CSocket::SeverSendQueueThreadFun中ngx_epoll_Regist_event(EPOLL_CTL_MOD,EPOLLOUT,0)失败 issendlen > 0");
                                    //可写事件注册失败,暂时不做处理
                                }
                        ngx_log_stderr(0,"发包不完整,由epoll事件驱动处理,socket = %d , issendlen = %d",_pConn->fd,issendlen);
                    }
                    continue;
                }
                else if(issendlen == 0)     //客户端主动断开
                {
                     ngx_log_stderr(errno,"发包完毕后客户端断开",issendlen);
                    _pMemory->FreeMemory(_pConn->psendMemoryPointer);   //释放内存
                    _pConn->psendMemoryPointer = NULL;
                    _pConn->PsendBuf           = NULL;
                    _pConn->nexTosendlen       = 0;
                    _pConn->ifsendbyepoll      = false;                 //这行可有可无,这里本来就是0
                    continue;
                }
                else if(issendlen == -1)
                {
                    //发送缓存区满,一个字节都没有发送出去,交给epoll处理
                    _pConn->ifsendbyepoll = true;                     //true表示交给epoll事件驱动通知处理
                    if(pCSocketObj->ngx_epoll_Regist_event(
                                                    _pConn->fd,       //Socketfd
                                                    EPOLL_CTL_MOD,    //MOD修改事件【添加发数据事件】
                                                    EPOLLOUT,         //EPOLLOUT:可写
                                                    0,
                                                    _pConn) == -1)
                                {
                                    ngx_log_stderr(errno,"CSocket::SeverSendQueueThreadFun中ngx_epoll_Regist_event(EPOLL_CTL_MOD,EPOLLOUT,0)失败 issendlen == -1");
                                    //可写事件注册失败,暂时不做处理
                                }
                    ngx_log_stderr(0,"发包不完整,由epoll事件驱动处理,socket = %d , issendlen = %d",_pConn->fd,issendlen);
                    continue;
                }
                else
                {
                    //ngx_log_stderr(errno,"retrn == -2 发包失败客户端断开");
                    //走到这里的是返回值等于-2的事件,一般源于客户端断开
                    _pMemory->FreeMemory(_pConn->psendMemoryPointer);   //释放内存
                    _pConn->psendMemoryPointer = NULL;
                    _pConn->PsendBuf           = NULL;
                    _pConn->ifsendbyepoll      = false;                 //这行可有可无,这里本来就是0
                    _pConn->nexTosendlen       = 0;
                    continue;
                }
            }//end pos != posend
            err =  pthread_mutex_unlock(&pCSocketObj->m_insendQueueMutex);
            if(err != 0) 
            {
                ngx_log_stderr(errno,"CSocket::SeverSendQueueThreadFun中pthread_mutex_lock(&m_insendQueueMutex)失败");
            }
        }//end if

    }//end while
    return (void*)0;
}