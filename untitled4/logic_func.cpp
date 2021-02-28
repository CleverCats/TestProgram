#include <WinSock2.h>
#include "c_macro.h"
#include "clogicsocket.h"

void  CLogicSocket::sendproc(void *Con, char* buff, unsigned int buflen)
{  
        lconn c = (lconn)Con;
        //ngx_log_stderr(0,"开始发包");
        c->issendlen = 0;
        while(true)
        {
            c->issendlen = c->_conn->write(buff, buflen);

            if(c->issendlen >= 0)
            {
                c->SnedSlotOverlable = true;
                //qDebug()<< "send len = " << c->issendlen <<endl;
                return;
            }
            else if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                qDebug("send buffer is fulling");
                c->issendlen = -1;
                c->SnedSlotOverlable = true;
                return;
            }
            else if(errno == EINTR)
            {
                qDebug("errno == EINTR");
                //因为信号引发的错误,不算真正的错误,等待下次while再次尝试
                //continue;
            }
            else
            {
                //连接异常断开
                c->issendlen = -2;
                c->SnedSlotOverlable = true;
                return;
            }
       }

}

void CLogicSocket::SendNoBodyPkgtoClient(LPMSG_HEADER _MsgHeadle , unsigned short _msgcode)
{
    char *emptyBodyPkgBuf = new char[m_MsgHeaderLen + m_PkgHeaderLen]();
    char *sendbuffer        = emptyBodyPkgBuf;
    memcpy(sendbuffer , _MsgHeadle , m_MsgHeaderLen);
    sendbuffer              = sendbuffer + m_MsgHeaderLen;
    LPCOMM_PKG_HEADER Pkgheader = (LPCOMM_PKG_HEADER)sendbuffer;

    Pkgheader->pkgLen       = htons(m_PkgHeaderLen);       //包长 = 包头长【空包】
    Pkgheader->msgCode      = htons(_msgcode);             //消息代码0
    Pkgheader->crc32        = htonl(0);                    //crc32验证码0

    InputSendDataQueue(emptyBodyPkgBuf);                   //入队列
}

bool CLogicSocket::_HandlePing(lconn Con ,                     //给客户端分配的连接池节点
                        LPMSG_HEADER _pMsgHeader   ,           //消息头
                        char *pPkgBody             ,           //包体
                        unsigned short pPkgBodyLenth)          //包体长
{
    if(pPkgBodyLenth != 0)                                     //心跳包,包体长为0【空包】
        return false;

     //std::lock_guard<std::mutex>LogicLock(Con->logiclock);                        //线程逻辑互斥
    Con->lastPingtime = time(NULL);                             //刷新ping last_time
    SendNoBodyPkgtoClient(_pMsgHeader , _CMD_PING);             //发送心跳包

    //ngx_log_stderr(0,"成功发送了心跳包");
    return true;
}
