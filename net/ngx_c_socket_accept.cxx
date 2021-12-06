#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>    //uintptr_t
#include <stdarg.h>    //va_start....
#include <unistd.h>    //STDERR_FILENO等
#include <sys/time.h>  //gettimeofday
#include <time.h>      //localtime_r
#include <fcntl.h>     //open
#include <errno.h>     //errno
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
#include "ngx_c_logic.h"
#include "ngx_c_socket.h"

//建立新连接专用函数，当新连接进入时，本函数会被ngx_epoll_process_events()所调用
void CSocket::ngx_event_accept(ngx_connection_ptr oldc)
{
    struct sockaddr    mysockaddr;        //远端服务器的socket地址
    socklen_t          socklen;
    int                err;
    int                level;
    int                s;
    static int         use_accept4 = 1;   //我们先认为能够使用accept4()函数
    ngx_connection_ptr newc;              //代表连接池中的一个连接【注意这是指针】

    socklen = sizeof(mysockaddr);
    do
    {

        //如果listen套接字是阻塞的
        if(use_accept4)
        {
            //【accept4】函数,可以把返回的套接字直接设置为非阻塞
            //从内核获取一个用户端连接，最后一个参数SOCK_NONBLOCK表示返回一个非阻塞的socket，节省一次ioctl【设置为非阻塞】调用
            s = accept4(oldc->fd, &mysockaddr, &socklen, SOCK_NONBLOCK); 

            //如果用accet的话,需要调用ioctl函数设置非阻塞
            // int nb=1; //0：清除，1：设置  
            // ioctl(s, FIONBIO, &nb) //FIONBIO：设置/清除非阻塞I/O标记：0：清除，1：设置
        }
        //如果listen套接字是非阻塞的
        else
        {
            //不会阻塞,即便已连接队列为空也不会等待,直接返回

            //此函数的返回值是一个新的套接字【客户端套接字,在客户端请求连接时带来的信息】,后续服务使用该套接字与客户端进行通讯,原套接字继续用于监听端口
            //补充:accept阻塞在这里不代表这是一个阻塞I/O函数,而是监听套接字listenfd设置的属性为阻塞模式,也可以设置非阻塞
            //参数二:addr -- 输出一个的sockaddr_in变量地址，该变量用来存放发起连接请求的客户端的协议地址；
            //参数三:addrten -- 作为输入时指明缓冲器的长度，作为输出时指明addr的实际长度。
            //connfd = accept(listenfd,(struct sockaddr*)NULL,NULL);
            s = accept(oldc->fd,&mysockaddr,&socklen);
            //【执行完后mysockaddr储存着远端客户端的协议地址】
        }

            if(s == -1)
            {
                //epoll发生惊群,多个进程同时调用accept(),仅有一个成功返回,其余返回-1
                err = errno;
                //ngx_log_stderr(err,"惊群accept return -1"); 
                //对accept、send和recv而言，事件未发生时errno通常被设置成EAGAIN（意为“再来一次”）或者EWOULDBLOCK（意为“期待阻塞”）
                if(err == EAGAIN) //accept()没准备好，这个EAGAIN错误EWOULDBLOCK是一样的
                {
                    //除非你用一个循环不断的accept()取走所有的连接，不然一般不会有这个错误【我们这里只取一个连接，也就是accept()一次】
                    return ;
                } 
                level = NGX_LOG_ALERT;
                if (err == ECONNABORTED)  //ECONNRESET错误则发生在对方意外关闭套接字后【您的主机中的软件放弃了一个已建立的连接--由于超时或者其它失败而中止接连(用户插拔网线就可能有这个错误出现)】
                {
                    //该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
                    //客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
                    //POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
                    //服务器进程一般可以忽略该错误，直接再次调用accept。
                    level = NGX_LOG_ERR;
                } 
                else if (err == EMFILE || err == ENFILE) //EMFILE:进程的fd已用尽【已达到系统所允许单一进程所能打开的文件/套接字总数】。可参考：https://blog.csdn.net/sdn_prc/article/details/28661661   以及 https://bbs.csdn.net/topics/390592927
                                                        //ulimit -n ,看看文件描述符限制,如果是1024的话，需要改大;  打开的文件句柄数过多 ,把系统的fd软限制和硬限制都抬高.
                                                        //ENFILE这个errno的存在，表明一定存在system-wide的resource limits，而不仅仅有process-specific的resource limits。按照常识，process-specific的resource limits，一定受限于system-wide的resource limits。
                {
                    level = NGX_LOG_CRIT;
                }
                //ngx_log_error_core(level,errno,"CSocekt::ngx_event_accept()中accept4()失败!");

                if(use_accept4 && err == ENOSYS) //accept4()函数没实现，系统不支持accept4
                {
                    use_accept4 = 0;  //标记不使用accept4()函数，改用accept()函数
                    continue;         //回去重新用accept()函数搞
                }

                if (err == ECONNABORTED)  //对方关闭套接字
                {
                    //这个错误因为可以忽略，所以不用干啥
                    //do nothing
                }
                
                if (err == EMFILE || err == ENFILE) 
                {
                    //do nothing，这个官方做法是先把读事件从listen socket上移除，然后再弄个定时器，
                    //定时器到了则继续执行该函数，但是定时器到了有个标记，会把读事件增加到listen socket上去；
                    //我这里目前先不处理吧【因为上边已经写这个日志了】；
                }            
                return;
            } //end if(s == -1)

            //走到这里accept已经成功了
            
            //ngx_log_stderr(0,"成功建立连接socket = %d , pid = %d",s,getpid()); 

            // write(STDERR_FILENO,"TCP connect success!!!\n",23);
            //走到这里的，表示accept4()/accept()成功了        
            newc = ngx_get_connection(s);     //【获取一个连接池对象,记录着一个成功接入的套接字】
            if(newc == NULL)                  //存在危险socket,断开连接
            {
               close(s);
               return;
            }

            //成功拿到连接池中的一个连接【记录下客户端的协议地址】 newc储存有客户端的套接字与客户端的协议地址
            memcpy(&newc->s_sockaddr,&mysockaddr,socklen);

            if(!use_accept4) //系统不支持accept4函数
            {
                if(setnonblocking(s) == false)
                {
                    //设置非阻塞失败.回收连接池中的连接,并关闭socket
                    if(close(s) == -1)
                    {
                        ngx_log_stderr(errno,"setnonblocking(s) close失败");    
                    }
                    ngx_free_connection(newc);      //直接回收【还未加入定时器】
                    return;
                }
            }
            newc->listening = oldc->listening;      //获取监听套接字的结构体指针【记录有监听套接字与其对应的监听端口】
            //newc->w_ready = 1;

            //ngx_log_stderr(0,"开始注册connectin可读事件处理函数 = %d , pid = %d",s,getpid()); 
            //设置处理用户连接套接字的读事件,用来接收处理用户数据
            newc->rhandler  = &CSocket::ngx_read_request_handler_prc1;
            newc->whandler  = &CSocket::ngx_write_surplus_pkg_proc;
            if(ngx_epoll_Regist_event(s,
                                    EPOLL_CTL_ADD,           //注册增加事件
                                    EPOLLIN | EPOLLRDHUP,     
                                    0,
                                    newc) == -1)
            {
                if(close(s) == -1)
                {
                    ngx_log_stderr(errno,"setnonblocking(s) close失败");    
                }
                ngx_free_connection(newc);                 //epoll初始读事件注册失败,直接回收【还未加入定时器】
                return;
            }

            //加入心跳监视器
            if(m_OpenPingTimerEnable == 1)
            {
                AddtoPingTimerQueue(newc);
            }

            ++m_CurOnlinUserCount;                         //在线人数+1
            //ngx_log_stderr(0,"成功注册connectin可读事件处理函数 = %d , pid = %d",s,getpid()); 
            // if(ngx_epoll_add_event( s,
            //                         1,0,
            //                         0,//EPOLLLT,            //edge trigged 边缘触发/边沿触发 高速模式【高效率】【默认水平触发】
            //                         EPOLL_CTL_ADD,
            //                         newc) == -1)
            // {
            //     ngx_close_connection(newc);
            //     return;
            // }
            break;
    }while(1);

    return;
}
void CSocket::ngx_close_connection(ngx_connection_ptr pConn)
{   
    //ngx_log_stderr(errno,"准备回收心跳异常 socket = %d , pid = %d",pConn->fd,getpid()); 

    if(pConn->fd != -1)
    {
       close(pConn->fd);
       pConn->fd = -1;     //释放后fd标记为-1.用于判断包是否过期   
    }

    ngx_delay_close_connection(pConn);    
    return;
}


