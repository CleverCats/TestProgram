#include <errno.h>
#include <sys/stat.h>
#include "ngx_func.h"
#include <fcntl.h>
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include <unistd.h>
//守护进程初始化
int ngx_daemon()
{
    switch (fork())
    {
    case -1: //fork()失败
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中 fork() 失败\n");
        return -1; //直接return
    case 0: //子进程break
        break;
    default:
        return 1; //父进程退出
    }//end switch

    //子进程走下来
    
    ngx_parent = ngx_pid; //ppid
    ngx_pid = getpid();   //pid

    if(setsid() == -1) //创建一个新的会话,成为session leader 脱离终端
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中 sedside失败 pid=%d\n",ngx_pid);
        return -1;
    }
    umask(0);//放开所有文件权限

    int fd = open("/dev/null",O_RDWR); //读写方式打开空设备,获取文件描述符
    if(fd == -1)
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中open(\"/dev/null\")失败 pid=%d\n",ngx_pid);
        return -1;
    }
    if(dup2(fd,SIDIN_FILENO) == -1)//dup2先关闭SIDIN_FILENO(标准输入)在指向fd
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中dup2(fd,SIDIN_FILENO)失败 pid=%d\n",ngx_pid);
    }
     if(dup2(fd,SIDOUT_FILENO) == -1)//dup2先关闭SIDIN_FILENO(标准输出)在指向fd
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中dup2(fd,SIDOUT_FILENO)失败 pid=%d\n",ngx_pid);
    }
    if(fd > SIDERR_FILENO)// fd > 3
    {
        if(close(fd) == -1)
        {
            ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_daemon()中close(fd) 失败 pid=%d\n",ngx_pid);
            return -1;
        }
    }
    else
    {
        ngx_log_error_core(NGX_LOG_EMERG,errno,"空设备打开异常,fd < SIDERR_FILENO\n");
        return -1;
    }
    
    
    //守护进程创建成功
    return 0;
}