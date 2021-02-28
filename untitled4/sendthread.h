#ifndef SENDTHREAD_H
#define SENDTHREAD_H

#include <QThread>
class sendthread: public QThread
{
public:
    sendthread();
protected:
    void run();
};

#endif // SENDTHREAD_H
