#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ngx_func.h"
#include "ngx_c_socket.h"
//延迟回收:
/*如果A即将执行I/O操作但突然断开连接然后socket被立即回收,分配到空闲队列,那么紧接着的B可能会获得A的socket,导致对B进行非法操作
延迟回收技术保证延迟时间内A即使断开了,A的业务逻辑也可以执行完成,正确被服务器处理断开逻辑,之后B即使分配到相同socket也没有问题
立即回收:
在用户还没有成功三次握手前【连接池分配连接给监听套接字】失败,可以立即回收*/

void CSocket::ngx_delay_close_connection(ngx_connection_ptr pConn)
{
    //双重检查,防止多线程下空闲连接池相同连接重复push
    if(pConn->ifInDelayRecyQueue == false)
    {
        //多线程调用
        CLOCK DelayRecvLock(&m_delayRecyConMutex);          //延迟回收锁【插入删除使用同一个锁,保证容器安全】
        if(pConn->ifInDelayRecyQueue == false)
        {
            pConn->ifInDelayRecyQueue = true;               //标志延迟回收
            pConn->curRecyTime = time(NULL);                //记录当前延迟回收时间
            ++pConn->iCurrsequence;                         //+1断开时都加1

            
            ngx_log_stderr(0,"入延迟回收队列"); 
            m_delayRecyConnectlist.push_back(pConn);        //放入延迟回收队列

            --m_CurOnlinUserCount;                          //在线人数-1
            
            ++m_delayRecyConnum;                            //延迟回收项+1
            
            if(m_OpenPingTimerEnable == 1)
            {  
                RemoveFromTimerAndShutConn(pConn);
            }
            return;
        }
    }

    ngx_log_stderr(0,"成功过滤"); 
    return;
}
void *CSocket::SeverRecyConnectionThreadFun(void *ThreadData)
{
    ngx_log_stderr(0,"延迟回收线程成功运行");

    ThreadItem *pThread = static_cast<ThreadItem *>(ThreadData);
    pThread->ifrunning  = true;                     //标志运行状态
    CSocket *pCSocketObj = pThread->_pThis;
    time_t curtime;
    int err;
    std::list<ngx_connection_ptr>::iterator pbegin,pend;
    ngx_connection_ptr p_Conn;
    while(true)
    {
        usleep(200 *1000);          //单位微秒,每200ms检测一次延迟回收池
        if(pCSocketObj->m_delayRecyConnum > 0 && m_shutdown == false)
        {
            err = pthread_mutex_lock(&pCSocketObj->m_delayRecyConMutex);
            if(err != 0) ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::SeverRecyConnectionThreadFun()lock(m_delayRecyConMutex)失败!"); 
DelayRecyLabel:
            curtime = time(NULL);
            pbegin = pCSocketObj->m_delayRecyConnectlist.begin();
            pend   = pCSocketObj->m_delayRecyConnectlist.end();
            for(; pbegin != pend ; ++pbegin)
            { 
                    p_Conn = *pbegin;

                    if(p_Conn->curRecyTime + pCSocketObj->m_RecyConnectionWaitTime > curtime) continue;  //不到80s,不做处理

                    /*---------测试----------*/
                    if(p_Conn->ifsendbyepoll)
                    {
                        ngx_log_stderr(0,"CSocket::p_Conn->ifsendbyepoll != false , 异常...");
                    }
                    /*-----------------------*/

                    //走到这里表示可以进行回收了
                    --pCSocketObj->m_delayRecyConnum;           //回收项-1
                    pCSocketObj->m_delayRecyConnectlist.erase(pbegin);      //迭代器已失效,移除容器,容器已经改变,立即跳出

                    pCSocketObj->ngx_free_connection(p_Conn);   //立即处理
                    
                    goto DelayRecyLabel;
            }
             err = pthread_mutex_unlock(&pCSocketObj->m_delayRecyConMutex);
             if(err != 0) ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::SeverRecyConnectionThreadFun()unlock(m_delayRecyConMutex)失败!\n"); 
        }
        
//线程通知退出

        if(m_shutdown == true)
        {
            if(pCSocketObj->m_delayRecyConnum > 0)
            {
                err = pthread_mutex_lock(&pCSocketObj->m_delayRecyConMutex);
                if(err != 0) ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::SeverRecyConnectionThreadFun()lock(m_delayRecyConMutex)失败!"); 
    ShutDownDelayThreadLabel:
                pbegin = pCSocketObj->m_delayRecyConnectlist.begin();
                pend   = pCSocketObj->m_delayRecyConnectlist.end();
                for(; pbegin != pend ; ++pbegin)
                { 
                        p_Conn = *pbegin;
                        --pCSocketObj->m_delayRecyConnum;                       //回收项-1
                        pCSocketObj->m_delayRecyConnectlist.erase(pbegin);      //迭代器已失效,移除容器,容器已经改变,立即跳出

                        pCSocketObj->ngx_free_connection(p_Conn);               //立即处理
                        
                        err = pthread_mutex_unlock(&pCSocketObj->m_delayRecyConMutex);
                        if(err != 0) ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::SeverRecyConnectionThreadFun()unlock(m_delayRecyConMutex)失败!\n"); 
                        goto ShutDownDelayThreadLabel;
                }
                err = pthread_mutex_unlock(&pCSocketObj->m_delayRecyConMutex);
                if(err != 0) ngx_log_error_core(NGX_LOG_ALERT,errno,"CSocekt::SeverRecyConnectionThreadFun()unlock(m_delayRecyConMutex)失败!\n");
            }
            break;     //走到这里连接都已经全部回收break
        }
        
    }
    return (void *)0;
}
