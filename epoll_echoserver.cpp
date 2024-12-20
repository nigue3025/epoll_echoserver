#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <thread>
#include <fcntl.h>

#define DEFAULT_BUFLEN 2560
#define DEFAULT_PORT 9000
#define DEFAULT_IP "127.0.0.1"
#define MAX_EVENTS 10

std::string HttpResponseContent = "HTTP/1.2 200 OK\r\nContent-Length:2\r\nContent-Type: text/html\r\n\r\nHi";

// 设置非阻塞模式
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 工作线程处理 I/O
void workerThread_RecvAndSend(int epollFd) {
    struct epoll_event events[MAX_EVENTS];
    char buffer[DEFAULT_BUFLEN];

    while (true) {
        int eventCount = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        for (int i = 0; i < eventCount; ++i) {
            int clientFd = events[i].data.fd;

            if (events[i].events & EPOLLIN) {
                // 接收資料
                memset(buffer, 0, DEFAULT_BUFLEN);
                int bytesReceived = recv(clientFd, buffer, DEFAULT_BUFLEN, 0);
                if (bytesReceived <= 0) {
                    close(clientFd);
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
                    std::cout << "Connection closed by client\n";
                } else {
                    std::cout << "Received: " << buffer << std::endl;

                    // 回傳資料
                    send(clientFd, buffer, bytesReceived, 0);
                }
            }
        }
    }
}

int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    
    setNonBlocking(serverFd);

    // 進行address binding
    struct sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(DEFAULT_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed");
        close(serverFd);
        return 1;
    }

    if (listen(serverFd, SOMAXCONN) == -1) {
        perror("Listen failed");
        close(serverFd);
        return 1;
    }

    // 创建 epoll 实例
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("Epoll creation failed");
        close(serverFd);
        return 1;
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = serverFd;

    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) == -1) {
        perror("Epoll ctl failed");
        close(serverFd);
        close(epollFd);
        return 1;
    }

    // 創建worker thread
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back(workerThread_RecvAndSend, epollFd);
    }

    // 主執行緒作為新的client接收端,迴圈中止條件待定
    while (true) {
        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);

        if (clientFd != -1) {
            setNonBlocking(clientFd);
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = clientFd;
            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
                perror("Epoll ctl add client failed");
                close(clientFd);
            } else {
                std::cout << "New connection accepted\n";
            }
        }
    }


    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    close(serverFd);
    close(epollFd);
    return 0;
}