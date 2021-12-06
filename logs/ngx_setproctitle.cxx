#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include"ngx_func.h"
#include"ngx_global.h"
#pragma warning(disable:4996)
void ngx_init_setproctitle() //迁移环境变量(防止设置可执行程序标题溢出覆盖环境变量)
{
	gp_envmem = new char[g_envneedmem]; //分配环境内存
	memset(gp_envmem, 0, g_envneedmem); //内存清空

	char* ptmp = gp_envmem;
	size_t envlen = 0;					//记录每一项环境变量的长度

	//转移环境变量
	for(int i = 0; environ[i];++i)
	{
		envlen = strlen(environ[i]) + 1;  //+‘\0‘
		strcpy(ptmp, environ[i]);
		environ[i] = ptmp;				//原环境变量地址指向新地址
		ptmp += envlen;
	}

	//环境变量迁移结束,源环境变量地址内存可任意覆盖
	return;
}

//设置可执行程序标题
void ngx_setproctitle(const char *title)
{
	//我们假设，所有的命令 行参数我们都不需要用到了，可以被随意覆盖了；
	//注意：我们的标题长度，不会长到原始标题和原始环境变量都装不下，否则怕出问题，不处理

	//(1)计算新标题长度(不包含\0)
	size_t ititlelen = strlen(title);

	//(2)计算总的原始的argv那块内存的总长度【包括各种参数】    
	size_t esy = g_argvneedmem + g_envneedmem; //argv和environ内存总和
	if (esy <= ititlelen)
	{
		//argv和environ总和都存不下,直接返回不在设置[environ变量的内存紧跟在argv变量内存的后面]
		return;
	}

	//设置后续的命令行参数为空，表示只有argv[]中只有一个元素了，防止后续argv被滥用
	//因为很多判断是用argv[] == NULL来做结束标记判断的;
	//标题名即为第一个元素
	g_os_argv[1] = NULL;

	//(4)把标题弄进来，注意原来的命令行参数都会被覆盖掉，不要再使用这些命令行参数,而且g_os_argv[1]已经被设置为NULL了
	char *ptmp = g_os_argv[0]; //让ptmp指向g_os_argv所指向的内存

	// printf("new title = %s\n",title);

	strcpy(ptmp, title);
	ptmp += ititlelen; //跳过标题

	//(5)把剩余的原argv以及environ所占的内存全部清0，否则会出现在ps的cmd列可能还会残余一些没有被覆盖的内容；
	size_t cha = esy - ititlelen;  //内存总和减去标题字符串长度(不含字符串末尾的\0)，剩余的大小
	memset(ptmp,0, cha);
	return;
}