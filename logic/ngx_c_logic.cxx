#include <unistd.h> 
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "ngx_c_crc32.h"  
#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_memory.h"
#include "ngx_c_logic.h"

/*
　在Linux系统下：htonl(),htons(), ntohl(), ntohs()的头文件及函数定义：
　　#include <arpa/inet.h>
　　uint32_t htonl(uint32_t hostlong);
　　uint16_t htons(uint16_t hostshort);
　　uint32_t ntohl(uint32_t netlong);
　　uint16_t ntohs(uint16_t netshort);
/*
   在windows系统下：htonl(),htons(), ntohl(), ntohs(), inet_addr()使用说明
    ntohs()
　　简述：
　　将一个无符号短整形数从网络字节顺序转换为主机字节顺序。
    ntohl()
　　简述：
　　将一个无符号长整形数从网络字节顺序转换为主机字节顺序。
    htons()
　　简述：
　　将主机的无符号短整形数转换成网络字节顺序。//将无符号短整型主机字节序转换为网络字节序
    htonl()
　　简述
　　将主机的无符号长整形数转换成网络字节顺序。//将无符号长整型网络字节序转换为主机字节序
*/




/*成员函数指针*/
typedef bool (CLogicSocket:: *_HandleSLogicFun)(ngx_connection_ptr _pConn ,   //给客户端分配的连接池节点
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

//处理业务逻辑函数
    &CLogicSocket::_HandleRegister,       //【5】注册
    &CLogicSocket::_HandleLogin,          //【6】登陆
    &CLogicSocket::_OtherMsg              //【7】其它消息
};
//获取SLogicFun函数总数
#define NUMBER_OF_SLOGICFUN (sizeof(SLogicFun)/sizeof(_HandleSLogicFun))

CLogicSocket::CLogicSocket()
{
   
}

CLogicSocket::~CLogicSocket()
{
    
}
bool CLogicSocket::Initialize()
{
    bool rec = CSocket::Initialize();
    return rec;
}


void CLogicSocket::procPingTimeOutChecking(LPMSG_HEADER pMsgHeader , time_t cur_time)
{
    //带扩展...处理延时心跳包用户
     CMemory *_pMemory  = CMemory::GetInstance();
    
  
     //判断客户端是否断开
    if(pMsgHeader->iCurrsequence == pMsgHeader->pConn->iCurrsequence)
    {
         ngx_connection_ptr p_Conn = pMsgHeader->pConn;
       
         if(m_kickOverUserOnTime == 1 && cur_time - p_Conn->lastPingtime > m_MaxWaitePingtime)   //直接踢出
         {
            ngx_log_stderr(0,"心跳超时直接踢出");
            ngx_close_connection(p_Conn);
         }
         else if((cur_time - p_Conn->lastPingtime) > m_MaxOverTime)                             //延迟踢出
         {   
             //从定时器剔除,释放内存,并加入延迟回收池
             ngx_log_stderr(0,"心跳间隔超过最大等待时常踢出");
             ngx_close_connection(p_Conn);
         }
    }

     //走到这里无论连接是否断开,释放分配的消息头内存
    _pMemory->FreeMemory(pMsgHeader);     //释放内存
     
    return;
}


void CLogicSocket::ThreadRecvProcFunc(char *pMsgBuffer)
{
    //获取Crc32类
    CCRC32 *Get_CRC32              = CCRC32::GetInstance();
    //获取消息头
    LPMSG_HEADER  pMsgHeader       = LPMSG_HEADER(pMsgBuffer);
    //获取包头
    LPCOMM_PKG_HEADER pPkgHeader   = (LPCOMM_PKG_HEADER)(pMsgBuffer + m_MsgHeaderLen);
    //获取包长【网络序转本机序列 ntohs专门转2字节类型】
    unsigned short pPkglen         = ntohs(pPkgHeader->pkgLen);

    //获取包体
    void *pPkgBody  = NULL;
    
    //无包体【仅包头,空包】
    if(m_PkgHeaderLen == pPkglen)
    {
        //只有包头的crc32校检码是0
        if(pPkgHeader->crc32 != 0)
        {
            //就算服务器报文结构被破解,crc32校检码也会丢弃掉crc32不匹配的数据包
            return; //crc错误,直接丢弃
        }
        //成功收到心跳包 
        //ngx_log_stderr(0,"成功收到心跳包 pPkglen = %d",pPkglen);
    }
    //有包体
    else
    {
        //有包体【网络序转本机序列 ntohl专门转4字节类型】
        pPkgHeader->crc32 = ntohl(pPkgHeader->crc32);
        //获取包体
        pPkgBody          = (void*)(pMsgBuffer + m_MsgHeaderLen + m_PkgHeaderLen);
        //crc32校检包的完整
        int  sCrc32       = Get_CRC32->Get_CRC((unsigned char *)pPkgBody,pPkglen - m_PkgHeaderLen);


        if(sCrc32 != pPkgHeader->crc32)
        {
            //商用可以去掉这行打印【一直打印也麻烦】
            ngx_log_stderr(0,"pkgbodylen:%d", pPkglen - m_PkgHeaderLen);
            ngx_log_stderr(0,"CLogicSocket::ThreadRecvProcFunc(char *pMsgBuffer),CRC错误 %ud != %ud,废包",sCrc32,pPkgHeader->crc32);
            return; //crc错,直接丢弃
        }

    }
    
    //crc校检OK,走到这里
    unsigned short imsgCode     = ntohs(pPkgHeader->msgCode);
    //获取消息头中记录的连接池节点指针
    ngx_connection_ptr p_Conn   = pMsgHeader->pConn;
 
    //校检iCurrsequence,检测客户端是否已经断开【丢弃过期包】
    if(p_Conn->iCurrsequence != pMsgHeader->iCurrsequence)
    {
        //ngx_log_stderr(0,"CLogicSocket::ThreadRecvProcFunc(char *pMsgBuffer),客户端已断开");
        return;
    }
     
    
    /*走到这里表示包没过期,检测是否有对应的消息处理函数*/
    if(imsgCode > NUMBER_OF_SLOGICFUN - 1)
    {
         ngx_log_stderr(0,"CLogicSocket::ThreadRecvProcFunc(char *pMsgBuffer),imsgCode = %ud 消息代码不合理,恶意行为,废包",imsgCode);
         return;
    }

    //消息代码合理,存在对应处理函数
    if(SLogicFun[imsgCode] == NULL)
    {
         ngx_log_stderr(0,"CLogicSocket::ThreadRecvProcFunc(char *pMsgBuffer),SLogicFun[imsgCode] == NULL,无处理函数");
         return;
    }

    //一切正常,可以开始处理了【调用业务逻辑处理函数】
    (this->*SLogicFun[imsgCode])(p_Conn,                    //连接池节点指针
                                 pMsgHeader,                //消息头
                                (char*)pPkgBody,            //包体
                                pPkglen - m_PkgHeaderLen    //包体长度
    );

}
