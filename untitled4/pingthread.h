#ifndef PINGTHREAD_H
#define PINGTHREAD_H
#include <QThread>
class pingthread: public QThread
{
public:
    pingthread(){};
protected:
    void run();
};

#endif // PINGTHREAD_H
