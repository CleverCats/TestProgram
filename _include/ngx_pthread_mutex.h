#ifndef __PTHREAD___MUEXT_LOCK_
#define __PTHREAD___MUEXT_LOCK_
#include <pthread.h>
class CLOCK
{
public:
    CLOCK(pthread_mutex_t *_mutex)
    {
        m_guard_mutex = _mutex;
        pthread_mutex_lock(m_guard_mutex);       //加锁互斥量
    }
    ~CLOCK()
    {
        pthread_mutex_unlock(m_guard_mutex);     //解锁互斥量
    }
private:
    pthread_mutex_t *m_guard_mutex;
};

#endif