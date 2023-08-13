#include "HTTPConnect.h"

//静态成员类外初始化
int HTTPConnect::epollfd = -1; 
int HTTPConnect::user_count = 0;
//网站根目录
const char* doc_root = "/home/decone/webserver/resources";
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int SetNonBlocking(int fd) {
    int old_fcntl = fcntl(fd, F_GETFL);
    old_fcntl |= O_NONBLOCK;//加上非阻塞
    fcntl(fd, F_SETFL ,old_fcntl);
}


//向epoll中添加需要监听的socketfd
void AddFd(int epfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;//任意时刻只触发一次，只被一个线程操作
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    //文件描述符非阻塞
    SetNonBlocking(fd);
}

//从epoll中移除socketfd
void RemoveFd(int epfd, int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

//修改socketfd，要重置ONESHOT
void ModifyFd(int epfd, int fd, int ev, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLHUP | ev;
    if (one_shot) {
        event.events |= EPOLLONESHOT;//任意时刻只触发一次，只被一个线程操作
    }
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);
}

//初始化
void HTTPConnect::Init(int http_connect_fd, const sockaddr_in &client_addr)  {
    sockfd = http_connect_fd;
    sockaddr = client_addr;
    
    //端口复用
    int optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    //新fd添加到epoll中，一次触发为true
    AddFd(epollfd, sockfd, true);
    user_count++;

    OthersInit();
}

//初始化剩余信息
void HTTPConnect::OthersInit() {
    check_state = CHECK_STATE_REQUESTLINE;
    read_index = 0;
    check_index = 0;
    start_line = 0;
    write_index = 0;
    method = GET;
    url = NULL;
    version = NULL;
    host = NULL;
    linker = false;
    content_length = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;
    
    bzero(read_buffer, read_buffer_size);
    bzero(write_buffer, write_buffer_size);
    bzero(real_file, filename_len);
}

//关闭连接
void HTTPConnect::Close_Connect() {
    if (sockfd != -1) {
        RemoveFd(epollfd, sockfd);
        sockfd = -1;
        user_count--;
    }  
}

//循环读取数据直到读完或客户端关闭连接
bool HTTPConnect::Read() {
    if(read_index >= read_buffer_size) {
        return false;
    }

    //读取字节
    int read_bytes = 0;
    while(1) {
        read_bytes = recv(sockfd, read_buffer + read_index, read_buffer_size - read_index, 0);
        if (read_bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK){//非阻塞下没有可读的报错
                break;
            }
            return false;
        }
        else if (read_bytes == 0) {//客户端关闭
            return false;
        }
        read_index += read_bytes;
    }
    std::cout << "读取的数据：" << read_buffer << std::endl;
    return true;
}

bool HTTPConnect::Write() {
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        ModifyFd(epollfd, sockfd, EPOLLIN, true); 
        OthersInit();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(sockfd, iv, iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                ModifyFd(epollfd, sockfd, EPOLLOUT, true);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;

        if (bytes_have_send >= iv[0].iov_len)
        {
            iv[0].iov_len = 0;
            iv[1].iov_base = file_address + (bytes_have_send - write_index);
            iv[1].iov_len = bytes_to_send;
        }
        else
        {
            iv[0].iov_base = write_buffer + bytes_have_send;
            iv[0].iov_len = iv[0].iov_len - temp;
        }

        if ( bytes_to_send <= 0 ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(linker) {
                OthersInit();
                ModifyFd(epollfd, sockfd, EPOLLIN, true);
                return true;
            } else {
                ModifyFd(epollfd, sockfd, EPOLLIN, true);
                return false;
            } 
        }
    }
}

//工作线程处理请求，但不读写！
void HTTPConnect::Process() {
    //根据读到的信息解析http请求
    HTTP_CODE read_ret = ProcessRead();
    if (read_ret == NO_REQUEST) {
        ModifyFd(epollfd, sockfd, EPOLLIN, true);
        return;
    }
    //生成写响应
    bool write_ret = ProcessWrite(read_ret);
    if (!write_ret) {
        Close_Connect();
    }
    ModifyFd(epollfd, sockfd, EPOLLOUT, true);
}

//解析http请求，主状态机
HTTPConnect::HTTP_CODE HTTPConnect::ProcessRead() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE HTTP_code = NO_REQUEST;

    char * texts = 0;
    while(((check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))//情况一：解析到内容，且每行不出问题
        ||((line_status = ParseRequestLine()) == LINE_OK)) {//解析了一行，且不出问题
        //获取一行数据
        texts = GetLine();
        start_line = check_index;
        std::cout << "获取一行HTTP信息：" << texts << std::endl;

        switch (check_state){
            //解析到首行
            case CHECK_STATE_REQUESTLINE:
            {
                HTTP_code = ParseRequestHeadLine(texts);
                if (HTTP_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
                
            case CHECK_STATE_HEADER:
            {
                HTTP_code = ParseRequestHeaders(texts);
                if (HTTP_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                //头部读完，请求具体内容
                else if (HTTP_code == GET_REQUEST) {
                    return DoRequest();
                }
                break;
            }
            
            case CHECK_STATE_CONTENT:
            {
                HTTP_code = ParseRequestContent(texts);
                if (HTTP_code == GET_REQUEST) {
                    return DoRequest();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

//解析完内容进行操作，把需要的资源进行内存映射
HTTPConnect::HTTP_CODE HTTPConnect::DoRequest() {
    strcpy(real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(real_file + len, url, filename_len - len - 1);//网站根目录 + url
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if (stat(real_file, &file_stat) < 0) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (!(file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(real_file, O_RDONLY);
    // 创建内存映射
    file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//内存映射回收
void HTTPConnect::unmap() {
    if(file_address )
    {
        munmap(file_address, file_stat.st_size);
        file_address = 0;
    }
}

//解析一行
HTTPConnect::LINE_STATUS HTTPConnect::ParseRequestLine() {
    char temp;
    for (; check_index < read_index; check_index++) {
        temp = read_buffer[check_index];//逐个读取字符
        if (temp == '\r') {
            if ((check_index + 1) == read_index) {
                return LINE_OPEN;//数据不完整
            }
            else if (read_buffer[check_index + 1] == '\n') {
                //读到完整一行
                read_buffer[check_index++] = '\0';
                read_buffer[check_index] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') {
            if ((check_index > 1) && (read_buffer[check_index - 1] == '\r')) {
                read_buffer[check_index - 1] = '\0';
                read_buffer[check_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//解析首行
HTTPConnect::HTTP_CODE HTTPConnect::ParseRequestHeadLine(char * texts) {
    // GET / HTTP/1.1
    url = strpbrk(texts, " \t");//匹配首个相同字符的位置
    //GET\0/ HTTP/1.1
    *url++ = '\0';
    char * method_name = texts;
    if (strcasecmp(method_name, "GET") == 0) {
        method = GET;
    }
    else return BAD_REQUEST;

    // GET\0/\0HTTP/1.1
    version = strpbrk(url, " \t");
    if (!version) {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    if (strcasecmp(version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    // http://192.168.1.1:10000/index.html
    if (strncasecmp(url, "HTTP://", 7) == 0) {
        url += 7;// 192.168.1.1:10000/index.html
        url = strchr(url, '/');// /index.html
    }
    if (!url || url[0] != '/') {
        return BAD_REQUEST;
    }
    check_state = CHECK_STATE_HEADER;//检查首行完成，开始检查头部
    return NO_REQUEST;
}

//解析头部
HTTPConnect::HTTP_CODE HTTPConnect::ParseRequestHeaders(char * texts) {
    // 遇到空行，表示头部字段解析完毕
    if (texts[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取content_length字节的消息体
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (content_length != 0) {
            check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } 
    else if (strncasecmp(texts, "Connection:", 11) == 0) {
        // 处理Connection 头部字段  Connection: keep-alive
        texts += 11;
        texts += strspn(texts, " \t");
        if (strcasecmp(texts, "keep-alive") == 0) {
            linker = true;
        }
    } 
    else if (strncasecmp(texts, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        texts += 15;
        texts += strspn(texts, " \t");
        content_length = atol(texts);
    } 
    else if (strncasecmp(texts, "Host:", 5) == 0) {
        // 处理Host头部字段
        texts += 5;
        texts += strspn(texts, " \t");
        host = texts;
    } 
    else {
        std::cout << "未知请求头部" << texts << std::endl; 
    }
    return NO_REQUEST;
}

//解析内容
HTTPConnect::HTTP_CODE HTTPConnect::ParseRequestContent(char * texts) {
    if (read_index >= (content_length + check_index))
    {
        texts[content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//生成响应，写回数据
bool HTTPConnect::ProcessWrite(HTTP_CODE read_ret) {
    switch (read_ret)
    {
        case INTERNAL_ERROR:
            AddStatusLine(500, error_500_title);
            AddHeaders(strlen(error_500_form));
            if (!AddContent(error_500_form)) {
                return false;
            }
            break;
        case BAD_REQUEST:
            AddStatusLine(400, error_400_title);
            AddHeaders(strlen(error_400_form));
            if (!AddContent(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            AddStatusLine(404, error_404_title);
            AddHeaders(strlen(error_404_form));
            if (!AddContent(error_404_form)) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            AddStatusLine(403, error_403_title);
            AddHeaders(strlen(error_403_form));
            if (!AddContent(error_403_form)) {
                return false;
            }
            break;
        case FILE_REQUEST:
            AddStatusLine(200, ok_200_title);
            AddHeaders(file_stat.st_size);
            //把消息头和消息体分别存入iv[0]和iv[1]
            iv[0].iov_base = write_buffer;
            iv[0].iov_len = write_index;
            iv[1].iov_base = file_address;
            iv[1].iov_len = file_stat.st_size;
            iv_count = 2;

            bytes_to_send = write_index + file_stat.st_size;
            return true;
        default:
            return false;
    }

    iv[0].iov_base = write_buffer;
    iv[0].iov_len = write_index;
    iv_count = 1;
    return true;
}

//往写缓冲区写入数据，write_buffer
bool HTTPConnect::AddResponse(const char* format, ...) {
    if(write_index >= write_buffer_size) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf( write_buffer + write_index, write_buffer_size - 1 - write_index, format, arg_list);
    if(len >= (write_buffer_size - 1 - write_index)) {
        return false;
    }
    write_index += len;
    va_end(arg_list);
    return true;
}

bool HTTPConnect::AddStatusLine(int status, const char* title) {
    return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HTTPConnect::AddHeaders(int content_len) {
    AddContentLength(content_len);
    AddContentType();
    AddLinker();
    AddBlankLine();
}

bool HTTPConnect::AddContentLength(int content_len) {
    return AddResponse("Content-Length: %d\r\n", content_len);
}

bool HTTPConnect::AddLinker() {
    return AddResponse("Connection: %s\r\n", (linker == true) ? "keep-alive" : "close");
}

bool HTTPConnect::AddBlankLine() {
    return AddResponse("%s", "\r\n");
}

bool HTTPConnect::AddContent(const char* content) {
    return AddResponse("%s", content);
}

bool HTTPConnect::AddContentType() {
    return AddResponse("Content-Type:%s\r\n", "text/html");
}