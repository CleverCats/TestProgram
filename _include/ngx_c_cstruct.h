#ifndef __NGX_C_CLASS_H___
#define __NGX_C_CLASS_H___
#include "ngx_c_socket.h"
#pragma pack(1)                         //1字节对齐

//网络通讯相关

/*包头结构*/
typedef struct _COMM_PAK_HEAD_{
    unsigned short pkgLen;     //报文长度【包头+包体】
    unsigned short msgCode;    //消息型代码【区别命令类型】
    int            crc32;      //校检是否为过期包

}COMM_PKG_HEADER , *LPCOMM_PKG_HEADER;


// /*[消息头]【用于记录数据包接入后的额外信息】*/
// typedef struct _MSG_HEADER{
//     ngx_connection_ptr pConn;         //记录对应连接
//     uint64_t           iCurrsequence; //收到数据包后记录序号,处理完信号后与源序号对比,判断是否是过期包
// }MSG_HEADER , *LPMSG_HEADER;

/*登录*/
typedef struct _Register{
    char           Registercid[50];      //登录账户
    char           Registerccode[50];    //登录密码
}C_Register,*LP_CRegister;

/*注册*/
typedef struct _Login{
    char            logincid[50];         //注册账户
    char            loginccode[50];       //注册密码
}C_Login,*LP_CLogin;

/*收到标识包*/
typedef struct ISRECV{
    char            recvlable[50];        //收到标识
}C_RECVPKG,*LP_RECVPKG;

#pragma pack()  
#endif