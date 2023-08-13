#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>
#include <iostream>
#include "ThreadLocker.h"

//线程池类，类模板，方便代码复用
template<typename T>
class ThreadPool {
public:
    ThreadPool(int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    bool WorkAppend(T* request);

private:
    int thread_num;//线程数量
    pthread_t * threads;//线程池数组的指针
    int max_reqs;//最多任务请求数量
    std::list<T*> request_list;//请求任务队列
    ThreadLocker request_list_locker;//访问队列需要的互斥锁
    ThreadSem request_list_sem;//请求队列是否有需要处理的任务
    bool thread_stop;//是否结束线程

    static void * Worker(void * arg);
    void ThreadPoolRun();
};


template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests) : 
    thread_num(thread_number), max_reqs(max_requests), thread_stop(false),
    threads(NULL) {
    //判断初始化合理
    if (thread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    //创建线程池数组
    threads = new pthread_t[thread_num]; 
    if (!threads) {
        throw std::exception();
    }

    //创建thread_num个线程，并设置线程脱离
    for (int i = 0; i < thread_num; ++i) {
        std::cout << "正在创建第" << i << "个线程..." << std::endl;

        if (pthread_create(&threads[i], NULL, Worker, this) != 0) {
            //this传入worker，就能访问ThreadPool类的非静态成员了
            delete []threads;
            throw std::exception();
        }

        if (pthread_detach(threads[i]) != 0) {//线程分离
            delete []threads;
            throw std::exception(); 
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete []threads;
    thread_stop = true;
}

//把任务加入到请求队列，保证线程同步
template<typename T>
bool ThreadPool<T>::WorkAppend(T* request) {
    request_list_locker.Lock();
    if (request_list.size() >= max_reqs) {//请求队列大于最大数
        request_list_locker.Unlock();
        return false;
    }

    else {
        request_list.push_back(request);//添加任务
        request_list_locker.Unlock();
        request_list_sem.Post();//信号量加一
        return true;
    }
}

template<typename T>
void * ThreadPool<T>::Worker(void * arg) {
    ThreadPool * threadpool_ptr = (ThreadPool *)arg;
    threadpool_ptr->ThreadPoolRun();
    return threadpool_ptr;
}

template<typename T>
void ThreadPool<T>::ThreadPoolRun() {
    while(!thread_stop) {
        request_list_sem.Wait();//等待请求队列的信号量
        request_list_locker.Lock();

        if (request_list.empty()) {//请求队列为空
            request_list_locker.Unlock();
            continue;
        }

        T * request = request_list.front();
        request_list.pop_front();
        request_list_locker.Unlock();

        if (!request) continue;//取到NULL

        request->Process();//执行操作

    }
} 
#endif
