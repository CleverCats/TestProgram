#ifndef CLOGICSOCKET_H
#define CLOGICSOCKET_H
#include "c_struct.h"
#include "csocket.h"
#pragma warning (disable:4819)
class CLogicSocket :public CSocket
{
    Q_OBJECT
public:
    CLogicSocket(){};
    ~CLogicSocket(){};
public:
     /*心跳包 - 空包*/
    bool _HandlePing(lconn _pConn ,                     //给客户端分配的连接池节点
                     LPMSG_HEADER _pMsgHeader  ,        //消息头
                     char *pPkgBody            ,        //包体
                     unsigned short pPkgBodyLenth);     //包头长

void SendNoBodyPkgtoClient(LPMSG_HEADER _MsgHeadle , unsigned short _msgcode);
signals:
    void   CallSendSlotFun(void *Con, char* buff, unsigned int buflen);
protected slots:
    void   sendproc(void *Con, char* buff, unsigned int buflen); //发包【发包线程主动调用】
};

#endif // CLOGICSOCKET_H
