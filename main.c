/*
- 发送大文件问题
- Content-type获取
- 压缩
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <zlib.h>

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

int OnNewConnect(int ep, int svr) {
	int clt = Accept(svr);
    if (clt < 0)
        return -1;
    return Epoll_add(ep, clt);
}

void Response(int ep, int clt, const char* version, int code, const char* reason, const char *type, const char* body, long bodysize) {
    long bufsize = 256 + bodysize;
    char* buf = (char*)malloc(bufsize);
    memset(buf, 0, bufsize);

    sprintf(buf, "%s %d %s\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n%s", version, code, reason, bodysize, type, bodysize == 0 ? "" : body);

    long left = strlen(buf);
    const char* current = buf;
    while (left > 0) {
        long n = send(clt, current, left, 0);
        printf("===> %ld, %ld\n", left, n);
        if (n < 0) {
			printf("[!] disconnected: %d\n", clt);
			Epoll_remove(ep, clt);
			return;
        }
        left -= n;
        current += n;
    }
    free(buf);
    buf = NULL;
    close(clt);
}

const char* GetUriType(const char* uri) {
    static const char* types[] = {
        "text/html",
        "image/png",
    };
    const char* begin = strrchr(uri, '.');
    char postfix[16];
    strcpy(postfix, begin + 1);
    int type = 0;
    if (strcmp(postfix, "png") == 0) {
        type = 1;
    }
    return types[type];
}

int gzCompress(const char* src, int srcLen, char* dest, int destLen)
{
    z_stream c_stream;
    int err = 0;
    int windowBits = 15;
    int GZIP_ENCODING = 16;

    if (src && srcLen > 0)
    {
        c_stream.zalloc = (alloc_func)0;
        c_stream.zfree = (free_func)0;
        c_stream.opaque = (voidpf)0;
        if (deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
            windowBits | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY) != Z_OK) return -1;
        c_stream.next_in = (Bytef*)src;
        c_stream.avail_in = srcLen;
        c_stream.next_out = (Bytef*)dest;
        c_stream.avail_out = destLen;
        while (c_stream.avail_in != 0 && c_stream.total_out < destLen)
        {
            if (deflate(&c_stream, Z_NO_FLUSH) != Z_OK) return -1;
        }
        if (c_stream.avail_in != 0) return c_stream.avail_in;
        for (;;) {
            if ((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END) break;
            if (err != Z_OK) return -1;
        }
        if (deflateEnd(&c_stream) != Z_OK) return -1;
        return c_stream.total_out;
    }
    return -1;
}

int data_compress(const char* idata, int ilen, char* odata, int* olen)
{
    z_stream z = { 0 };

    z.next_in = (Bytef*)idata;
    z.avail_in = ilen;
    z.next_out = (Bytef*)odata;
    z.avail_out = *olen;

    printf("total %u bytes\n", z.avail_in);

    /* 使用最高压缩比 */
    if (deflateInit(&z, Z_BEST_COMPRESSION) != Z_OK) {
        printf("deflateInit failed!\n");
        return -1;
    }

    if (deflate(&z, Z_NO_FLUSH) != Z_OK) {
        printf("deflate Z_NO_FLUSH failed!\n");
        return -1;
    }

    if (deflate(&z, Z_FINISH) != Z_STREAM_END) {
        printf("deflate Z_FINISH failed!\n");
        return -1;
    }

    if (deflateEnd(&z) != Z_OK) {
        printf("deflateEnd failed!\n");
        return -1;
    }

    *olen = *olen - z.avail_out;

    printf("compressed data %d bytes.\n", *olen);

    return 0;
}

void ProcessGet(int ep, int clt, const char *uri, const char *version, const char *type, int gzip) {
    char* buf = NULL;
    long bufsize = 0;
    int code = 404;
    const char* reason = "are you fucking hacker?!!";
    char path[1024];
    sprintf(path, "/home/outsky/webc%s", uri);
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        code = 200;
        reason = "OK";
        fseek(f, 0, SEEK_END);
        bufsize = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = (char*)malloc(bufsize);
        long size = fread(buf, 1, bufsize, f);
        if (size != bufsize) {
            printf("[!] fread size not equal: %ld, %ld\n", bufsize, size);
            bufsize = size;
        }

        if (gzip != 0) {
            char* dest = (char*)malloc(bufsize * 2);
            //bufsize = gzCompress(buf, bufsize, dest, bufsize * 2);
            data_compress(buf, bufsize, dest, (int*)&bufsize);
            if (bufsize < 0) {
                printf("[!] gzCompress %s failed!\n", path);
            }
            free(buf);
            buf = dest;
        }
    }
    else {
        printf("<%s>\n", path);
        perror("[x] fopen");
    }

    //const char* type = GetUriType(uri);
    Response(ep, clt, version, code, reason, type, buf, bufsize);
    if (buf != NULL) {
        free(buf);
        buf = NULL;
    }
}

void ProcessMsg(int ep, int clt, const char* buf, long len) {
    char method[16], uri[256], version[16];
    char* parts[3] = { method, uri, version };
    const char* begin = buf;
    for (int i = 0; i < 3; ++i) {
        const char* end = strpbrk(begin, " \r");
        strncpy(parts[i], begin, end - begin);
        parts[i][end - begin] = '\0';
        begin = end + 1;
    }

    char type[32] = "text/html";
    begin = strstr(begin, "Accept: ");
    if (begin != NULL) {
        begin += 8;
        const char *end = strpbrk(begin, ",\r");
        if (end != NULL) {
            strncpy(type, begin, end - begin);
            type[end - begin] = '\0';
        }
    }

    int gzip = strstr(buf, "gzip") != NULL;

    if (strcmp(method, "GET") == 0) {
        ProcessGet(ep, clt, uri, version, type, gzip);
    }
    else {
        printf("[x] unsupported method: %s\n", method);
    }
}

void OnMsg(int ep, int clt) {
	char buf[1024];
    long len = recv(clt, buf, sizeof(buf), 0);
	if (len <= 0) {
		printf("[!] disconnected: %d\n", clt);
		Epoll_remove(ep, clt);
        return;
	}
    buf[len] = '\0';

    printf("[%d] -> %s(%ld)\n", clt, buf, len);

    ProcessMsg(ep, clt, buf, len);
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
                OnNewConnect(ep, e->data.fd);
            }
            else {
                OnMsg(ep, e->data.fd);
            }
        }
    }
    return 0;
}