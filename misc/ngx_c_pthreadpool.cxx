#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include  <time.h>
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_c_memory.h"
#include "ngx_global.h"

#include "ngx_c_pthreadpool.h"
#include "ngx_c_socket.h"

/*动态初始化：局部变量应采用动态初始化。pthread_mutex_init(&mutex, NULL);

在声明定义Mutex变量的时候进行初始化正是所谓的静态初始化的过程，而将Mutex变量声明之后，
在后面的某条语句中对该Mutex变量进行首次赋值则不是静态初始化过程，不能使用宏的方式进行初始化。

静态初始化过程就是编译器在编译的过程中完成了某些内存空间的初始化，
也就是说这个初始化过程发生在编译时，而不是运行时，因此称之为静态初始化

静态初始化：如果互斥锁mutex是静态分配的(定义在全局，或加了static关键字修饰），
可以直接使用宏进行初始化。pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;*/
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;       //初始化线程同步锁
pthread_cond_t  CThreadPool::m_pthreadCond  = PTHREAD_COND_INITIALIZER;        //初始化条件变量

//构造函数
CThreadPool::CThreadPool()
{

    m_CurMsgNUM       = 0;                           //当前消息队列项数
    m_isRunningThreadNum = 0;           
    m_lastThreadEmTime   = time(NULL);
}

//析构函数
CThreadPool::~CThreadPool()
{
   
};

void CThreadPool::ProcessDataAndSignal(char* _cMsg)
{
    int err;
    err = pthread_mutex_lock(&m_pthreadMutex);
    if(err != 0)
    {
        ngx_log_stderr(0,"ProcessDataAndSignal(char* _cMsg)中,pthread_mutex_lock,错误码%d !!!",err);
    }

    m_MsgRecvQueue.push_back(_cMsg);            //入消息队列
    ++m_CurMsgNUM;                              //消息数 +1

    err = pthread_mutex_unlock(&m_pthreadMutex);
    if(err != 0)
    {
        ngx_log_stderr(0,"ProcessDataAndSignal(char* _cMsg)中,pthread_mutex_unlock,错误码%d !!!",err);
    }

    CallFreeThread();                           //唤醒一个线程处理
    return;
}

void CThreadPool::CallFreeThread()
{

    /*唤醒一个线程处理业【可能会唤醒多个进程中的一个沉睡的线程】->【惊群】【虚假唤醒】
    【但这里使用while达到自激发技巧保证不遗漏用户需求,已经处理的很好,惊群不会影响消息处理】
     pthread_cond_signal 函数的作用是发送一个信号给另外一个正在处于阻塞等待状态的线程,使其脱离阻塞状态,继续执行.
    【ps:如果没有线程处在阻塞等待状态,pthread_cond_signal也会成功返回】*/
    int err = pthread_cond_signal(&m_pthreadCond);
    if(err != 0)
    {
        ngx_log_stderr(0,"CallFreeThread()中,pthread_cond_signal调用失败,错误码%d !!!",err);
        return;
    }

    if(m_ThreadNum == m_isRunningThreadNum)
    {
        //如果线程全部繁忙,若至少10s间出现两次,则警告
        time_t _CurTime = time(NULL);
        if(_CurTime - m_lastThreadEmTime > 10)
        {
            m_lastThreadEmTime = _CurTime;
            ngx_log_stderr(0,"繁忙线程数:%d 线程全部占用中,无法及时处理用户需求...",(int)m_isRunningThreadNum);
        }
    }
    return;
}

bool CThreadPool::CreatThreadPool(int threadNum)
{
    ThreadItem *_lpthread;      //线程结构指针
    int err;

    m_ThreadNum = threadNum;    //记录创建线程数

    for(int i = 0; i < m_ThreadNum; ++i)
    {
        //获取类对象指针【因为线程入口函数是静态函数,不能获取this指针,这里手动获取,并传入入口函数】

        //【类成员函数作线程入口必须采用静态函数】
        //线程函数的一般格式是void *funtion(void *arg)
        //因为普通成员函数调用实际上是: void *（类名）+ ThreadFunc(this, void *args);
        //这就导致了该函数的格式是不符合 pthread_create() 对线程函数的要求的。故，如果类成员函数做为线程函数时，必须是静态的。
        m_LogicthreadsQueue.push_back(_lpthread = new ThreadItem(this));

        /*创建一个线程,并立即执行,成功返回 0 相当于 std::thread
         第一个参数为指向线程标识符的指针。
    　　 第二个参数用来设置线程属性。【一般设置NULL】
    　　 第三个参数是线程运行函数的起始地址。
    　　 最后一个参数是运行函数的参数。
         线程函数有多个参数的情况：这种情况就必须申明一个结构体来包含所有的参数，然后在传入线程函数，*/
        err = pthread_create(&_lpthread->_Handle,NULL,ThreadFunc,(void *)_lpthread);

        if(err != 0)
        {
            //创建线程失败
            ngx_log_stderr(err,"CreatThreadPool(int threadNum),创建线程%d 调用失败,错误码:%d",i,err);
            //直接退出此进程
            return false;
        }
        else
        {
           //线程创建成功
           //ngx_log_stderr(err,"线程创建成功:%d",i);
        }
       
    } //end for

    std::vector<ThreadItem *>::iterator iter;
lblfor:
    for(iter = m_LogicthreadsQueue.begin(); iter != m_LogicthreadsQueue.end(); ++iter)
    {
        if((*iter)->ifrunning == false)
        {
            //等待没有完全启动的线程
            usleep(100 *1000);          //单位微妙
            goto lblfor;
        }
    }
    
    ngx_log_stderr(err,"业务逻辑线程池创建成功,线程数:%d",m_LogicthreadsQueue.size());
    return true;
}

void* CThreadPool::ThreadFunc(void *ThreadData)
{
    ThreadItem* lpthread = (ThreadItem*)ThreadData;
    CThreadPool* lpThreadPool = lpthread->_pThis;

    int err;
    CMemory *_pMemory   = CMemory::GetInstance();

    pthread_t _tid      = pthread_self();           //获取线程id
    while(true)
    {
        err = pthread_mutex_lock(&m_pthreadMutex);  //线程同步互斥
        if(err != 0) ngx_log_stderr(err,"ThreadFunc(void *ThreadData),pthread_mutex_lock调用失败,错误码:%d",err);  //不应该出现,走到这里的都锁住了才对

        //线程刚开始消息队列为空,线程池被初始化处于闲置状态,所以每个线程调用时都一定成立
        //但当线程被再次唤醒时,代表lpThreadPool->m_LogicthreadsQueue.size != 0 || m_shutdown == true,这时候线程就好出去处理业务
        //while( (lpThreadPool->m_MsgQueue.size() == 0) && m_shutdown == false)
        while((lpThreadPool->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
        {
            if(lpthread->ifrunning == false)
                   lpthread->ifrunning = true;       //线程标记运行中

            //ngx_log_stderr(err,"线程创建成功,等待唤醒...");
            //pthread_cond_wait 吧线程放入 等待条件 的线程列表中
            pthread_cond_wait(&m_pthreadCond,&m_pthreadMutex);       //相当于C++11中条件对象的成员函数wait
            //m_pthreadCond初始状态沉睡,每个线程走到这里都会进入沉睡状态,然后解锁互斥量m_pthreadMutex
            //如果被唤醒代表有业务需要处理,就会再次争夺锁,成功锁住互斥量的线程【只会有一个】,出去处理业务

        }

        //线程池通知退出
        if(m_shutdown == true)
        {
            pthread_mutex_unlock(&m_pthreadMutex);
            break;
        }

        /*【互斥获取用户需求】*/
        char *jobBuf = lpThreadPool->m_MsgRecvQueue.front();
        lpThreadPool->m_MsgRecvQueue.pop_front();
        --lpThreadPool->m_CurMsgNUM;                        //消息数 -1

        err = pthread_mutex_unlock(&m_pthreadMutex);        //解锁互斥量
        if(err != 0) ngx_log_stderr(err,"ThreadFunc(void *ThreadData),pthread_mutex_unlock调用失败,错误码:%d !",err);

         /*【处理用户需求】*/

            //当前正处理业务线程数+1
            ++lpThreadPool->m_isRunningThreadNum;
            //ngx_log_stderr(0,"Processing Msg Success pid = %d",getpid());

            //开始处理用户需求...
            g_socket.ThreadRecvProcFunc(jobBuf);
            //...

            //处理完毕释放消息内存
            _pMemory->FreeMemory(jobBuf);
            --lpThreadPool->m_isRunningThreadNum;
       
    }
}

void CThreadPool::StopAllthreads()
{
    //已经调用过了
    if(m_shutdown == true)
    {
        return;
    }
    m_shutdown = true;
    
    //广播唤醒所有m_pthreadCond导致沉睡的线程【相当于notify_all】
    int err = pthread_cond_broadcast(&m_pthreadCond);
    
    if(err != 0)  //无法唤醒,打印紧急日志
    {
        ngx_log_stderr(err,"StopAllthreads(),pthread_cond_broadcast(&m_pthreadCond) 广播失败,错误码:%d",err);  //不应该出现,走到这里的都锁住了才对
        
        return;
    }
    
    std::vector<ThreadItem *>::iterator iter;
    for(iter = m_LogicthreadsQueue.begin(); iter != m_LogicthreadsQueue.end(); ++iter)
    {
        //等待一个线程调用结束【这里for等待线程池中所有线程终止】
        pthread_join((*iter)->_Handle,NULL);  //相当于join函数
    }

    //开始回收互斥量与条件变量利用的内存【走到这里所有线程都已经结束调用】
    /*销毁一个互斥锁即意味着释放它所占用的资源，且要求锁当前处于开放状态。由于在Linux中，互斥锁并不占用任何资源，
    因此LinuxThreads中的 pthread_mutex_destroy()除了检查锁状态以外（锁定状态则返回EBUSY）没有其他动作。*/
    pthread_mutex_destroy(&m_pthreadMutex);
    pthread_cond_destroy(&m_pthreadCond);

    //释放new出来的线程结构内存
    for(iter = m_LogicthreadsQueue.begin(); iter != m_LogicthreadsQueue.end(); ++iter)
    {
        if(*iter)
            delete *iter;
    }
    m_LogicthreadsQueue.clear();

    g_socket.ngx_clear_connectionPool();        //回收连接池内存
    ngx_log_stderr(0,"StopAllthreads()调用成功线程池内存回收完毕,线程正常结束！！！");

    return;
}

