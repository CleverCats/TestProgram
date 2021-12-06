#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ngx_c_crc32.h"  
#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_pthread_mutex.h"
#include "ngx_c_memory.h"
#include "ngx_macro.h"
#include "ngx_c_logic.h"

bool CLogicSocket::_OtherMsg(ngx_connection_ptr _pConn ,     //给客户端分配的连接池节点
                        LPMSG_HEADER _pMsgHeader   ,           //消息头
                        char *pPkgBody             ,           //包体
                        unsigned short pPkgBodyLenth)          //包体长
{
     //(1)判断包体的合法性【登录包体为空,return False】
    if(pPkgBody == NULL)
    {
        return false;
    }

    //(3)对于同一个客户可能同时发过来多个请求,造成多个数据为该客户端服务,可能发生100金币同时买两个100金币以下物品的现象,所以此类业务必须互斥
    CLOCK LoginLock(&_pConn->logiclock);                                   //线程逻辑互斥

    //获取包体
    char *PkgBody = (char *)pPkgBody;
    //(4)这里可以根据包体进一步判断收到的数据的合法性...
    //...

    //畸形数据包防范【无结束符'/0'】
    //AddTerminatingSymbol(PkgBody, pPkgBodyLenth);

    //ngx_log_stderr(0,"pid:%d 检测到信息:%s",getpid(),PkgBody);
    return true;
}

bool CLogicSocket::_HandlePing(ngx_connection_ptr _pConn ,     //给客户端分配的连接池节点
                        LPMSG_HEADER _pMsgHeader   ,           //消息头
                        char *pPkgBody             ,           //包体
                        unsigned short pPkgBodyLenth)          //包体长
{
    if(pPkgBodyLenth != 0)                                     //心跳包,包体长为0【空包】
        return false;

     CLOCK PingLock(&_pConn->logiclock);                       //线程逻辑互斥
    _pConn->lastPingtime = time(NULL);                         //刷新ping last_time
    SendNoBodyPkgtoClient(_pMsgHeader , _CMD_PING);            //发送心跳包

    //ngx_log_stderr(0,"成功发送了心跳包");
    return true;
}


/*登录*/          
bool CLogicSocket::_HandleLogin(ngx_connection_ptr _pConn ,     //给客户端分配的连接池节点
                        LPMSG_HEADER _pMsgHeader   ,            //消息头
                        char *pPkgBody             ,            //包体
                        unsigned short pPkgBodyLenth)           //包体长
{
    //(1)判断包体的合法性【登录包体为空,return False】
    if(pPkgBody == NULL)
    {
        return false;
    }

    //(2)判断登录包体大小是否正确【排除恶意包】
    int iRecvLen = sizeof(C_Login);
    if(iRecvLen != pPkgBodyLenth)
    {
        return false;
    }

    //(3)对于同一个客户可能同时发过来多个请求,造成多个数据为该客户端服务,可能发生100金币同时买两个100金币以下物品的现象,所以此类业务必须互斥
    CLOCK LoginLock(&_pConn->logiclock);                                   //线程逻辑互斥

    //获取包体
    LP_CLogin pLogin = (LP_CLogin)pPkgBody;
    //(4)这里可以根据包体进一步判断收到的数据的合法性...
    //...

    //畸形数据包防范【无结束符'/0'】
    AddTerminatingSymbol(pLogin->logincid, sizeof(pLogin->logincid));
    AddTerminatingSymbol(pLogin->loginccode, sizeof(pLogin->loginccode));

    //(5)开始处理业务
    char    *ccid    =  (char*)(pLogin->logincid);
    char    *ccode   =  (char*)(pLogin->loginccode);
    //ngx_log_stderr(0,"pid:%d 检测到登录信息 loginid = %s ,logincode = %s",getpid(),ccid,ccode);

    //(6)处理完成,给客户端返回数据成功标识【具体发什么由客户端服务器协商,一般也是一个结构】
    CMemory *p_memory   = CMemory::GetInstance(); 
    CCRC32  *p_crc32    = CCRC32::GetInstance();

    //这里包体就返回答应标识包
    int p_sendbodylen   = sizeof(C_RECVPKG);
    //分配待发包内存
    char *p_sendbuf     = (char *)p_memory->AllocMemory(m_MsgHeaderLen + m_PkgHeaderLen + p_sendbodylen,true);
    //填充消息头
    memcpy(p_sendbuf, _pMsgHeader, m_MsgHeaderLen);
    //填充包头
    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader          = (LPCOMM_PKG_HEADER)(p_sendbuf + m_MsgHeaderLen);
    pPkgHeader->msgCode = _CMD_LOGIN;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_PkgHeaderLen + p_sendbodylen);
    //填充包体
    LP_RECVPKG p_sendbody = (LP_RECVPKG)(pPkgHeader + m_PkgHeaderLen);

    //这里根据需要填充要返回给客户端的包体内容【int用htonl()转,short用htons()转】
    memcpy(p_sendbody->recvlable , "success to Login",17);
    
    //包头填充好后,计算包头的crc32值
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendbody,p_sendbodylen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);

    delete []p_sendbuf;
    //入发包消息队列
    //ngx_put_sendMsgQueue(p_sendbuf);
    
    return true;

}



/*注册*/
bool CLogicSocket::_HandleRegister(ngx_connection_ptr _pConn ,  //给客户端分配的连接池节点
                         LPMSG_HEADER _pMsgHeader  ,            //消息头
                         char *pPkgBody            ,            //包体
                         unsigned short pPkgBodyLenth)          //包体长
{
    //(1)判断包体的合法性【具体看客户服务器约定,注册包是否必须带包体】
    if(pPkgBody == NULL)
    {
        ngx_log_stderr(0,"pid:%d 检测到注册信息 ERROR：pPkgBody == NULL",getpid());
        return false;
    }

    //(2)判断注册包体大小是否正确【排除恶意包】
    int iRecvLen = sizeof(C_Register);
    if(iRecvLen != pPkgBodyLenth)
    {
        ngx_log_stderr(0,"pid:%d 检测到注册信息 ERROR：iRecvLen != pPkgBodyLenth",getpid());
        return false;
    }

    // //(3)对于同一个客户可能同时发过来多个请求,造成多个数据为该客户端服务,可能发生100金币同时买两个100金币以下物品的现象,所以此类业务必须互斥
    CLOCK RegisterLock(&(_pConn->logiclock));                                   //线程逻辑互斥

    //(4)获取包体
    LP_CRegister pRegister = (LP_CRegister)pPkgBody;
    
    //(4)这里可以根据包体进一步判断收到的数据的合法性...
    //...

    //畸形数据包防范【无结束符'/0'】
    AddTerminatingSymbol(pRegister->Registercid, sizeof(pRegister->Registercid));
    AddTerminatingSymbol(pRegister->Registerccode, sizeof(pRegister->Registerccode));
    
    //(5)开始处理业务
    char    *ccid   = (char*)pRegister->Registercid;
    char    *ccode  =  (char*)pRegister->Registerccode;
    ngx_log_stderr(0,"pid:%d 检测到注册信息 Registerid = %s ,Registercode = %s",getpid(),ccid,ccode);


    //(6)处理完成,给客户端返回数据成功标识【具体发什么由客户端服务器协商,一般也是一个结构】
    CMemory *p_memory   = CMemory::GetInstance(); 
    CCRC32  *p_crc32    = CCRC32::GetInstance();

    int p_sendbodylen   = sizeof(C_RECVPKG);
    //分配待发包内存
    char *p_sendbuf     = (char *)p_memory->AllocMemory(m_MsgHeaderLen + m_PkgHeaderLen + p_sendbodylen,true);
    //ngx_log_stderr(0,"p_sendbuf = %d ,m_MsgHeaderLen = %d , m_PkgHeaderLen =  %d , p_sendbodylen = %d ",p_sendbuf, m_MsgHeaderLen,m_PkgHeaderLen,p_sendbodylen);
    //填充消息头
    memcpy(p_sendbuf, _pMsgHeader, m_MsgHeaderLen);
    //填充包头
    LPCOMM_PKG_HEADER pPkgHeader;
    pPkgHeader          = (LPCOMM_PKG_HEADER)(p_sendbuf + m_MsgHeaderLen);
    //ngx_log_stderr(0,"pPkgHeader = %d ,p_sendbuf + m_MsgHeaderLen :%d + %d" ,pPkgHeader,  p_sendbuf , m_MsgHeaderLen);
    pPkgHeader->msgCode = _CMD_Register;
    pPkgHeader->msgCode = htons(pPkgHeader->msgCode);
    pPkgHeader->pkgLen  = htons(m_PkgHeaderLen + p_sendbodylen);

    //填充包体
    LP_RECVPKG p_sendbody = (LP_RECVPKG)((char *)pPkgHeader + m_PkgHeaderLen);
    //ngx_log_stderr(0,"p_sendbody =%d, pPkgHeader + m_PkgHeaderLen :%d + %d" ,p_sendbody, pPkgHeader , m_PkgHeaderLen);
    
    //这里根据需要填充要返回给客户端的包体内容【int用htonl()转,short用htons()转】
    //....
    const char *psend = "It is ok";
    memcpy(p_sendbody->recvlable, psend, strlen(psend) + 1);

    //包体填充好后,计算包的crc32值
    pPkgHeader->crc32   = p_crc32->Get_CRC((unsigned char *)p_sendbody,p_sendbodylen);
    pPkgHeader->crc32   = htonl(pPkgHeader->crc32);

    //LP_RECVPKG p_sendbody2 = (LP_RECVPKG)(p_sendbuf + m_MsgHeaderLen + m_PkgHeaderLen);
    //ngx_log_stderr(0,"p_sendbody2 = %d ,p_sendbuf +  m_MsgHeaderLen + m_PkgHeaderLen: %d + %d + %d",p_sendbody2,p_sendbuf,m_MsgHeaderLen,m_PkgHeaderLen);

    //ngx_log_stderr(0,"入send队列");
    ngx_put_sendMsgQueue(p_sendbuf);
  
    //Socket添加可写事件【EPOLLOUT】【针对socket发送缓冲区的状态】
    
    /*EPOLLOUT:socket可写事件
    可写事件:每个socket有有一个接受缓冲区和发送缓冲区,当socket缓冲区未被占满时,代表允许使用send/write函数把数据放入发送缓冲区【可写状态】
    ps:此后内核会尝试将发送缓冲区的数据往客户端发送,所有当send/write成功返回并不代表客户端已经接受到数据了

    也即当EPOLL注册了EPOLLOUT事件,且socket的发送缓冲区未被占满时,那么在epoll_wait()时该socket的可写事件时会不断通知(返回可写事件通知)
    ps:发送缓冲区大概十几K,接受缓冲区大概几十K,可以通过setsocketopt()设置.

    基于socket之间的通讯协议,当发送缓冲区的数据发送出去,只有客户端接受缓冲区成功接受到了数据recv/read,客户端会通知服务器数据到达,
    服务器才会将socket中的发送缓冲区成功发送的数据段移除,给发送缓冲区腾出位置
    ps:如果客户端不接受或者接受数据过慢,导致服务器socket的发送缓冲区堆积达到上限,此时服务器再次调用send/write放在数据到发送缓存区会返还EAGAIN错误
      EAGAIN:其实不是一个错误,只是示意socekt发送缓冲区已满,等待socket的发送缓存区有空闲时会继续发送【TCP协议会尽力保证数据成功到达】

    LT模式下,如果使用了ctl注册了EPOLLOUT事件(socket可写事件),那么在epoll_wait时会不断返回该socket的可写事件*/
    // if(ngx_epoll_Regist_event(
    //                           _pConn->fd,       //Socketfd
    //                           EPOLL_CTL_MOD,    //MOD修改事件【添加发数据事件】
    //                           EPOLLOUT,         //EPOLLOUT:可写
    //                           0,
    //                           _pConn) == -1)
    // {
    //     ngx_log_stderr(errno,"CLogicSocket::_HandleLogin中ngx_epoll_Regist_event(EPOLL_CTL_MOD,EPOLLOUT)失败");
    //     ngx_delay_close_connection(_pConn);      //可写事件注册失败,连接延迟回收
    // }

    //解决EPOLL水平触发模式下可写事件不断触发通知的问题:
    /*方案一:
    写的时候在注册添加EPOLLOUT事件标记,写完后立即清除EPOLLOUT事件标记
    缺点:即使发送很少的数据也要进行ctl修改红黑树节点,多少影响效率.*/
    /*方案二(对方案一的优化)：
    epoll水平触发发送数据的改进方案：
	开始不把socket写事件通知加入到epoll,当我需要写数据的时候，直接调用write/send发送数据；
	如果返回了EAGIN【发送缓冲区满了，需要等待可写事件才能继续往缓冲区里写数据】，此时，我再把写事件通知加入到epoll，
	此时，就变成了在epoll驱动下写数据，全部数据发送完毕后，再把写事件通知从epoll中干掉；
	优点：数据不多的时候，可以避免epoll的写事件的增加/删除，提高了程序的执行效率；*/
    return true;

    //  //(5)开始处理业务
    // LP_CRegister pRegister = (LP_CRegister)pPkgBody;
    // char    *ccid   = (char*)pRegister->Registercid;
    // char    *ccode  =  (char*)pRegister->Registerccode;
    // ngx_log_stderr(0,"pid:%d 检测到登陆信息 Registerid = %s ,Registercode = %s",getpid(),ccid,ccode);

}

