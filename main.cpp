#include <cstdio>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iostream>
#include <signal.h>
#include "ThreadLocker.h"
#include "ThreadPool.h"
#include "HTTPConnect.h"
#include <vector>
#define MAX_FD 65535//最大用户数
#define MAX_EVENT_NUM 10000//epoll最大监听数量




//添加信号捕捉
void AddSig(int sig, void(handler)(int)) {//handler函数名
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);//全部阻塞
    sigaction(sig, &sa, NULL);
}

//添加文件描述符到epoll中
extern void AddFd(int epfd, int fd, bool one_shot);
extern void RemoveFd(int epfd, int fd);
extern void ModifyFd(int epfd, int fd, int ev, bool one_shot);

int main(int argc, char* argv[]) {

    if (argc <= 1) {
        std::cout << "按照如下格式运行：" << basename(argv[0]) <<" port number";
        exit(-1);
    }

    int port = atoi(argv[1]);//端口号字符串转化为数字

    //对SIGPIPE信号进行处理
    AddSig(SIGPIPE, SIG_IGN);

    //创建线程池并初始化
    ThreadPool<HTTPConnect> * threadpool = NULL;
    try {
        threadpool = new ThreadPool<HTTPConnect>;
    }
    catch(...) {
        exit(1);
    }

    HTTPConnect * Users = new HTTPConnect[MAX_FD];

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        exit(-1);
    }

    //端口复用在bind之前
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, "49.123.113.133", &server_addr.sin_addr.s_addr);
    int ret = bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("bind");
        exit(-1);
    }

    ret = listen(listen_fd, 5);

    //创建epoll
    epoll_event events[MAX_EVENT_NUM];
    int epfd = epoll_create(1);

    //添加listenfd
    AddFd(epfd, listen_fd, false);
    
    //共用一个epollfd
    HTTPConnect::epollfd = epfd;

    while(1) {
        int num = epoll_wait(epfd, events, MAX_EVENT_NUM, -1);
        if ((num < 0) && (errno != EINTR)) {
            std::cout << "epoll失败" << std::endl;
            break;
        }

        //循环遍历发生事件数组
        for (int i = 0; i < num; i++) {
            //listen到新客户，加入epoll
            if (events[i].data.fd == listen_fd) {
                sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                //接收新连接
                int http_connect_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);

                if (HTTPConnect::user_count >= MAX_FD) {//服务器正忙
                    close(http_connect_fd);
                    continue;
                }//Users加入新的fd并初始化
                Users[http_connect_fd].Init(http_connect_fd, client_addr);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //客户端异常断开或者错误等事件，关闭连接
                Users[events[i].data.fd].Close_Connect();
            }
            else if (events[i].events & EPOLLIN) {
                if (Users[events[i].data.fd].Read()) {
                    //一次性读完，交给线程池处理
                    threadpool->WorkAppend(&Users[events[i].data.fd]);
                }
                else {//读取失败，关闭连接
                    Users[events[i].data.fd].Close_Connect();
                }
            }
            else if (events[i].events & EPOLLOUT) {
                if (!Users[events[i].data.fd].Write()) {
                    Users[events[i].data.fd].Close_Connect();
                }
            }
        }
    }

    close(epfd);
    close(listen_fd);
    delete []Users;
    delete threadpool;
    
    return 0;
}

