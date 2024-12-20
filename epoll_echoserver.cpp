#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <fcntl.h>
#include <errno.h>

#define DEFAULT_BUFLEN 2560
#define DEFAULT_PORT 9000
#define MAX_EVENTS 1024

std::string HttpResponseContent = "HTTP/1.2 200 OK\r\nContent-Length:2\r\nContent-Type: text/html\r\n\r\nHi";

// 設置非阻塞模式
void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 處理接收與發送邏輯
void handleClient(int clientFd, epoll_event& event, int epollFd) {
    char buffer[DEFAULT_BUFLEN];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(clientFd, buffer, sizeof(buffer), 0);

        if (bytesReceived == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 資料暫時讀完
                break;
            }
            perror("recv failed");
            close(clientFd);
            epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
            return;
        } else if (bytesReceived == 0) {
            // 客戶端關閉連線
            std::cout << "Client disconnected\n";
            close(clientFd);
            epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
            return;
        } else {
            // 回傳資料
            std::cout << "Received: " << buffer << std::endl;
            int bytesSent = send(clientFd, buffer, bytesReceived, 0);
            if (bytesSent == -1) {
                perror("send failed");
                close(clientFd);
                epoll_ctl(epollFd, EPOLL_CTL_DEL, clientFd, nullptr);
                return;
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


    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


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

    // 創建 epoll 實例
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("Epoll creation failed");
        close(serverFd);
        return 1;
    }

    // 添加 server socket 到 epoll
    struct epoll_event ev{}, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = serverFd;

    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &ev) == -1) {
        perror("Epoll ctl failed");
        close(serverFd);
        close(epollFd);
        return 1;
    }

    std::cout << "Server is running on port " << DEFAULT_PORT << "\n";

    while (true) {
        int eventCount = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (eventCount == -1) {
            perror("Epoll wait failed");
            break;
        }

        for (int i = 0; i < eventCount; ++i) {
            int fd = events[i].data.fd;

            if (fd == serverFd) {
                // 接受新連線
                while (true) {
                    struct sockaddr_in clientAddr{};
                    socklen_t clientLen = sizeof(clientAddr);
                    int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);

                    if (clientFd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            // 所有連線都已接受
                            break;
                        }
                        perror("Accept failed");
                        continue;
                    }

                    std::cout << "New client connected: FD " << clientFd << "\n";
                    setNonBlocking(clientFd);

                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = clientFd;
                    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) == -1) {
                        perror("Epoll ctl add client failed");
                        close(clientFd);
                    }
                }
            } else {
                // 處理現有客戶端 I/O
                handleClient(fd, events[i], epollFd);
            }
        }
    }

    close(serverFd);
    close(epollFd);
    return 0;
}