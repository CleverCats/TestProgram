#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>   //信号相关头文件 
#include <errno.h>    //errno
#include "ngx_func.h"
#include <sys/wait.h>  //waitpid and 信号集相关
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_c_conf.h"
#include "ngx_c_socket.h"
#include "ngx_c_pthreadpool.h"

//进程相关函数及变量声明
static u_char master_process[]="master process";
static void ngx_worker_process_init(int inum);
static void ngx_start_worker_process(int threadnums);
static int ngx_spwan_process(int inum,const char* pprocname);
static void ngx_worker_process_cycle(int inum,const char* pprocname);

//创建worker子进程
void ngx_master_process_cycle()
{

    sigset_t mask;      //信号集
    sigemptyset(&mask); //清空信号集,无阻塞   
    /*{ SIGHUP,    "SIGHUP",           ngx_signal_handler },      //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },  */

    //设置信号屏蔽
    //注意：调用这个函数才能改变进程的屏蔽字，之前的函数都是为改变一个变量的值而已，并不会真正影响进程的屏蔽字。
    sigaddset(&mask, SIGCHLD);     //子进程状态改变
    sigaddset(&mask, SIGALRM);     //定时器超时
    sigaddset(&mask, SIGIO);       //异步I/O
    sigaddset(&mask, SIGINT);      //终端中断符
    sigaddset(&mask, SIGHUP);      //连接断开
    sigaddset(&mask, SIGUSR1);     //用户定义信号
    sigaddset(&mask, SIGUSR2);     //用户定义信号
    sigaddset(&mask, SIGWINCH);    //终端窗口大小改变
    sigaddset(&mask, SIGTERM);     //终止
    sigaddset(&mask, SIGQUIT);     //终端退出符
    
   /***注意:此时如果设置 sigemptyset(&mask); 这种操作只是对信号集变量mask的操作,不会影响当前进程的信号集***
    ***只有sigprocmask函数才可以将当前进程的信号状态通过与mask信号集的一些操作来改变***/

    //进程绑定信号集
    //参数一为SIG_BLOCK表示将当前信号集状态与参数二信号集求并集作为当前进程信号集状态
    if(sigprocmask(SIG_BLOCK,&mask,NULL) == -1)
    {
        //信号集绑定失败 打印错误日志
         ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_master_process_cycle()中sigprocmask()失败!\n");
         
    }
    //设置进程标题
    size_t size;
    //记录标题字符大小 argv参数长度+sizeof(master_process)
    size = g_argvneedmem + sizeof(master_process);
    //标题长度 < 1000 bit
    if(size < 1000 )
    {
        char title[1000] = {0};
        //拷贝进程名字符串,将参数二的指针指向的字符串(包括'\0')覆盖(不是全覆盖)到由title开始的内存
        strcpy(title,(const char*)master_process);

        //尾接空格(连接字符串,从上一个字符的'\0'开始覆盖)
        strcat(title," ");
        //printf("title = %s\n",title);
        for(int i =0;i< g_os_argc;++i)
        {
            //连接字符串,从上一个字符的'\0'开始覆盖
            strcat(title,g_os_argv[i]);
        }

        ngx_setproctitle(title);
        //printf("master process title set success!!!\n");
        ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【master进程】启动并开始运行...!\n",title,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    }
    
    CConfig *p_config = CConfig::GetInstance(); //获取单例类对象
    int workprocess = p_config->GetIntDefault("WorkerProcesses",1); //获取配置进程数

    //创建子进程(只有master进程可以从这个函数出来)
    ngx_start_worker_process(workprocess);

    sigemptyset(&mask); //清空信号屏蔽字变量:清空的只是这个变量,和当前系统的信号集无关,不会影响
    /***注意:此时设置 sigemptyset(&mask); 这种操作只是对信号集变量mask的操作,不会影响当前进程的信号集***
    ***只有ssigemptyset 函数才可以将当前进程的信号状态通过与mask信号集的一些操作来改变***/
   
   //走到这里信号master进程还是由之前设置的信号屏蔽字屏蔽信号
   pid_t masterpid = getpid();
    for(;;)
    {
        //sigsuspend函数:
        //前提:以下操作被包装成为原子操作,不可打断,从而避免调用sigaction引发的可能丢失信号问题
        //1.进程首先会进当前进程的信号集设置为新的mask，并记录oldmask【例:SIG_SETMASK参数的作用】(这里mask为空集,所以设置成不阻塞信号)
        //2.进程会立即阻塞到这里(进程挂起,不占用cpu)此时master进程完全靠驱动干活,直到一个信号的到来
        //3.接收该信号,如果有对应的信号处理函数,则调用该函数,并同时立即恢原复先的进程信号集屏蔽状态(oldmask)即未调用sigsuspend(&mask)之前状态
            //也就是说在执行信号处理函数的时候,已经开始屏蔽其它信号了,当前信号处理函数不会被其他信号打断
        //4.信号处理函数返回后,然后sigsupend函数返回,程序流程往下执行
       
        //在这里阻塞挂起,等待信号
        sigsuspend(&mask);  //这里面其实包含了对sigaction的调用,但因为该函数对一系列操作进行了包装,使其成为原子操作,不可打断
        //不使用sigaction的原因:当调用该函数来取消对原来信号的屏蔽时,正取消时来了的信号可能会丢掉
        //这是操作系统的历史问题,是我们不希望发生的,这里放在for(;;)的的master进程中显然不合适

        //走到这里信号已经恢复原先屏蔽状态
        //printf("执行完信号%d\n");
        //sleep(1);
    }
}
static void ngx_start_worker_process(int threadnums)
{
    //mastsr进程调用,for生成threadnum个进程
    for(int i = 0;i < threadnums; ++i)
    {
        //子进程名做参数
        ngx_spwan_process(i,"worker process");

    }//master end for
    return;  //仅master执行return
}
static int ngx_spwan_process(int inum,const char* pprocname)
{
    pid_t pid = getpid();
    pid = fork(); //生成子进程

    switch(pid)
    {
        case -1:  //fork()失败,打印日志
        {
            ngx_log_error_core(NGX_LOG_ALERT,errno,"ngx_spawn_process() fork()子进程失败 inum=%d\n",inum);
            return -1;
        }
        case 0:  //子进程进来
        {
            //worker进程标识
            ngx_process_type = NGX_WORKER_PROCESS;
            //记录当前父进程id
            ngx_parent = ngx_pid; //写时复制,所以这里的ngx_parent已经是每个子进程独有的了
            //记录当前进程id
            ngx_pid = getpid();   //同上

            //printf("fork() success!\n");
            //所有子进程进入此函数,并不会在出来
            ngx_worker_process_cycle(inum,pprocname);
        }
        default:
            break; //master 进程出去
    }
    //返回master进程pid
    return pid;
}
static void ngx_worker_process_cycle(int inum,const char* pprocname)
{
    CConfig *p_config = CConfig::GetInstance(); //获取配置

    if(p_config->GetIntDefault("ThreadBindCpuEnable",0) == 1)       //1:是 0:否
	{
		if(SetCpuAffinity(inum + 1) == -1)     //绑定当前进程到固定CPU
        {
            exit(-2);
        }		   					 
    }
    
    if(p_config->GetIntDefault("ThreadPolicyAlterEnable",0) == 1)
    {  
		if(CpuOptimize() == -1)                //设置进程调度策略及优先级
		{
			exit(-2);
		}
	}

    if(g_socket.Initialize() == false)         //【socket初始化】监听端口
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"pid = %d, Initialize()失败!",getpid());
        exit(-2);
    }

    int ThreadProcessNum = p_config->GetIntDefault("ThreadProcess",1);
    
    ngx_setproctitle(pprocname); //设置标题 worker process

    //创建线程池
    //初始化子进程前【对外服务前】生成线程池,之后才可以对外服务
    if((ThreadsPool.CreatThreadPool(ThreadProcessNum)) == false)        //创建线程池
    {
        ThreadsPool.StopAllthreads();              //回收所有线程
        exit(-2);                                  //暴力退出,其它资源由系统释放
    }

    //初始化子进程(初始化子进程无阻塞信号集  初始化epoll【创建epoll对象 并注册每个监听套接字的epoll事件处理函数】)
    ngx_worker_process_init(inum);     
    
    ngx_log_error_core(NGX_LOG_NOTICE,0,"%s %P 【worker进程】启动并开始运行...!",pprocname,ngx_pid); //设置标题时顺便记录下来进程名，进程id等信息到日志
    for(;;)
    {
        // sleep(1);
        g_socket.ngx_epoll_process_events(-1);
        //子进程在这里死循环

        //监控信息
        g_socket.MonitoringAndOutPutServerInfo();

    }
    ThreadsPool.StopAllthreads();            //回收线程池所有线程
    g_socket.shoutdown_subproc();            //回收延迟回收进程与发包进程
}

static void ngx_worker_process_init(int inum)
{
    sigset_t mask;      //信号集

    sigemptyset(&mask); //清空信号屏蔽
    //参数一SIG_SETMASK表示将当前进程信号集设置为mask状态成功返回0
    if(sigprocmask(SIG_SETMASK,&mask,NULL) == -1) //设置子进程信号集,无阻塞
    {
        ngx_log_error_core(NGX_LOG_ALERT,errno,"sigprocmask(SIG_SETMASK,&mask,NULL)失败!");
    }

    g_socket.ngx_epoll_init();     //初始化epoll内容,监听端口,向监听套接字上添加监听事件,从而让监听端口履行职责
    //...
    return;
}