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
#include "ngx_c_memory.h"
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"


//连接池函数
ngx_connection_ptr CSocket::ngx_get_connection(int isock)
{
    //当前在线人数 > 最大限制人数, 拒绝连入
    if(m_CurOnlinUserCount >= m_workConnections)
    {
        ngx_log_stderr(0,"超出系统允许的最大连入用户数(最大允许连入数%d)，关闭连入请求(%d)\n",m_workConnections); 
        return NULL;
    }

    //若存在连接不断连入, 然后断开, 会导致连接池分配过大【连接池使用new分配】, 无线扩张会导致崩溃
    //认定为恶意用户, 如果此时空闲连接池数 < 最大限制人数, 拒绝连入服务
    if(m_connectpoollist.size() > m_workConnections * 5)
    {
        if(m_freeconnectpoollist.size() < m_workConnections)
        {
            ngx_log_stderr(0,"连接池过大且空闲连接小于最大限制在线人数, 可能存在恶意短连接, 拒绝连接"); 
            return NULL;
        }
    }

    CLOCK GetLock(&m_allocConMuext);                              //取连接和去连接应该使用一个锁,防止
    if(!m_freeconnectpoollist.empty())
    {
        ngx_connection_ptr p_Conn = m_freeconnectpoollist.front(); //取顶【获取空闲链接结点】
        m_freeconnectpoollist.pop_front();                         //去顶
        p_Conn->GetOneToUse();                                     //初始化空闲连接
        --m_free_connections_num;                                  //空闲-1
        p_Conn->fd = isock;                                        //记录套接字【前两次是监听套接字,后续是已连接套接字】
        return p_Conn;                                     
    }

//无空闲连接,动态分配
ngx_connection_ptr p_Conn;
CMemory *p_memory = CMemory::GetInstance();
int ilenconnpoolnode = sizeof(ngx_connection_type);
p_Conn = (ngx_connection_ptr)p_memory->AllocMemory(ilenconnpoolnode,true); //定位new,将ngx_connection_type填充到p_Conn中,不调用构造函数【因为节点需要重复利用,不可能重复调用构造函数初始化】
p_Conn = new(p_Conn) ngx_connection_type();                                //调用构造函数【构造函数中仅初始化iCurrsequence和LocgicLock】
p_Conn->GetOneToUse();                      //初始化该连接节点【不放入构造函数因为连接是重复利用,不可能重复调用构造函数】
++m_connections;                            //总连接池节点+1
p_Conn->fd = isock;                         //记录套接字
m_connectpoollist.push_back(p_Conn);        //链接放入到连接池
return p_Conn;


// //     ngx_connection_ptr c = m_pfree_connection; //获取空闲链接结点

// //     //链接被耗尽
// //     if(c == NULL)
// //     {
// //         ngx_log_stderr(0,"CSocket::ngx_get_connection()中空闲链接为空!!!\n");
// //         return NULL;
// //     }

// //     m_pfree_connection = c->data;   //指向下一块空闲链接结点
// //     m_free_connections_num--;       //空闲-1

// //     //需要用的一些数值,先保留到临时变量中去
// //     uintptr_t instance = c->instance;
// //     uint64_t  iCurrsequence = c->iCurrsequence;

// //     //清空待分配出去的空闲链接结点内存
// //     memset(c,0,sizeof(ngx_connection_type));
// //     c->fd = isock;   //保存监听套接字

// //     //.......
// //     c->instance      = !instance;   //这个值有用,所以还原
// //     c->iCurrsequence = iCurrsequence;

// //     //++c->iCurrsequence;           //每次取用该值就+1
// //     // c->isnewMemory       = false;   //初始未分配报文缓存区
// //     // c->pnewMemoryPointer = NULL;    //报文缓存区首地址
// //     // c->PrecvBuf          = c->DataHeadInfo;          //每个新的C/S链接,PrecvBuf指向缓存区首地址
// //     // c->CurStat           = _PKG_HEAD_INIT;           //设置新连接为待接包状态[状态机]
// //     // c->nexTorecvLen      = sizeof(COMM_PKG_HEADER);  //待接收包长[主要用于解决缺包装态]
// //     return c;//返回空闲链接结点
}