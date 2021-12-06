#ifndef __PTHREAD__POOL___
#define __PTHREAD__POOL___

#include <vector>
#include <list>
#include <pthread.h>
#include <atomic>

//线程池相关
class CThreadPool
{
public:
    CThreadPool();

    ~CThreadPool();
public:
    bool CreatThreadPool(int threadNum);         //创建线程池

    void StopAllthreads();                       //终止所有线程
    void CallFreeThread();                       //收到完整需求后调用一个空闲线程处理
    void ProcessDataAndSignal(char* _cMsg);      //入消息队列并通知线程处理用户需求

private:
    static void *ThreadFunc(void *ThreadData);   //线程入口函数


private:
    //定义线程结构
    struct ThreadItem
    {
        pthread_t       _Handle;                 //线程句柄
        CThreadPool     *_pThis;                 //线程池指针
        bool            ifrunning;               //线程启动 false:闲置 true:启动 

        ThreadItem(CThreadPool *pThis):_pThis(pThis),ifrunning(false){};

        ~ThreadItem(){};

    };
private:
    static pthread_mutex_t  m_pthreadMutex;      //线程同步互斥量【线程同步锁】
    static pthread_cond_t   m_pthreadCond;       //线程条件变量【用于线程的沉睡和唤醒】

    std::vector<ThreadItem *> m_LogicthreadsQueue;    //业务逻辑线程队列  
    time_t                    m_lastThreadEmTime;     //上一次线程池占满时

public:
    std::atomic<int>                m_isRunningThreadNum;      //正在运行的线程数
//多线程处理消息队列相关
    int    m_ThreadNum;                                        //要创建的线程数量
    int                             m_CurMsgNUM;               //消息队列项数
    std::list<char *>               m_MsgRecvQueue;            //接受消息队列

};


#endif
