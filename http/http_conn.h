#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"


class http_conn
{
public:
	//读取文件长度上限
    static const int FILENAME_LEN = 200;
	//读缓存大小
    static const int READ_BUFFER_SIZE = 2048;
	//写缓存大小
    static const int WRITE_BUFFER_SIZE = 1024;
	//HTTP请求方法名
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
	//主状态机状态，表示此时在分析请求报文的哪个部分   // 请求行、请求头和请求报文
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
	//HTTP状态码
    enum HTTP_CODE
    {
        NO_REQUEST,     // 请求报文不完整
        GET_REQUEST,    // 读取到完整报文
        BAD_REQUEST,    // 请求有误， 如请求的是个目录、请求行格式不对等。
        NO_RESOURCE,    // 请求资源不存在
        FORBIDDEN_REQUEST,  // 禁止访问
        FILE_REQUEST,   // 获取文件成功
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
	//从状态机的状态，文本解析是否成功   // 解析一行
    enum LINE_STATUS
    {
        LINE_OK = 0,  // 获取到完整请求行
        LINE_BAD,
        LINE_OPEN     // 请求行还不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
	//初始化套接字
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
	//关闭HTTP连接
    void close_conn(bool real_close = true);
	//http处理函数
    void process();
	//读取浏览器发送的数据
    bool read_once();
	//给相应报文中写入数据
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
	//初始化数据库读取线程
    void initmysql_result(connection_pool *connPool);
	
    int timer_flag;//是否关闭连接  1代表此时需要删除定时器关闭连接
    int improv;//是否正在处理数据中


private:
    void init();
	//从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
	//向m_write_buf写入响应报文数据
    bool process_write(HTTP_CODE ret);
	//主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
	//主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
	//主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
	//生成响应报文
    HTTP_CODE do_request();
	//m_start_line是已经解析的字符
	//get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
	//从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();
	//之后重点介绍**
    void unmap();
	//根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;  // 静态变量用于记录所有的http连接数量
    MYSQL *mysql;  // 用于从sql连接池中取出一个连接
    int m_state;  //IO 事件类别:读为0, 写为1  这些事件需要加入线程池，用于表示这是什么事件

private:
    int m_sockfd;  // 这个http连接用户的connfd
    sockaddr_in m_address;  // ip地址
	//存储读取的请求报文数据
    char m_read_buf[READ_BUFFER_SIZE];
	//缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_read_idx;  // 已经使用revc读取的位置
	//m_read_buf读取的位置m_checked_idx
    int m_checked_idx;
	//m_read_buf中已经解析的字符个数
    int m_start_line;
	//存储发出的响应报文数据
    char m_write_buf[WRITE_BUFFER_SIZE];
	//指示buffer中的长度
    int m_write_idx;
	//主状态机的状态  表示此时在分析请求报文的哪个部分   // 请求行、请求头和请求报文
    CHECK_STATE m_check_state;
	//请求方法
    METHOD m_method;    //  请求行中的method
	//以下为解析请求报文中对应的6个变量
    //存储读取文件的名称
    char m_real_file[FILENAME_LEN];   // 实际请求文件路径
    char *m_url;           // 请求行中的url，即请求的url页面
    char *m_version;        
    char *m_host;           // 请求头中的host， 请求服务器域名
    int m_content_length;
    bool m_linger;          // 请求头 keep-alive
	//读取服务器上的文件地址
    char *m_file_address;   // mmap内存映射到的地址。对于请求的文件不进行写缓冲区发送，而是直接内存映射。
    struct stat m_file_stat;   // 客户端请求的服务端的文件的状态信息（是否存在，是否有权限等信息） // 传出参数，纪录客户端请求的服务端文件的状态信息
	//io向量机制iovec  0是写缓冲区的地址长度，1是请求文件的地址长度
    struct iovec m_iv[2];   // writev中使用了->聚集写。  这是两个分散的buffer
    int m_iv_count;
    int cgi;        //是否启用的POST     ？？依旧不懂 cgi默认值是0，当请求方法是POST是cgi = 1；这样岂不是跟m_method一样的作用？？
    char *m_string; //存储请求头数据   请求体中的数据
	 //剩余发送字节数
    int bytes_to_send;
	//已发送字节数
    int bytes_have_send;
    char *doc_root;   // webserver服务器定义的根目录

    map<string, string> m_users;//用户名密码匹配表
    int m_TRIGMode;//触发模式  ET / LT
    int m_close_log;//是否开启日志

    // 存储mysql相关信息
    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
