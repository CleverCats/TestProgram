//函数声明放在这个头文件

#ifndef __NGX_FUNC_H__
#define __NGX_FUNC_H__
#include "ngx_global.h"

//进程相关函数
void ngx_master_process_cycle();
int SetCpuAffinity(int cpuindex, pid_t pid = 0);                                        //设置CPU亲和性[绑定CPU]
int CpuOptimize();                                                                      //CPU优化[进程调度策略与优先级]
void InitschedInfo(struct sched_param *sched, int priority, int policy, int act);       //初始化sched_param结构体信息
int  SetThreadPolicyAndPoritory(int policy, int poritory, pid_t pid = 0, int act = -1); //设置进程调度策略与优先级
int  GetThreadPolicyInfo(pthread_attr_t *attr);                                         //打印进程调度策略
void ShowThreadPriorityInfo(pthread_attr_t *attr, int policy);                          //打印进程优先级信息
//void SetThreadPolicy(pthread_attr_t *attr,int policy);                                  //设置OS进程调度策略
//字符串相关函数
void   Rtrim(char *string);
void   Ltrim(char *string);

//环境变量相关函数
void ngx_init_setproctitle();						     //环境变量信息迁移
void ngx_setproctitle(const char *title);                //执行进程名设置

//日志打印输出相关函数
void ngx_log_init();									 //初始化日志(从配置文件读取日志相关信息)
void ngx_log_stderr(int err, const char *fmt, ...);		 //标准错误日志打印(打印到屏幕 + 文件)
void ngx_log_error_core(int level, int err, const char *fmt, ...); //打印文件日志(打印到文件)

u_char *ngx_log_errno(u_char *buf, u_char *last, int err);//打印错误提示
u_char *ngx_vslprintf(u_char *buf, u_char *last, const char *fmt, va_list args);
u_char *ngx_snprintf(u_char *buf, size_t max, const char *fmt, ...);
u_char *ngx_slprintf(u_char *buf, u_char *last, const char *fmt, ...);

//信号相关
int ngx_init_signals();

//守护进程相关
int ngx_daemon();

#endif  