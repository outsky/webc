#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

int Listen(const char *port, int backlog) {
    struct addrinfo hint, *res = NULL;
    memset(&hint, 0, sizeof(hint));
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;
    hint.ai_flags = AI_PASSIVE;
    if (getaddrinfo("0.0.0.0", port, &hint, &res) != 0) {
        perror("[x] getaddrinfo");
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, 0);
    if (fd < 0) {
        perror("[x] socket");
        return -1;
    }

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        perror("[x] setsockopt");
        return -1;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) != 0) {
        perror("[x] bind");
        return -1;
    }

    if (listen(fd, backlog) != 0) {
        perror("[x] listen");
        return -1;
    }

    struct sockaddr_in* sk = (struct sockaddr_in*)res->ai_addr;
    char ip[64];
    if (inet_ntop(res->ai_family, &sk->sin_addr, ip, sizeof(ip)) == NULL) {
        perror("[x] inet_ntop");
        return -1;
    }
    printf("listening at %s:%d\n", ip, ntohs(sk->sin_port));
    return fd;
}

int Accept(int fd) {
    int clt = accept(fd, NULL, NULL);
    if (clt < 0) {
        perror("[x] accept");
        return -1;
    }
    printf("new connect: %d\n", clt);
    return clt;
}

int Epoll_add(int ep, int fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) != 0) {
        perror("[x] Epoll_add");
        return -1;
    }
    return 0;
}

int Epoll_remove(int ep, int fd) {
    if (epoll_ctl(ep, EPOLL_CTL_DEL, fd, NULL) != 0) {
        perror("[x] Epoll_remove");
        return -1;
    }
    return 0;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);

    int svr = Listen("1234", 5);
    if (svr < 0) {
        return -1;
    }

    int ep = epoll_create(1);
    if (ep < 0) {
        perror("[x] epoll_create");
        return -1;
    }

    if (Epoll_add(ep, svr) != 0) {
        return -1;
    }

    struct epoll_event events[3];
    for (;;) {
        int n = epoll_wait(ep, events, 3, -1);
        if (n < 0) {
            perror("[x] epoll_wait");
            return -1;
        }

        for (int i = 0; i < n; ++i) {
            const struct epoll_event* e = &(events[i]);
            if ((e->events & EPOLLIN) == 0) {
                printf("[!] unexpected event: %d\n", e->events);
                continue;
            }

            if (e->data.fd == svr) {
				int clt = Accept(svr);
				if (clt < 0)
					continue;

                const char* hello = "hello fucking world!\r\n";
                send(clt, hello, strlen(hello), 0);

                if (Epoll_add(ep, clt) != 0) {
                    return -1;
                }
            }
            else {
				char buf[64];
                int clt = e->data.fd;
				if (read(clt, buf, sizeof(buf)) < 0) {
                    printf("[!] disconnected: %d\n", clt);
                    Epoll_remove(ep, clt);
					continue;
				}
				int i = atoi(buf);
				sprintf(buf, "%d\r\n", i + 1);
				if (send(clt, buf, strlen(buf), 0) < 0) {
					printf("[!] disconnected: %d\n", clt);
                    Epoll_remove(ep, clt);
					continue;
				}
            }
        }
    }
    return 0;
}