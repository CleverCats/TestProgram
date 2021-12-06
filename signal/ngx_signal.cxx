#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>    //信号相关头文件 
#include <errno.h>     //errno
#include <sys/wait.h>  //waitpid
#include <unistd.h>
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h" 

typedef struct 
{
    int             signo;      //信号对应数字编号
    const char      *signame;   //信号对应名字,例如SIGHUP
    
    //信号处理函数,siginfo_t是系统定义的结构
    void (*heandler)(int signo,siginfo_t *siginfo,void *ucontext);
}ngx_signal_t;

//声明信号处理函数,static修饰函数表示限制该函数只在当前文件可见,固定参数,系统要求
static void ngx_signal_handler(int signo,siginfo_t *siginfo,void *ucontext);

//获取子进程结束状态函数,防止单独kill子进程时,子进程变成僵尸进程
static void ngx_process_get_status(void);

//定义需要处理的各种信号,
ngx_signal_t signals[] = {
    
    { SIGHUP,    "SIGHUP",           ngx_signal_handler },        //终端断开信号，对于守护进程常用于reload重载配置文件通知--标识1
    { SIGINT,    "SIGINT",           ngx_signal_handler },        //标识2   
	{ SIGTERM,   "SIGTERM",          ngx_signal_handler },        //标识15
    { SIGCHLD,   "SIGCHLD",          ngx_signal_handler },        //子进程退出时，父进程会收到这个信号--标识17
    { SIGQUIT,   "SIGQUIT",          ngx_signal_handler },        //标识3
    { SIGIO,     "SIGIO",            ngx_signal_handler },        //指示一个异步I/O事件【通用异步I/O信号】
    { SIGSYS,    "SIGSYS, SIG_IGN",  NULL               },        //我们想忽略这个信号，SIGSYS表示收到了一个无效系统调用，如果我们不忽略，进程会被操作系统杀死，--标识31
    { SIGUSR1,   "SIGUSR1",          ngx_signal_handler },        //自定义信号【输出服务器运行状态】
                                                                  //所以我们把handler设置为NULL，代表 我要求忽略这个信号，请求操作系统不要执行缺省的该信号处理动作（杀掉我）
    //...日后根据需要再继续增加
    { 0,         NULL,               NULL               }         //信号对应的数字至少是1，所以可以用0作为一个特殊标记
};

//初始化信号函数,用于注册信号处理程序,返回值:0 成功 , -1 失败
int ngx_init_signals()
{
    ngx_signal_t  *sig; //声明一个自定义结构体指真,包含信号编号,名字及对应信号处理函数
    struct sigaction sa;

     /*struct sigaction{
        void (*sa_handler)(int);
        sigset_t sa_mask;
        int sa_flag;
        void (*sa_sigaction)(int,siginfo_t *,void *);
    };*/

    for(sig = signals; sig->signo != 0; ++sig)
    {
            //清空内存
            memset(&sa,0,sizeof(struct sigaction));

            if(sig->heandler)
            {
            /*sa_handler 是一个函数指针，其含义与 signal 函数中的信号处理函数类似。成员
            sa_sigaction 则是另一个信号处理函数，它有三个参数，可以获得关于信号的更详细的信息。当 sa_flags 成员的值
            包含了 SA_SIGINFO 标志时，系统将使用 sa_sigaction 函数作为信号处理函数，否则使用 sa_handler 作为信号处理
            函数。在某些系统中，成员 sa_handler 与 sa_sigaction 被放在联合体中，因此使用时不要同时设置。*/
                sa.sa_sigaction = sig->heandler;
                //int型,作为一个标记,设置为SA_SIGINFO可以理解为允许sa.sa_sigaction指定的信号处理程序(函数)生效
                sa.sa_flags = SA_SIGINFO;//(标记(SA_SIGINFO)，就表示信号附带的参数可以被传递到信号处理函数中)
            }
            else
            {
                //走到这里表示没有信号处理函数,那么就将该信号忽略掉,也不让其进行默认处理,因为默认下大多数信号会杀掉进程
                //printf("Signal %d:sa.sa_handler = SIG_IGN \n",sig->signo);
                sa.sa_handler = SIG_IGN;
            }
            //清空信号集,不屏蔽
            sigemptyset(&sa.sa_mask);
        //信号处理函数  和signal()函数相识,但是更安全
        //参数1：要处理的信号
        //参数2：struct sigaction结构体变量包含信号处理函数以及执行信号处理函数时候要屏蔽的信号等等内容
        //参数3：oldact参数输出先前信号的处理方式（如果不为NULL的话）【跟sigprocmask()函数边的第三个参数相同】，跟参数2同一个类型,这里不需要这个东西，所以直接设置为NULL；
        if(sigaction(sig->signo,&sa,NULL) == -1) 
        {
            //注册信号捕捉函数失败
             ngx_log_stderr(NGX_LOG_EMERG,"FAILE CATCH SIGNAL: %s...\n",sig->signame);
             return -1; //有失败就直接返回
        }
        // // else
        // // {       
        // //     //注册信号捕捉函数成功
        // //      printf("SUCCESS CATCH SIGNAL: %s!!!\n",sig->signame);
        // //     //ngx_log_error_core(NGX_LOG_EMERG,errno,"sigaction(%s) succed!",sig->signame);     //成功不用写日志 
        // //     //ngx_log_stderr(0,"sigaction(%s) succed!",sig->signame); //直接往屏幕上打印看看 ，不需要时可以去掉
        // // }//end for
    }  
    return 0; //成功  
}

static void ngx_signal_handler(int signo,siginfo_t *siginfo,void *ucontext)
{
    // if(signo == SIGCHLD)
    // {
    //     int status;
    //     //参数WNOHANG表示无论子僵尸进程是否被终止,不可以阻塞在这里,会立即返回
    //     //返回值:子僵尸进程未被终止返回0,waitpid函数调用失败返回-1,成功终止子僵尸进程返会处理的的僵尸进程pid
    //     pid_t pid = waitpid(-1,&status,WNOHANG); 
    //     if(pid == 0 || pid == -1)  //立即返回
    //         return;
    //     //走到这里表示waitpid函数调用成功pid接收的返回值为终止进程的id
    //     // //  printf("pid = %d\n",(int)pid);
    //     // //  return;
    // }

    ngx_signal_t *sig;
    char         *action; //用于记录一个动作,并写入日志
    for(sig = signals;sig->signo != 0; ++sig)
    {
        if(sig->signo == signo )
        {
            //找到对应信号,break出来
            break;
        }
    }//end for

   action = (char*)"";      //目前还没有什么动作；
   if(ngx_process_type == NGX_MASTER_PROCESS)
   {
       switch(signo)
       {
        case SIGCHLD:                   //子进程退出,父进程会接受到该信号
                ++g_worker_breakcount;  //子进程终止
                break;
        case SIGUSR1:
                g_socket.MonitoringAndOutPutServerInfo();    //打印server信息
                break;
        //.....其他信号处理以后待增加
        default:
            break;
        } //end switch
    }
    else if(ngx_process_type == NGX_WORKER_PROCESS) //worker进程，具体干活的进程，处理的信号相对比较少
    {
        switch(signo)
        {
      
        //worker进程的往这里走
        //......以后再增加
        //....
        default:
            break;
        } //end switch
    }
    else
    {
        //非master非worker进程,暂不处理
    }//end if

    if(siginfo && siginfo->si_pid)
    {
        ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received from %P%s", signo, sig->signame, siginfo->si_pid, action); 
    }
    else
    {
        //暂时不打印
        //ngx_log_error_core(NGX_LOG_NOTICE,0,"signal %d (%s) received %s",signo, sig->signame, action);//没有发送该信号的进程id，所以不显示发送该信号的进程id
    }
    //.......其他需要扩展的将来再处理；

    //子进程状态有变化，通常是意外退出【既然官方是在这里处理，我们也学习官方在这里处理】
    if (signo == SIGCHLD) 
    {
        ngx_process_get_status(); //获取子进程的结束状态
    } //end if
    return;
}
static void ngx_process_get_status(void)
{
    pid_t            pid;
    int              status;
    int              err;
    int              one = 0; //标记信号正常处理过一次

    for (;;) 
    {
        //参数WNOHANG表示无论子僵尸进程是否被终止,不可以阻塞在这里,会立即返回
        //返回值:子僵尸进程未被终止返回0,waitpid函数调用失败返回-1,成功终止子僵尸进程返会处理的的僵尸进程pid
        pid = waitpid(-1, &status, WNOHANG); 
        if(pid == 0) //子进程没结束，会立即返回这个数字，但这里应该不是这个数字【因为一般是子进程退出时会执行到这个函数】
        {
            return;
        } //end if(pid == 0)
            //-------------------------------
        if(pid == -1)//调用失败
        {
            //这里处理代码来自官方nginx源码,比较成熟
            err = errno;
            if(err == EINTR)           //调用被某个信号中断
            {
                continue;
            }

            if(err == ECHILD  && one)  //没有子进程
            {
                return;
            }

            if (err == ECHILD)         //没有子进程
            {
                ngx_log_error_core(NGX_LOG_INFO,err,"waitpid() failed!");
                return;
            }
                ngx_log_error_core(NGX_LOG_ALERT,err,"waitpid() failed!");
                return;
        }  //end if(pid == -1)
            //-------------------------------
                  //走到这里，表示  成功【返回进程id】,打印一些日志来记录子进程的退出
        one = 1;  //标记waitpid()返回了正常的返回值
        if(WTERMSIG(status))  //获取使子进程终止的信号编号
        {
            ngx_log_error_core(NGX_LOG_ALERT,0,"pid = %P exited on signal %d!",pid,WTERMSIG(status)); //获取使子进程终止的信号编号
        }
        else
        {
            ngx_log_error_core(NGX_LOG_NOTICE,0,"pid = %P exited with code %d!",pid,WEXITSTATUS(status)); //WEXITSTATUS()获取子进程传递给exit或者_exit参数的低八位
        }//end for
    }
     return;
}


