#ifndef HTTPCONNECT_H
#define HTTPCONNECT_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>
#include "ThreadPool.h"
#include "ThreadLocker.h"

class HTTPConnect {
public:
    static int epollfd; //所有的socket事件都要放到一个epoll里
    static int user_count;//用户数量
    static const int read_buffer_size = 2048;//读缓冲大小
    static const int write_buffer_size = 1024;//写缓冲大小
    static const int filename_len = 200; //文件名最大长度

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };


    //HTTPConnect();
    //~HTTPConnect();
    void Process();
    void Init(int http_connect_fd, const sockaddr_in &client_addr);
    void Close_Connect();
    bool Read();
    bool Write();
    

private:
    int sockfd;//当前事件使用的sockfd
    sockaddr_in sockaddr;//当前sockfd的地址信息
    char read_buffer[read_buffer_size];//读缓冲
    int read_index;//已读部分最后一个字节的下标的后一位
    int check_index;//当前正在分析的字符在读缓冲区的位置
    int start_line;//当前正在解析的行的起始位置
    CHECK_STATE check_state;//主状态机的当前状态
    METHOD method;
    char * url;
    char * version;
    char * host;//主机名
    int content_length;//消息内容长度
    bool linker;//是否长连接
    char real_file[filename_len];//文件名
    char* file_address;//目标文件映射到内存的地址
    struct stat file_stat;//文件状态：存在、目录、可读可写、文件大小
    char write_buffer[write_buffer_size];//写缓冲区
    int write_index;//写缓冲区待发送的字节数
    struct iovec iv[2];//writev操作需要的结构体
    int iv_count;//被写的内存块数量

    int bytes_to_send;//将要发送的数据字节数
    int bytes_have_send;//已发送的字节数

    void OthersInit();
    HTTP_CODE ProcessRead();
    bool ProcessWrite(HTTP_CODE read_ret);
    LINE_STATUS ParseRequestLine();
    HTTP_CODE ParseRequestHeadLine(char * texts);
    HTTP_CODE ParseRequestHeaders(char * texts);
    HTTP_CODE ParseRequestContent(char * texts);
    HTTP_CODE DoRequest();
    void unmap();
    char * GetLine() {return read_buffer + start_line;};

    bool AddResponse( const char* format, ... );
    bool AddStatusLine(int status, const char* title);
    bool AddHeaders(int content_len);
    bool AddContentLength(int content_len); 
    bool AddLinker();
    bool AddBlankLine();
    bool AddContent(const char* content);
    bool AddContentType(); 
};

#endif