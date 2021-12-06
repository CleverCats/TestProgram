
#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__
#include "ngx_global.h"  //一些全局/通用定义
#include <vector>

//类名可以遵照一定的命名规则规范，第一个字母是C，后续的单词首字母大写
class CConfig
{
private:
	CConfig();
public:
	~CConfig();
private:
	static CConfig *m_instance;

public:
	static CConfig* GetInstance()
	{
		if (m_instance == NULL)
		{
			//锁
			if (m_instance == NULL)
			{
				m_instance = new CConfig();
				static CGarhuishou cl;
			}
			//放锁	
		}
		return m_instance;
	}
	class CGarhuishou  //类中套类，用于释放对象
	{
	public:
		~CGarhuishou()
		{
			if (CConfig::m_instance!=NULL)
			{
				delete CConfig::m_instance;
				CConfig::m_instance = NULL;
			}
		}
	};
//---------------------------------------------------------
public:
	bool Load(const char *pconfName);						   //装载配置文件
	const char *GetString(const char *p_itemname);			   //获取配置文件字符串信息
	int  GetIntDefault(const char *p_itemname, const int def); //获取配置文件数字字符信息

public:
	std::vector<LPCConfItem> m_ConfigItemList; //存储配置信息的列表

};

#endif