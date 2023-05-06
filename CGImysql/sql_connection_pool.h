#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool // sql连接池
{
public:
	MYSQL *GetConnection();				 //获取一个连接池中的数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式 这个是利用局部静态变量懒汉模式实现单例（多线程不友好，有多线程安全问题，饿汉没有）
	//对于多线程而言，多个线程可能同时访问到该静态变量，并发现其没有被初始化（C++实现机制是）
	//（该看静态变量内部一标志位是为1，为1则说明已被初始化）
	//多个线程同时判定其没有初始化而后均初始化就会造成错误（即它不是一个原子操作）
	//PS-C++11之后局部静态变量线程安全
	static connection_pool *GetInstance(); // 	有一个获取实例的静态方法，可以全局访问
	// 静态方法属于类不属于对象，因此没有this指针，所以静态方法只能访问静态变量不可以访问其他非静态变量

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	// 构造拷贝构造赋值构造和析构为私有类型   // 因为是使用的单例模式
	connection_pool();
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	locker lock;  // 懒汉加锁来保证线程安全
	list<MYSQL *> connList; //连接池具体实现：双链表
	sem reserve;  // 连接池的数量

public:
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};

class connectionRAII{   // 使用局部对象来管理资源，实现自动初始化和销毁。我的理解这是一个sql连接

public:
	// 构造，获取一个连接，并将外部sql连接指向该连接。 con类似传出参数
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();	// 析构，释放一个连接
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
