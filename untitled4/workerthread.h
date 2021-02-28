#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H
#include <QThread>
class workerthread: public QThread
{
public:
    workerthread();
protected:
    void run();
};

#endif // WORKERTHREAD_H
