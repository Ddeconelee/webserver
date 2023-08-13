#ifndef ThreadLocker_H
#define ThreadLocker_H

#include <pthread.h>
#include <semaphore.h>
#include <exception>
//线程同步机制封装类

/*---互斥锁---*/
class ThreadLocker {
public:
    //创建锁
    ThreadLocker();
    //销毁锁
    ~ThreadLocker();
    //上锁
    bool Lock();
    //解锁
    bool Unlock();
private:
    pthread_mutex_t pthread_mutex;
};


/*---条件锁---*/
class THreadCond {
public:
    THreadCond();
    ~THreadCond();
    //阻塞等待通知
    bool Wait(pthread_mutex_t * pthread_mutex_ptr);
    //指定时间等待
    bool TimeWait(pthread_mutex_t * pthread_mutex_ptr, struct timespec TimeSpec);
    //信号通知，唤醒一个或多个
    bool Signal();
    //唤醒所有
    bool Broadcast();
private:
    pthread_cond_t pthread_cond;
};

/*---信号量---*/
class ThreadSem {
public:
    ThreadSem();
    ThreadSem(int num);
    ~ThreadSem();
    bool Wait();
    bool Post();

private:
    sem_t pthread_sem;   
};
#endif