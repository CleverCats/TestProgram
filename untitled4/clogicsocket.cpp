#include "clogicsocket.h"
#pragma warning(disable:4819)
/*成员函数指针*/
typedef bool (CLogicSocket:: *_HandleSLogicFun)(lconn _pConn ,   //给客户端分配的连接池节点
              LPMSG_HEADER _pMsgHeader  ,     //消息头
              char *pPkgBody            ,     //包体
              unsigned short pPkgBodyLenth);  //包头长
static const _HandleSLogicFun SLogicFun[] =  {
//实现服务器基本功能函数
    &CLogicSocket::_HandlePing,           //【0】心跳包
    NULL,                                 //【1】
    NULL,                                 //【2】
    NULL,                                 //【3】
    NULL,                                 //【4】
};
