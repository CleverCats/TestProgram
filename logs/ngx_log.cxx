#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>    //va_start ...... 可变参实现相关函数头文件
#include <unistd.h>
#include <errno.h>     //errno相关

// #include<sched.h>
// #include<ctype.h>

#include <time.h>      //时间相关

#include <sys/types.h> //open相关
#include <sys/stat.h>
#include <fcntl.h>

#include "sys/time.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_func.h"
#include "ngx_c_conf.h"

//错误等级,ngx_macro.h里定义的日志等级宏是一一对应关系
static u_char err_levels[][20]=
{
	{ "stderr" },    //0：控制台错误        初始化索引号为 0 的行 
	{ "emerg" },     //1：紧急				初始化索引号为 1 的行
	{ "alert" },     //2：警戒				....
	{ "crit" },      //3：严重
	{ "error" },     //4：错误
	{ "warn" },      //5：警告
	{ "notice" },    //6：注意
	{ "info" },      //7：信息
	{ "debug" }      //8：调试
};
ngx_log_t  ngx_log; //包含日志级别与日志文件描述符的结构体类型对象

void ngx_log_init()
{
	u_char *plogname = NULL;
	size_t nlen;
	
	//读取配置文件中日志相关的配置信息
	CConfig* p_config = CConfig::GetInstance();
	//读取配置文件中日志打印路径
	plogname = (u_char*)p_config->GetString("log");
	if (plogname == NULL)
	{
		//读取失败设置默认路径
		plogname = (u_char*)NGX_ERROR_LOG_PATH; //"logs/error.log" ,logs目录需要提前建立出来
	}
	//获取日志等级筛选信息,失败返回默认等级信息 NGX_LOG_NOTICE 6【notice】
	ngx_log.log_level = p_config->GetIntDefault("LogLevel", NGX_LOG_NOTICE);
	//Linux下以读写方权限打开日志文件,获取文件描述符
	//ngx_log.fd = open((const char*)plogname, O_WRONLY | O_APPEND | O_CREAT, 0644); open为Linux下的函数,这里使用fopen
	//C标准库中的fopen返回的是一个结构体指针(文件指针),该结构体封装了文件描述符_file;
	//Linux下的open()返回文件描述符
	ngx_log.fd = open((const char *)plogname,O_WRONLY|O_APPEND|O_CREAT,0644);  
	if (ngx_log.fd == -1)  //如果有错误，则直接定位到 标准错误上去 
    {
        ngx_log_stderr(errno,"[alert] could not open error log file: open() \"%s\" failed", plogname);
        ngx_log.fd = STDERR_FILENO; //直接定位到标准错误去了        
    } 
	//Windows下以读写方式打开文件并获取文件指针
	//fopen(....)
	return;
}
void ngx_log_stderr(int err, const char* fmt, ...)
{
	va_list args; //char *
	u_char errstr[NGX_MAX_ERROR_STR + 1];	//0 - 2048
	u_char *p, *last;

	memset(errstr, 0, sizeof(errstr));
	last = errstr + NGX_MAX_ERROR_STR;		//记录尾元素地址(有效缓存区尾地址)
	p = ngx_cpymem(errstr, "nginx: ", 7);   //记录ngxinx: 字符串后的有效首地址(有效缓存区首地址)

	va_start(args, fmt);					//使args指向可变参数表...里面的第一个参数
	p = ngx_vslprintf(p, last, fmt, args); //组合出这个字符串保存在errstr里
	va_end(args);        //释放args

	if (err)  //如果错误代码不是0，表示有错误发生
	{
		//错误代码和错误信息也要显示出来
		p = ngx_log_errno(p, last, err);
	}

	//若位置不够，那换行也要硬插入到末尾，哪怕覆盖到其他内容    
	if (p >= (last - 1))
	{
		p = (last - 1) - 1; //把尾部空格留出来，这里感觉nginx处理的似乎就不对 
		//我觉得，last-1，才是最后 一个而有效的内存，而这个位置要保存\0，所以我认为再减1，这个位置，才适合保存\n
	}
	*p++ = '\n';  //增加个换行符   

	//往标准错误输出信息,一般指屏幕
	write(STDERR_FILENO,errstr,p - errstr);
	//因为上边已经把err信息显示出来了，所以这里就不要显示了，否则显示重复了
	
	if(ngx_log.fd > STDERR_FILENO) //文件描述符 > 2 表示有效文件
	{
		err = 0;    //不要再次把错误信息弄到字符串里，否则字符串里重复了
		p--; *p = 0; //把原来末尾的\n干掉，因为到ngx_log_err_core中还会加这个\n

		ngx_log_error_core(NGX_LOG_STDERR, err, (const char *)errstr);
	}
	return;
}
u_char *ngx_log_errno(u_char *buf, u_char *last, int err)
{
	//strerror 通过标准错误的标号，获得错误的描述字符串 
	char *perrorinfo = strerror(err); //根据资料不会返回NULL;
	size_t len = strlen(perrorinfo);

	//插入一些字符串： (%d:)  
	char leftstr[10] = { 0 }; //内存清'\0'
	sprintf(leftstr, " (%d: ", err);
	size_t leftlen = strlen(leftstr);

	char rightstr[] = ") ";
	size_t rightlen = strlen(rightstr);

	size_t extralen = leftlen + rightlen; //左右的额外宽度
	if ((buf + len + extralen) < last)
	{
		//保证整个我装得下，我就装，否则我全部抛弃 ,nginx的做法是 如果位置不够，就硬留出50个位置【哪怕覆盖掉以往的有效内容】，也要硬往后边塞，这样当然也可以；
		buf = ngx_cpymem(buf, leftstr, leftlen);
		buf = ngx_cpymem(buf, perrorinfo, len);
		buf = ngx_cpymem(buf, rightstr, rightlen);
	}
	return buf;
}
void ngx_log_error_core(int level, int err, const char *fmt, ...)
{
	pid_t ngx_pid;
	u_char  *last;
	u_char  errstr[NGX_MAX_ERROR_STR + 1];   //本函数可以参考ngx_log_stderr()函数的写法；

	memset(errstr, 0, sizeof(errstr));
	last = errstr + NGX_MAX_ERROR_STR;
	ngx_pid = getpid();

  	struct timeval   tv;
    struct tm        tm;
    time_t           sec;   //秒
    u_char           *p;    //指向当前要拷贝数据到其中的内存位置
    va_list          args;

	memset(&tv,0,sizeof(struct timeval));    
    memset(&tm,0,sizeof(struct tm));
	gettimeofday(&tv, NULL);     //获取当前时间，返回自1970-01-01 00:00:00到现在经历的秒数【第二个参数是时区，一般不关心】        

    sec = tv.tv_sec;             //秒
    localtime_r(&sec, &tm);      //把参数1的time_t转换为本地时间，保存到参数2中去，带_r的是线程安全的版本，尽量使用
    tm.tm_mon++;                 //月份要调整下正常
    tm.tm_year += 1900;          //年份要调整下才正常
    
    u_char strcurrtime[40]={0};  //先组合出一个当前时间字符串，格式形如：2019/01/08 19:57:11
    ngx_slprintf(strcurrtime,  
                    (u_char *)-1,                       //若用一个u_char *接一个 (u_char *)-1,则 得到的结果是 0xffffffff....，这个值足够大
                    "%4d/%02d/%02d %02d:%02d:%02d",     //格式是 年/月/日 时:分:秒
                    tm.tm_year, tm.tm_mon,
                    tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec);

	p = ngx_cpymem(errstr,strcurrtime,strlen((const char *)strcurrtime));  //日期增加进来，得到形如：     2019/01/08 20:26:07
    p = ngx_slprintf(p, last, " [%s] ", err_levels[level]);                //日志级别增加进来，得到形如：  2019/01/08 20:26:07 [crit] 
    p = ngx_slprintf(p, last, "%P: ",ngx_pid);                             //支持%P格式，进程id增加进来，得到形如：   2019/01/08 20:50:15 [crit] 2037:

	va_start(args, fmt);                     //使args指向起始的参数
	p = ngx_vslprintf(p, last, fmt, args);   //把fmt和args参数弄进去，组合出来这个字符串
	va_end(args);                            //释放args 


	if (err)  //如果错误代码不是0，表示有错误发生
	{
		//错误代码和错误信息也要显示出来
		p = ngx_log_errno(p, last, err);
	}
	//若位置不够，那换行也要硬插入到末尾，哪怕覆盖到其他内容
	if (p >= (last - 1))
	{
		p = (last - 1) - 1; //把尾部空格留出来，这里感觉nginx处理的似乎就不对 
		//我觉得，last-1，才是最后 一个而有效的内存，而这个位置要保存\0，所以我认为再减1，这个位置，才适合保存\n
	}
	*p++ = '\n'; //换行

    ssize_t   n;
    while(1) 
    {        
        if (level > ngx_log.log_level) 
        {
            //要打印的这个日志的等级太落后（等级数字太大，比配置文件中的数字大)
            //这种日志就不打印了
            break;
        }
        //磁盘是否满了的判断，先算了吧，还是由管理员保证这个事情吧； 

        //写日志文件
		char* fptr = (char*)errstr; 
		while(*fptr!='\0')
		{
			putchar(*fptr++);
		}
		//printf("sizeof = %d bit\n",(int)(p-errstr));
        n = write(ngx_log.fd,errstr,p - errstr);  //文件写入
		//注意:write是系统函数,不是c的标准IO库函数,系统函数都是原子操作,不会被终止
		//以f开头的函数例如fopen这种实际底层调用也是write函数,不过还是优先使用系统函数
		//printf("success write in: n=%d\n",(int)n);
        if (n == -1) 
        {
            //写失败有问题
            if(errno == ENOSPC) //写失败，且原因是磁盘没空间了
            {
                //磁盘没空间了
                //do nothing吧；
            }
            else
            {
                //这是有其他错误，那么我考虑把这个错误显示到标准错误设备吧；
                if(ngx_log.fd != STDERR_FILENO) //当前是定位到文件的，则条件成立
                {
                    n = write(STDERR_FILENO,errstr,p - errstr);
                }
            }
        }
        break;
    } //end while    
}

int GetThreadPolicyInfo(pthread_attr_t *attr)
{
	int policy;
	policy = sched_getscheduler(0);
    switch (policy)
    {
        case SCHED_FIFO:
		ngx_log_stderr(0,"pid:%d policy = SCHED_FIFO",getpid()); 
            break;
        case SCHED_RR:
			ngx_log_stderr(0,"pid:%d policy = SCHED_RR",getpid()); 
            break;
        case SCHED_OTHER:
			ngx_log_stderr(0,"pid:%d policy = SCHED_OTHER",getpid()); 
            break;
        default:
			ngx_log_stderr(0,"pid:%d policy = UNKNOWN\n",getpid());
            break; 
    }
	return policy;
}

void InitschedInfo(struct sched_param *sched, int priority, int policy, int act)
{
	int minporiorityvalue, maxporiorityvalue;

	//实时优先级，范围从1（最高优先级）- 99（最低优先级）的值。调度程序总是让优先级高的进程运行，禁止低优先级运行。
    minporiorityvalue = sched_get_priority_min(policy);
	maxporiorityvalue = sched_get_priority_max(policy);
	sched->sched_priority = minporiorityvalue;

	if(act != -1)
	{
		if(act == 0)
		{
			sched->sched_priority = maxporiorityvalue;  //设置最小优先级
		}
		else
		{
			sched->sched_priority = minporiorityvalue;  //设置最大优先级
		}
	}
	else if(act == -1)
	{
		if(priority >= minporiorityvalue && priority <= maxporiorityvalue)
		sched->sched_priority = priority;
	}

    ngx_log_stderr(0,"pid:%d policty = %d minvalue = %d, maxvalue = %d",getpid(),sched->sched_priority,minporiorityvalue,maxporiorityvalue);

    return;
}


int SetCpuAffinity(int cpuindex, pid_t pid)
{
	/*绑定CPU*/
	int res = 1;
	cpu_set_t mask;  			//CPU掩码集合[多少个CPU就有多少位掩码]
	cpu_set_t get;   			//获取在集合中的CPU

	while(true)
	{
	
		CPU_ZERO(&mask);			//清空CPU掩码
		CPU_SET(cpuindex, &mask);	//设置CPU掩码[执行sched_getaffinity时绑定mask指定的CPU]
		if(sched_setaffinity(pid, sizeof(mask), &mask) == -1)
		{
			res = -1;
			ngx_log_stderr(errno,"pid:%d SetCpuaffinityAndSetschedpolicy中sched_setaffinity() 失败",getpid());
			break;
		}

		CPU_ZERO(&get);
		if (sched_getaffinity(0, sizeof(get), &get) == -1)		//获取线程CPU亲和力[绑定CPU]
		{
			res = -1;
			ngx_log_stderr(errno,"pid:%d SetCpuaffinityAndSetschedpolicy中sched_getaffinity() 失败",getpid());
			break;
		}

		for(int i = 0; i < g_cpunum; ++i)
		{
			if (CPU_ISSET(i, &get))							   //判断线程与哪个CPU有亲和力[判断绑定了哪个CPU]
			{
				ngx_log_stderr(errno,"pid:%d 成功绑定CPU:%d",getpid(),i);   
				break;
			}
		}
		break;
	}
	return res;
}

int SetThreadPolicyAndPoritory(int policy, int poritory, pid_t pid, int act)
{
	int res = 1;
	while(true)
	{
		/*设置进程优先级*/
		pthread_attr_t attr;       // 线程属性
		struct sched_param sched;  // 调度策略
		int rs;
		/* 对线程属性初始化
		* 初始化完成以后，pthread_attr_t 结构所包含的结构体
		* 就是操作系统实现支持的所有线程属性的默认值*/
		if(pthread_attr_init(&attr) != 0)
		{
			res = -1;
			ngx_log_stderr(errno,"pid = %d SetCpuaffinityAndSetschedpolicy中pthread_attr_init() 失败",getpid());
			break;
		}

		/*获取当前调度策略*/
		/* 1.SCHED_OTHER 分时调度策略
		   2.SCHED_FIFO  实时调度策略，先到先服务。一旦占用cpu则一直运行。一直运行直到有更高优先级任务到达或自己放弃
		   3.SCHED_RR	 实时调度策略，时间片轮转。当进程的时间片用完，系统将重新分配时间片，并置于就绪队列尾。放在队列尾保证了所有具有相同优先级的RR任务的调度公平*/

		//ShowThreadPriorityInfo(&attr, policy);			//打印进程优先级
		//SetThreadPolicy(&attr, SCHED_FIFO);				//设置OS事实调度策略[时间片轮转]

		InitschedInfo(&sched, poritory, policy, act);		//初始化sched数据

		/* sched_setscheduler(pid_t pid, int policy, const struct sched_param *param); 
		参数: pid ,调度策略, 调度优先级*/

		if(sched_setscheduler(pid, policy, &sched) != 0)
		{
			res = -1;
			ngx_log_stderr(errno,"pid = %d SetCpuaffinityAndSetschedpolicy中pthread_attr_setschedparam() 失败",getpid());
			break;
		}

		//GetThreadPolicyInfo(&attr);

		if(pthread_attr_destroy(&attr) != 0)
		{
			res = -1;
			ngx_log_stderr(errno,"pid = %d SetCpuaffinityAndSetschedpolicy中pthread_attr_destroy() 失败",getpid());
			break;
		}		
		break;
	}
	return res;
}

int CpuOptimize()
{
	int res = 1;
    int ipol = SCHED_OTHER;
	CConfig *p_config = CConfig::GetInstance();

  	const char *spol = p_config->GetString("ThreadPolicy");
    	if(strcmp(spol, "SCHED_OTHER") == 0)		//SCHED_OTHER是分时调度策略不可进行优先级修改
			return res;
        if(strcmp(spol, "SCHED_FIFO") == 0)
            ipol = SCHED_FIFO;
        if(strcmp(spol, "SCHED_RR") == 0)
            ipol = SCHED_RR;

        const char *por = p_config->GetString("ThreadPriority");
        if(strcmp(por, "MAX") == 0)
        {
            res = SetThreadPolicyAndPoritory(ipol, 0, 0, THREAD_PRIORITYMAX);
        }
        else if(strcmp(por,"MIN") == 0)
        {
            res = SetThreadPolicyAndPoritory(ipol, 0, 0, THREAD_PRIORITYMIN);
        }
        else
        {
            res = SetThreadPolicyAndPoritory(ipol, atoi(por));
        }            

	return res;
}
   
// void SetThreadPolicy(pthread_attr_t *attr,int policy)
// {
// 	if(pthread_attr_setschedpolicy(attr, policy) != 0)
// 	{
// 		ngx_log_stderr(errno,"pid:%d SetThreadPolicy中pthread_attr_setschedpolicy()失败",getpid());
// 	}
// }