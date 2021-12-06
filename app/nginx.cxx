#include"ngx_func.h"			//函数声明相关 
#include"ngx_macro.h"   		//宏相关
#include"errno.h"  				//全局相关
#include"ngx_c_conf.h"  		//配置文件相关
#include "ngx_c_socket.h" 		//Socket相关
#include "ngx_c_logic.h"
#include"ngx_global.h"  		//pid相关
#include "ngx_c_pthreadpool.h"  //线程池相关
#include <arpa/inet.h>
#include"signal.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
//内存释放相关处理
static void freeresource();

//标题有关全局量
size_t g_argvneedmem = 0;     //命令行参数 -> argc内存大小
size_t g_envneedmem  = 0;     //环境变量所需内存大小
int	   g_cpunum  	 = 0;     //cpu数 -用于cpu绑定

int	   g_os_argc;		      //命令行参数个数
char** g_os_argv;		      //储存命令行参数的首地址
char*  gp_envmem = NULL;      //指向自己分配的环境变量 - 在ngx_ngx_setproctitle中实现分配

//进程相关变量
pid_t   ngx_pid;   			  				//当前进程pid
pid_t   ngx_parent;      	  				//父进程的pid
int 	exitcode = 0;		  				//程序退出状态
int 	g_damon_iswork = 0;   				//守护进程是否启用,0未启用 1启用
int 	ngx_process_type;     				//进程类型 0: master进程 1: worker进程
bool 	m_shutdown = false;   				//线程池标志 false:退出 true:不退出
sig_atomic_t g_worker_breakcount = 0; 	  	//worker进程终止时 +1

//socket相关【CSocket的派生类】
CLogicSocket g_socket; 		  //主要储存监听套接字句柄与epoll相关信息

//线程池相关
CThreadPool ThreadsPool;

int main(int argc, char* const * argv)
{		

	//初始化全局变量
	ngx_pid    = getpid();     					//取得当前进程pid
	ngx_parent = getppid();    					//取得父进程的id
	g_cpunum   = sysconf(_SC_NPROCESSORS_ONLN); //获取CPU实际可用核数

	g_os_argc = argc;			//记录命令行参数个数
	g_os_argv = (char**)argv;	//记录命令行参数首地址
	ngx_process_type = NGX_MASTER_PROCESS;
	
	
	//统计argv所占的内存
	for (int i = 0; i < argc;++i)
	{
		g_argvneedmem += strlen(argv[i]) + 1; //+1添加 '\0'的大小
	}
	//统计环境变量所占内存
	//environ是系统定义的储存环境变量的指针数组,结束标记为NULL
	for (int i = 0; environ[i]; ++i)
	{
		g_envneedmem += strlen(environ[i]) + 1;
	}
	
	CConfig *p_config = CConfig::GetInstance(); //单例类生成
	if (p_config->Load("nginx.conf") == false)  //把配置文件内容载入到内存     
	{
		ngx_log_init();		    //日志初始化
		ngx_log_stderr(NGX_LOG_EMERG,"main:Load(\"/nginx.conf\")配置文件载入失败\n");
		exitcode = 2;
		goto labexit;
	}
	ngx_log_init();  			//日志初始化
	    
	if(ngx_init_signals() == -1)//信号初始化
	{
		ngx_log_error_core(NGX_LOG_EMERG,errno,"ngx_init_signals()信号注册失败");
		exitcode = -1;
		goto labexit;
	}
	
	// if(g_socket.Initialize() == false) //【socket初始化】监听端口
	// {
	// 	exitcode = -1;
	// 	goto labexit;
	// }

	ngx_init_setproctitle();	//迁移环境变量

	//获取daemon process配置信息【是否以守护进程的方式运行master进程】
	if(p_config->GetIntDefault("Daemon",0) == 1)//1:是 0:否
	{
		int daemon_result = ngx_daemon();
		if(daemon_result == -1) //守护进程创建失败
		{
			exitcode = -1;
			goto labexit;
		}
		if(daemon_result == 1) //父进程进程返回
		{
			exitcode = 0;
			return exitcode; //父进程退出,使守护进程成为孤儿进程被init领养 ppid = 1
		}
		//走到这里的只有守护进程,标记1,此守护进程作为后续的master进程
		g_damon_iswork = 1;
	}//end if 守护进程成为master进程
	
	g_socket.GetConfInfo();		   //读取配置项

	
	if(p_config->GetIntDefault("ThreadBindCpuEnable",0) == 1)//1:是 0:否
	{
		if(SetCpuAffinity(0) == -1)//绑定当前进程到固定CPU:0[当前计算机有8个逻辑CPU] 00000001表示绑定第一个CPU,00000010表示绑定第二个CPU以此类推
		{
			exitcode = -1;
			goto labexit;
		}				   					 
    }
	
	if(p_config->GetIntDefault("ThreadPolicyAlterEnable",0) == 1)
    {  
		if(CpuOptimize() == -1)
		{
			exitcode = -1;
			goto labexit;
		}
	}

	//worker process 进程创建
	ngx_master_process_cycle();
	
labexit:
	ngx_log_stderr(0,"程序结束,再见\n");
	freeresource();
	return exitcode;
	
}
void freeresource()
{
	if (gp_envmem)
	{
		//释放环境变量内存
		delete []gp_envmem;
		gp_envmem = NULL;
	}

	//关闭文件,处理缓冲区
	if (ngx_log.fd > STDERR_FILENO)
	{
		printf("success close(ngx_log.fd)\n");
		close(ngx_log.fd);
	}
}