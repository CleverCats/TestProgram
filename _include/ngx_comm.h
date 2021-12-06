#ifndef __NGX_PKG_HEAD_
#define __NGX_PKG_HEAD_

#define _PKG_MAX_LENGTH 30000           //每个包的最大长度【包头 + 包体】
#define _DATA_BUFFER_SIZE 20            //包头大小【一般 >= 实际包头大小】


//通信收报状态定义
#define _PKG_HEAD_INIT      0           //初始化状态,准备开始接受包头
#define _PKG_HEAD_RECVING   1           //接收包头中【缺包导致】
#define _PKG_BODY_INIT      2           //包头接受完毕,准备接受包体
#define _PKG_BODY_RECVING   3           //包体接收中【包体接受完成直接初始化为_PKG_HEAD_INIT状态】

#endif