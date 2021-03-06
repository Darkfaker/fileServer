#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H
#define _CRT_SECURE_NO_WARNINGS
#include "STL"
#include "LinuxFun"
#include "MD5.h"
#include "sockpair.h"
using namespace std;

/*process control*/
class process
{
public:
	process() : m_pid(-1){}
public:
	pid_t m_pid;        
	int m_pipefd[2];
};

/*pool class */
template< typename T >
class processpool{
public:
	static processpool<T>* create(int listenfd, int process_number = 8){
		if (!m_instance)
			m_instance = new processpool< T >(listenfd, process_number);
		return m_instance;
	}

	~processpool(){
		delete[] m_sub_process;
	}
	
    void run();

private:
	
    processpool(int listenfd, int process_number = 8);

	void setup_sig_pipe();
	
    void run_parent();
	
    void run_child();
    
    /*max process number*/
	static const int MAX_PROCESS_NUMBER = 16;
	/*cli number*/
    static const int USER_PER_PROCESS = 65536;
	/*epoll process number*/
    static const int MAX_EVENT_NUMBER = 10000;
	/*process number*/
	int m_process_number;
	/*process id*/
	int m_idx;
	
    int m_epollfd;
	
    int m_listenfd;

	int m_stop;
	
    process* m_sub_process;
	
    static processpool< T >* m_instance;
};

template< typename T >
processpool< T >* processpool< T >::m_instance = NULL;

static int sig_pipefd[2];

static int setnonblocking(int fd){
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
	return fd;
}

static void addfd(int epollfd, int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

static void removefd(int epollfd, int fd){
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

static void sig_handler(int sig){
	int save_errno = errno;
	int msg = sig;
	send(sig_pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true){
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;//指定信号处理函数
	if (restart){
		sa.sa_flags |= SA_RESTART;//设置程序接收到信号后重新调用被该信号终止的系统调用
	}
	sigfillset(&sa.sa_mask);//sa_mask为进程的信号集掩码，sigfillset在信号集中设置所有信号
	assert(sigaction(sig, &sa, NULL) != -1);
}

template< typename T >
processpool< T >::processpool(int listenfd, int process_number)
: m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false){
	assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));
	//传入要创建的进程数，new 子进程管理对象
	m_sub_process = new process[process_number];
	assert(m_sub_process);

	for (int i = 0; i < process_number; ++i){
		//创建父子间的全双工管道
		int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
		assert(ret == 0);
		//fork 子进程
		m_sub_process[i].m_pid = fork();
		assert(m_sub_process[i].m_pid >= 0);
		if (m_sub_process[i].m_pid > 0){
			close(m_sub_process[i].m_pipefd[1]);
			continue;
		}
		else{
			close(m_sub_process[i].m_pipefd[0]);//如果是子进程则关闭 写端
			m_idx = i;
			break;
		}
	}
}

template< typename T >
void processpool< T >::setup_sig_pipe(){
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	assert(ret != -1);

	setnonblocking(sig_pipefd[1]);
	addfd(m_epollfd, sig_pipefd[0]);
	
    addsig(SIGCHLD, sig_handler);
	addsig(SIGTERM, sig_handler);
	addsig(SIGINT, sig_handler);
	addsig(SIGPIPE, SIG_IGN);
}

template< typename T >
void processpool< T >::run(){
	if (m_idx != -1){
		run_child();
		return;
	}
	run_parent();
}

template< typename T >
void processpool< T >::run_child(){
    setup_sig_pipe();
	
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
	addfd(m_epollfd, pipefd);

	epoll_event events[MAX_EVENT_NUMBER];
	T* users = new T[USER_PER_PROCESS];
	assert(users);
	int number = 0;
	int ret = -1;
	
    while (!m_stop){
		number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((number < 0) && (errno != EINTR)){
			printf("epoll failure\n");
			break;
		}
		
        for (int i = 0; i < number; i++){
			int sockfd = events[i].data.fd;
			if ((sockfd == pipefd) && (events[i].events & EPOLLIN)){
				int client = 0;
				ret = recv(sockfd, (char*)&client, sizeof(client), 0);
				if (((ret < 0) && (errno != EAGAIN)) || ret == 0){
					continue;
				}
				else//读取成功则表示有新用户连接
				{
					struct sockaddr_in client_address;
					socklen_t client_addrlength = sizeof(client_address);
					//为新用户 accept连接
					int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
					if (connfd < 0)
					{
						printf("errno is: %d\n", errno);
						continue;
					}
					//加入管道监听
					addfd(m_epollfd, connfd);
					//模本 T 类必须提供init初始化方法，以初始化一个连接，使用connfd索引提高效率
					users[connfd].init(m_epollfd, connfd, client_address);
				}
			}
			//如果为信号管道中有事件
			else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				int sig;
				char signals[1024];
				// 0 号读 处信号
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if (ret <= 0)//ret 为就绪描述符个数
				{
					continue;
				}
				else
				{
					//接收成功则进行循环处理
					//因为每个信号值占一个字节所以按字节逐个处理
					for (int i = 0; i < ret; ++i)
					{
						switch (signals[i])//判断信号类型
						{
						case SIGCHLD://子进程状态发生变化(退出或暂停)
						{
										 pid_t pid;
										 int stat;
										 //waitpid发现没有已退出的子进程则返回 0
										 while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
										 {
											 continue;
										 }
										 break;
						}
							//终止进程
						case SIGTERM://终止进程，kill命令默认的信号
						case SIGINT://ctrl + C 中断退出进程
						{
										m_stop = true;
										break;
						}
						default:
						{
								   break;
						}
						}
					}
				}
			}
			//如果是其他类型数据，则必然是老客户的请求，调用相应连接 子进程处理
			else if (events[i].events & EPOLLIN)
			{
				users[sockfd].process();
			}
			else
			{
				continue;
			}
		}
	}

	delete[] users;
	users = NULL;
	close(pipefd);
	//close( m_listenfd );
	//
	close(m_epollfd);
}
//父进程启动函数
template< typename T >
void processpool< T >::run_parent()
{
	//初始化信号管道
	setup_sig_pipe();
	//父进程监听 listenfd
	addfd(m_epollfd, m_listenfd);
	//建立内核事件表
	epoll_event events[MAX_EVENT_NUMBER];
	int sub_process_counter = 0;
	int new_conn = 1;
	int number = 0;
	int ret = -1;

	//等待 m_stop 结束标志
	while (!m_stop)
	{
		//epoll_wait 监听
		number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((number < 0) && (errno != EINTR))
		{
			printf("epoll failure\n");
			break;
		}
		//number 为epoll_wait 返回的就绪描述符的个数
		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == m_listenfd)//表明有新连接到来
			{
				//新连接会使用 ROUND ROBIN 的方法将新连接分配给一个子进程
				int i = sub_process_counter;
				do
				{
					if (m_sub_process[i].m_pid != -1)
					{
						break;
					}
					i = (i + 1) % m_process_number;
				} while (i != sub_process_counter);

				if (m_sub_process[i].m_pid == -1)
				{
					m_stop = true;
					break;
				}
				sub_process_counter = (i + 1) % m_process_number;
				//send( m_sub_process[sub_process_counter++].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
				send(m_sub_process[i].m_pipefd[0], (char*)&new_conn, sizeof(new_conn), 0);
				printf("send request to child %d\n", i);
				//sub_process_counter %= m_process_number;
			}
			//父进程处理其接收的信号
			else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				int sig;
				char signals[1024];
				//从信号管道读取信号
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if (ret <= 0)
				{
					continue;
				}
				else
				{
					//如果有信号则循环处理
					for (int i = 0; i < ret; ++i)
					{
						//判断信号类型
						switch (signals[i])
						{
						case SIGCHLD:
						{
										pid_t pid;
										int stat;
										while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
										{
											for (int i = 0; i < m_process_number; ++i)
											{
												if (m_sub_process[i].m_pid == pid)
												{
													printf("child %d join\n", i);
													close(m_sub_process[i].m_pipefd[0]);
													m_sub_process[i].m_pid = -1;
												}
											}
										}
										m_stop = true;
										for (int i = 0; i < m_process_number; ++i)
										{
											if (m_sub_process[i].m_pid != -1)
											{
												m_stop = false;
											}
										}
										break;
						}
						case SIGTERM:
						case SIGINT:
						{
									   printf("kill all the clild now\n");
									   for (int i = 0; i < m_process_number; ++i)
									   {
										   int pid = m_sub_process[i].m_pid;
										   if (pid != -1)
										   {
											   kill(pid, SIGTERM);
										   }
									   }
									   break;
						}
						default:
						{
								   break;
						}
						}
					}
				}
			}
			else
			{
				continue;
			}
		}
	}

	//close( m_listenfd );
	close(m_epollfd);
}

#endif
