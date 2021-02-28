#ifndef RECVTHREAD_H
#define RECVTHREAD_H

#include <QThread>
class recvthread: public QThread
{
public:
    recvthread(){};
protected:
    void run();
};

#endif // RECVTHREAD_H
