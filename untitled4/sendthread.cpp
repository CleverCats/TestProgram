#include <WinSock2.h>
#include "c_global.h"
#include "sendthread.h"
sendthread::sendthread(){}
void sendthread::run()
{
     qDebug("sendthread running");
    std::list<char *>::iterator pos,posend,postmp;
    lconn               Con        = NULL;
    LPMSG_HEADER        pMsgHeader = NULL;
    LPCOMM_PKG_HEADER   pPkgHeader = NULL;
    char                *pMsgBuf   = NULL;
    unsigned short      _pkglen    = 0;
    while(!g_shutdown)
    {
        //qDebug("m_SendSem.acquire() ready");
        usleep(200);
        g_socket.m_SendSem.acquire();       //获取信号量

        //qDebug("m_SendSem.acquire() over");
        if(g_shutdown == true)
            return;

        if(g_socket.m_SendQueueSize > 0)
        {

                 std::lock_guard<std::mutex>InPutSendQueuLock(g_socket.m_SendDataQueueMutex);
                 pos      =  g_socket.m_SendDataQueue.begin();
                 posend   =  g_socket.m_SendDataQueue.end();
                 while(pos != posend)
                 {

                    pMsgBuf     = (*pos);
                    pMsgHeader  = (LPMSG_HEADER)pMsgBuf;                                        //消息头
                    pPkgHeader  = (LPCOMM_PKG_HEADER)(pMsgBuf + g_socket.m_MsgHeaderLen);       //包头
                    Con         = pMsgHeader->Con;
                    if(Con->_conn == nullptr)
                    {
                      qDebug("socket shuted...");
                      postmp = pos;
                      ++pos;
                      delete [](*postmp);                       //释放发包所分配的内存【其它断开处理在recv处已经处理】
                      g_socket.m_SendDataQueue.erase(postmp);   //移除失效迭代器
                      --g_socket.m_SendQueueSize;               //消息数-1
                      //errno,"即将发包,但客户端断开");
                      //....
                       continue;
                    }

                    //可以开始正常发送
                    //--Con->iWaitToSendPkgCount;                 //消息积压数量-1
                    --g_socket.m_SendQueueSize;                   //消息数减一
                    Con->psendMemoryPointer = pMsgBuf;            //记录新开辟内存首地址
                    Con->PsendBuf = (char *)pPkgHeader;           //记录发缓存首地址
                    postmp = pos;                                 //记录用于erase
                    ++pos;                                        //后移到下一个待处理项
                    g_socket.m_SendDataQueue.erase(postmp);       //移除
                    _pkglen = ntohs(pPkgHeader->pkgLen);
                    Con->nexTosendlen = _pkglen;                  //初始长度 包头+包体

                    labelrsend:                      
                    Con->SnedSlotOverlable = false;

                    //emit g_socket.CallSendSlotFun((void *)Con , Con->PsendBuf , Con->nexTosendlen);  //发包信号, 更新Con->issendlen
                    emit g_socket.CallSendSlotFun((void *)Con , Con->PsendBuf , Con->nexTosendlen);  //发包信号, 更新Con->issendlen

                    while(!Con->SnedSlotOverlable){
                        qDebug("waiting...");
                    };

                    if(Con->issendlen > 0)
                    {
                       g_socket.SendRate += Con->issendlen;             //每隔1s重置
                       if(Con->issendlen == (int)Con->nexTosendlen)
                       {
                           ++g_socket.SendPkgRate;                      //每隔1s重置
                           ++g_socket.m_pkgcount;                       //发包数+1
                           //包头+包体 全部发送成功,释放消息内存
                           delete []Con->psendMemoryPointer;
                           Con->psendMemoryPointer = NULL;
                           Con->PsendBuf           = NULL;
                           //qDebug()<<"发包完毕 issendlen = "<< Con->issendlen << endl;
                       }
                       else
                       {
                           //发送了一部分
                           Con->PsendBuf       =  Con->PsendBuf + Con->issendlen;
                           Con->nexTosendlen   =  Con->nexTosendlen  - Con->issendlen;
                           goto labelrsend;
                       }
                    }
                    else if(Con->issendlen == 0 || Con->issendlen == -1)
                    {
                        //缓存区满
                        goto labelrsend;
                    }
                    else
                    {
                       //走到这里issendlen = -2源于连接断开
                       delete []Con->psendMemoryPointer;  //释放内存
                       Con->psendMemoryPointer = NULL;
                       Con->PsendBuf           = NULL;
                       Con->nexTosendlen       = 0;
                       continue;
                    }
                  }
             }
      }
}

