#include <mutex>
#include "c_global.h"
#include "pingthread.h"

void pingthread::run()
{
    qDebug("pingthread running");
    std::multimap<time_t , lconn>::iterator pos,posend,postmp;
    time_t curtime;
    while(!g_shutdown)
    {
         if(g_socket.m_timercount > 0)
         {
            curtime = time(NULL);
            lconn OverCon;
            g_socket.UpdateErrlistTime();
            if(curtime > g_socket.errlisttime)                  //存在超时连接
            {
                //qDebug("have overtime");
                std::list<lconn> PingReadyList;
                {
                    std::lock_guard<std::mutex>RefreshLock(g_socket.m_RefreshTimeMutex);

                    while((OverCon = g_socket.GetOneReadyPing(curtime)) != nullptr)
                    {

                        //qDebug("get overtime");
                        PingReadyList.push_back(OverCon);
                    }
                }

                while(!PingReadyList.empty())
                {
                    //qDebug("Ready1");
                    lconn Con = PingReadyList.front();
                    PingReadyList.pop_front();
                    g_socket.SendPingPkg(Con);
                }

            }
         }
    }
}
