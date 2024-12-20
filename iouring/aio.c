#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define BUF_SIZE 1024

int main() {
    struct aiocb read_cb, write_cb;
    int fd, ret;
    char buffer[BUF_SIZE];

    // 从终端获取输入
    printf("Please input text: ");
    fgets(buffer, BUF_SIZE, stdin);

    // 打开文件
    fd = open("test.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 初始化写操作的AIO控制块
    memset(&write_cb, 0, sizeof(struct aiocb));
    write_cb.aio_fildes = fd;
    write_cb.aio_buf = buffer;
    write_cb.aio_nbytes = strlen(buffer);
    write_cb.aio_offset = 0;

    // 异步写操作
    ret = aio_write(&write_cb);
    if (ret < 0) {
        perror("aio_write");
        close(fd);
        return 1;
    }

    // 等待写操作完成
    while (aio_error(&write_cb) == EINPROGRESS) {
        // 可以在这里做其他事情
        printf("Waiting for write to complete...\n");
        sleep(1);
    }

    // 获取写操作的返回状态
    ret = aio_return(&write_cb);
    if (ret < 0) {
        perror("aio_write return");
        close(fd);
        return 1;
    }

    // 初始化读操作的AIO控制块
    memset(&read_cb, 0, sizeof(struct aiocb));
    read_cb.aio_fildes = fd;
    read_cb.aio_buf = buffer;
    read_cb.aio_nbytes = BUF_SIZE;
    read_cb.aio_offset = 0;

    // 异步读操作
    ret = aio_read(&read_cb);
    if (ret < 0) {
        perror("aio_read");
        close(fd);
        return 1;
    }

    // 等待读操作完成
    while (aio_error(&read_cb) == EINPROGRESS) {
        // 可以在这里做其他事情
        printf("Waiting for read to complete...\n");
        sleep(1);
    }

    // 获取读操作的返回状态
    ret = aio_return(&read_cb);
    if (ret < 0) {
        perror("aio_read return");
        close(fd);
        return 1;
    }

    // 将从文件读取到的内容输出到终端（去除可能多余的空字符）
    buffer[ret] = '\0';
    printf("Content read from test.txt:\n%s", buffer);

    close(fd);
    return 0;
}