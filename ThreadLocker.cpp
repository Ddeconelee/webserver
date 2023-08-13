#include "ThreadLocker.h"

/*---互斥锁---*/
//创建锁
ThreadLocker::ThreadLocker() {
    if (pthread_mutex_init(&pthread_mutex, NULL) != 0)  {
        throw std::exception();//抛出异常 
    }
}

//销毁锁
ThreadLocker::~ThreadLocker() {
    pthread_mutex_destroy(&pthread_mutex);
}

//上锁
bool ThreadLocker::Lock() {
    return pthread_mutex_lock(&pthread_mutex) == 0;
}

//解锁
bool ThreadLocker::Unlock() {
    return pthread_mutex_unlock(&pthread_mutex) == 0;
}


/*---条件锁---*/
//创建锁
THreadCond::THreadCond() {    
    if (pthread_cond_init(&pthread_cond, NULL) != 0) {
        throw std::exception();
    }
}

//销毁锁
THreadCond::~THreadCond() {
    pthread_cond_destroy(&pthread_cond);
} 

bool THreadCond::Wait(pthread_mutex_t * pthread_mutex_ptr) {
    return pthread_cond_wait(&pthread_cond, pthread_mutex_ptr) == 0;
}

bool THreadCond::TimeWait(pthread_mutex_t * pthread_mutex_ptr, struct timespec TimeSpec) {
    return pthread_cond_timedwait(&pthread_cond, pthread_mutex_ptr, &TimeSpec);
}

bool THreadCond::Signal() {
    return pthread_cond_signal(&pthread_cond) == 0;
}

bool THreadCond::Broadcast() {
    return pthread_cond_broadcast(&pthread_cond) == 0;
}


/*---信号量---*/
ThreadSem::ThreadSem() {
    if (sem_init(&pthread_sem, 0, 0) != 0) {
        throw std::exception();
    }
}
//构造函数重载
ThreadSem::ThreadSem(int num) {
    if (sem_init(&pthread_sem, 0, num) != 0) {
        throw std::exception();
    }
}

ThreadSem::~ThreadSem() {
    sem_destroy(&pthread_sem);
}

bool ThreadSem::Wait() {
    return sem_wait(&pthread_sem) == 0;
}

bool ThreadSem::Post() {
    return sem_post(&pthread_sem) == 0;
}