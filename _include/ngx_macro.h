#ifndef __NGX_MACRO_H__
#define __NGX_MACRO_H__

//日志相关------------------------------
//日志一共分成八个等级【级别从高到低，数字最小的级别最高，数字大的级别最低】，以方便管理、显示、过滤等等
#define NGX_LOG_STDERR            0    //控制台错误【stderr】：最高级别日志，内容写入log参数指定的文件，同时也尝试直接将日志输出到标准错误设备比如控制台屏幕
#define NGX_LOG_EMERG             1    //紧急 【emerg】
#define NGX_LOG_ALERT             2    //警戒 【alert】
#define NGX_LOG_CRIT              3    //严重 【crit】
#define NGX_LOG_ERR               4    //错误 【error】：属于常用级别
#define NGX_LOG_WARN              5    //警告 【warn】：属于常用级别
#define NGX_LOG_NOTICE            6    //注意 【notice】
#define NGX_LOG_INFO              7    //信息 【info】
#define NGX_LOG_DEBUG             8    //调试 【debug】：最低级别
#define NGX_ERROR_LOG_PATH       "error.log"   //定义日志存放的路径和文件名 

//日志显示的错误信息最大数组长度
#define NGX_MAX_ERROR_STR  2048

//日志信息打印字符串相关
#define ngx_cpymem(buf,str,n) (((u_char*)memcpy(buf,str,n))+(n))

//网络安全-远程溢出攻击 添加数组边界'\0'
#define AddTerminatingSymbol(arr,arrsize) ((*((char*)arr + arrsize - 1)) = ('\0'))

//数字相关--------------------
#define NGX_MAX_UINT32_VALUE   (uint32_t) 0xffffffff              //最大的32位无符号数：十进制是‭4294967295‬
#define NGX_INT64_LEN          (sizeof("-9223372036854775808") - 1)     

/*0:标准输入 默认来源【键盘】,对应符号常量叫 SIGIN_FILENO       
1:标准输出 默认来源【屏幕】,对应符号常量叫 SIGOUT_FILENO
2:标准错误 默认来源【屏幕】,对应符号常量叫 SIGERR_FILENO*/
#define STDIN_FILENO	0
#define STDOUT_FILENO	1
#define STDERR_FILENO	2

//标准输入 标准输出 标准错误
#define SIDIN_FILENO 0
#define SIDOUT_FILENO 1
#define SIDERR_FILENO 2

//进程类型
#define NGX_MASTER_PROCESS 0    //master process
#define NGX_WORKER_PROCESS 1    //worker process

#define THREAD_PRIORITYMIN 0    //thread max priority
#define THREAD_PRIORITYMAX 1    //thread min prority

//Socket相关
#define NGX_LISTEN_BLOCKLOG 32  //已完成连接队列最大项数

//epoll相关
#define NGX_MAX_EVENTS 512      //epoll返回事件集合长度

//收发包消息代码相关
#define _CMD_PING      0        //心跳包
#define _CMD_Register  5        //用户注册
#define _CMD_LOGIN     6        //用户登陆

//宏定义放置
#endif