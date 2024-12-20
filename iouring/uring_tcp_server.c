#include <liburing/io_uring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <liburing.h>

#define ENTRIES_LENGTH 1024
#define BUFFER_LENGTH 1024
#define EVENT_ACCEPT 0
#define EVENT_READ 1
#define EVENT_WRITE 2

//用于存储socket的文件描述符和当前事件类型，便于关联队列中的I/O请求
struct conn_info{
    int fd;
    int event;
};

int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}
	listen(sockfd, 10);
	return sockfd;
}

//set_event_recv 函数的功能是向 io_uring 的提交队列（SQ，Submission Queue）中添加一个 接收数据（recv） 的事件，以便在 socket 上异步接收数据
int set_event_recv(struct io_uring *ring,int sockfd,void *buf,size_t len,int flags){
    //从 io_uring 的提交队列（Submission Queue，SQ）中获取一个条目（SQE, Submission Queue Entry），这个条目将描述一个具体的 I/O 操作
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

        struct conn_info recv_info = {
        .fd = sockfd,
        .event = EVENT_READ
    };
    //为提交队列条目（SQE）准备一个 接收数据（recv） 的操作
    io_uring_prep_recv(sqe, sockfd, buf, len, 0);

    memcpy(&sqe->user_data, &recv_info, sizeof(struct conn_info));

    return 0;
}

int set_event_send(struct io_uring *ring,int sockfd,void *buf,size_t len,int flags){
	struct io_uring_sqe *sqe=io_uring_get_sqe(ring);

	struct conn_info send_info={
		.fd=sockfd,
		.event=EVENT_WRITE
	};

	io_uring_prep_send(sqe,sockfd,buf,len,0);
	memcpy(&sqe->user_data,&send_info,sizeof(struct conn_info));

    return 0;
}

int set_event_accept(struct io_uring *ring,int sockfd,struct sockaddr *addr,socklen_t *addrlen,int flags){
	struct io_uring_sqe *sqe=io_uring_get_sqe(ring);  //获取这个uring的头指针

	struct conn_info accept_info={
		.fd=sockfd,
		.event=EVENT_ACCEPT
	};

	io_uring_prep_accept(sqe,sockfd,(struct sockaddr*)addr,addrlen,0);
	memcpy(&sqe->user_data,&accept_info,sizeof(struct conn_info));

    return 0;
}

int main(){
    unsigned short port = 2048;
    int sockfd = init_server(port);

    struct io_uring_params params;
    memset(&params,0,sizeof(params));

    //定义一个 struct io_uring 类型的变量 ring，用于管理 io_uring 的状态
    struct io_uring ring;
    //此操作会在内核中创建 提交队列（SQ） 和 完成队列（CQ）
    io_uring_queue_init_params(ENTRIES_LENGTH,&ring,&params);

    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);
    set_event_accept(&ring, sockfd, (struct sockaddr*)&clientaddr, &len, 0);

    char buffer[BUFFER_LENGTH] = {0};
    while(1){  //进入一个死循环，不断处理客户端的连接请求和 I/O 事件
        //提交uring环，让内核开始执行操作
        io_uring_submit(&ring); //调用 io_uring_submit，将提交队列（SQE）中的所有操作发送到内核，通知内核开始执行这些异步任务

        struct io_uring_cqe *cqe;
        //调用 io_uring_wait_cqe 阻塞等待完成队列（CQ）中出现新的事件
        //当内核完成某个异步操作时，会将其结果放入 CQ 中，并唤醒这个调用
        io_uring_wait_cqe(&ring, &cqe); //代码阻塞在这里

        struct io_uring_cqe *cqes[128];
        //调用 io_uring_peek_batch_cqe，非阻塞地获取最多 128 个完成事件
        int nready = io_uring_peek_batch_cqe(&ring,cqes,128);
        //遍历获取到的完成事件
        for(int i = 0;i < nready ;i++){
            struct io_uring_cqe *entries = cqes[i];
            struct conn_info result;
            memcpy(&result,&entries->user_data,sizeof(struct conn_info));
        
            if(result.event == EVENT_ACCEPT){
                //消耗一个sqe队列中的accept任务，需要重新设置
                set_event_accept(&ring,sockfd,(struct sockaddr*)&clientaddr,&len,0);
				//printf("io_uring_peek_batch_cqe_set\n");

                //收数据
				int connfd=entries->res;
                //如果没有数据，这个任务会被挂起
				set_event_recv(&ring,connfd,buffer,BUFFER_LENGTH,0);
            }else if(result.event==EVENT_READ){
                int ret=entries->res;
				//printf("io_uring_peek_batch_cqe_recv: %d, %s\n",ret,buffer);

                if(ret == 0){  //这表示 I/O 操作成功完成，但没有数据可供处理（比如读取到文件末尾）s
                    close(result.fd);
                }else if(ret > 0){
                    set_event_send(&ring,result.fd,buffer,ret,0);
                }
            }else if(result.event==EVENT_WRITE){
                int ret=entries->res;
				printf("io_uring_peek_batch_cqe_send: %d, %s\n",ret,buffer);

                set_event_recv(&ring,result.fd,buffer,BUFFER_LENGTH,0);
            }
        }
        //调用 io_uring_cq_advance，移动完成队列的指针，清理已处理的完成事件
        io_uring_cq_advance(&ring,nready);  //把cq队列清空
    }

    return 0;
}

