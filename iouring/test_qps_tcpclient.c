#include<stdio.h>
#include<stdlib.h>
#include <string.h> 
#include <unistd.h>     //包含Unix操作系统API，比如getopt、read、write等
#include <netinet/in.h> //网络相关定义，包括sockaddr_in结构和htons等函数
#include <arpa/inet.h>  //提供IP地址转换函数，比如inet_addr、inet_ntoa等
#include <sys/time.h>   //用于获取系统时间，测量程序运行时间
#include <pthread.h>    //用于创建和管理线程

typedef struct test_context_s{
    char serverip[16];
    int port;
    int threadnum;  //表示要创建的线程个数
    int connection; //表示每个线程要发起的连接数
    int requestion; //表示每个线程要发起的请求数
    int failed;     //失败的请求数
}test_context_t;

typedef struct test_context_s test_context_t;

//用于连接tcp服务器
int connect_tcpserver(const char *ip,unsigned short port){
    int connfd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in tcpserver_addr;
    memset(&tcpserver_addr,0,sizeof(struct sockaddr_in));

    tcpserver_addr.sin_family = AF_INET;
    tcpserver_addr.sin_addr.s_addr = inet_addr(ip);  //将点分十进制的IP转换为32位整数
    tcpserver_addr.sin_port = htons(port);

    int ret = connect(connfd,(struct sockaddr*)&tcpserver_addr,sizeof(struct sockaddr_in));
    if (ret) {
        perror("connect\n");
        return -1;
    }

    return connfd;
}

//该宏用于计算两个struct timeval类型的时间差（以毫秒为单位）
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)
#define TEST_MESSAGE   "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890abcdefghijklmnopqrstuvwxyz\r\n"

#define RBUFFER_LENGTH		2048
#define WBUFFER_LENGTH		2048

//用于发送和接收tcp数据包
int send_recv_tcppkt(int fd){
    char wbuffer[WBUFFER_LENGTH] = {0};
    /*
    这个 for 循环将 TEST_MESSAGE 字符串复制到 wbuffer 缓冲区的不同位置，
    目的是填充缓冲区并确保发送的数据量较大。
    这是为了模拟在高并发网络请求中，发送更大的数据量，
    从而测试TCP连接的性能，特别是吞吐量和延迟
    */
    for(int i = 0;i < 8;i++){
        strcpy(wbuffer + i * strlen(TEST_MESSAGE), TEST_MESSAGE);
    }

    int res = send(fd,wbuffer,strlen(wbuffer),0);
    if (res < 0) {
        exit(1);
    }

    char rbuffer[RBUFFER_LENGTH] = {0};
    res = recv(fd, rbuffer, RBUFFER_LENGTH, 0);
    if (res <= 0) {
        exit(1);
    }
    //校验收到的数据
    if (strcmp(rbuffer, wbuffer) != 0) {
        printf("failed: '%s' != '%s'\n", rbuffer, wbuffer);
        return -1;
    }

    return 0;
}

static void *test_qps_entry(void *arg){
    test_context_t *pctx = (test_context_t*)arg;

    int connfd = connect_tcpserver(pctx->serverip,pctx->port);
    if(connfd<0){
        printf("connect_tcpserver failed\n");
        return NULL;
    }
    //count 的作用是计算 每个线程需要处理的请求数量
    int count=pctx->requestion/pctx->threadnum;
    int i = 0;
    int res;
    while(i++<count){
        res = send_recv_tcppkt(connfd);
        if(res != 0){
            //printf("send_recv_tcppkt failed\n");
			pctx->failed ++; // 
            continue;
        }	
    }
    return NULL;
}

// ./test_qps_tcpclient -s 127.0.0.1 -p 2048 -t 50 -c 100 -n 100000
int main(int argc,char *argv[]){
    int ret = 0;
    int opt;
    test_context_t ctx ={0};

        while ((opt = getopt(argc, argv, "s:p:t:c:n:?")) != -1) {
        switch (opt) {
            case 's':  //设置服务器的IP地址（存储在ctx.serverip中）
                printf("-s: %s\n", optarg);
                strcpy(ctx.serverip, optarg);
                break;
            case 'p':  //设置服务器的端口号（存储在ctx.port中）
                printf("-p: %s\n", optarg);
                ctx.port = atoi(optarg);
                break; 
            case 't':  //设置并发线程数（存储在ctx.threadnum中）
                printf("-t: %s\n", optarg);
                ctx.threadnum = atoi(optarg);
                break;
            case 'c':  //设置并发连接数（存储在ctx.connection中）
                printf("-c: %s\n", optarg);
                ctx.connection = atoi(optarg);
                break;
            case 'n':  //设置总请求数量（存储在ctx.requestion中）
                printf("-n: %s\n", optarg);
                ctx.requestion = atoi(optarg);
                break;
            default:
                return -1;
        }
    }

    //创建线程组
    pthread_t *ptid = (pthread_t*)malloc(ctx.threadnum * sizeof(pthread_t));
    int i = 0;

    struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);
	for (i = 0;i < ctx.threadnum;i ++) {
		pthread_create(&ptid[i], NULL, test_qps_entry, &ctx);
	}
	
	for (i = 0;i < ctx.threadnum;i ++) {
		pthread_join(ptid[i], NULL);
	}

	struct timeval tv_end;
	gettimeofday(&tv_end, NULL);

	int time_used = TIME_SUB_MS(tv_end, tv_begin);

	
	printf("success: %d, failed: %d, time_used: %d, qps: %d\n", ctx.requestion-ctx.failed, 
		ctx.failed, time_used, ctx.requestion * 1000 / time_used);


clean: 
	free(ptid);

	return ret;
}

