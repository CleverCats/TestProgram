#ifndef __CLOGICSOCK__H__
#define __CLOGICSOCK__H__

#include <sys/socket.h>
#include "ngx_c_cstruct.h"
#include "ngx_c_socket.h"

//处理业务逻辑和通讯类【继承CSocket】
class CLogicSocket : public CSocket
{
public:
    CLogicSocket();                   //构造
    virtual ~CLogicSocket();          //析构

    virtual bool Initialize();                                              //初始化
    virtual void ThreadRecvProcFunc(char *pMsgBuffer);                      //处理用户需求
    virtual void procPingTimeOutChecking(LPMSG_HEADER pMsgHeader , time_t cur_time);

public:
     /*心跳包 - 空包*/
    bool _HandlePing(ngx_connection_ptr _pConn ,        //给客户端分配的连接池节点
                         LPMSG_HEADER _pMsgHeader  ,    //消息头
                         char *pPkgBody            ,    //包体
                         unsigned short pPkgBodyLenth); //包头长

    /*注册*/
    bool _HandleRegister(ngx_connection_ptr _pConn ,     //给客户端分配的连接池节点
                         LPMSG_HEADER _pMsgHeader  ,     //消息头
                         char *pPkgBody            ,     //包体
                         unsigned short pPkgBodyLenth);  //包头长

    /*登陆*/          
    bool _HandleLogin(ngx_connection_ptr _pConn     ,     //给客户端分配的连接池节点
                         LPMSG_HEADER _pMsgHeader   ,     //消息头
                         char *pPkgBody             ,     //包体
                         unsigned short pPkgBodyLenth);   //包头长

    /*其它消息*/
    bool _OtherMsg(ngx_connection_ptr _pConn     ,        //给客户端分配的连接池节点
                         LPMSG_HEADER _pMsgHeader   ,     //消息头
                         char *pPkgBody             ,     //包体
                         unsigned short pPkgBodyLenth);   //包头长

};

#endif