#include <stdio.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

void usage(char *arg0) {
    fprintf(stderr, "Usage: %s <socket> <file-to-add> <message> [<read-until>]\n",
            arg0);
    fprintf(stderr, "\n");
    fprintf(stderr, "Open <file-to-add> and sends it to <socket> using SCM_RIGHTS.\n");
    fprintf(stderr, "Use /dev/fd/N as <file-to-add> to send already open FD.\n");
    fprintf(stderr, "If <read-until> is provided, data is read from the socket,\n"
                    "until <read-until> or EOF is received; otherwise it's\n"
                    "closed immediately after sending the message.\n");
}


int main(int argc, char **argv) {
    int socket_fd, fd;
    struct sockaddr_un addr;
    struct msghdr msg = { 0 };
    struct iovec iov;
    struct cmsghdr *cmsg;
    char control[CMSG_SPACE(sizeof(int))];
    size_t datalen;

    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
        err(1, "socket");

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path));

    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        err(1, "connect");

    if (sscanf(argv[2], "/dev/fd/%d", &fd) != 1)
        fd = open(argv[2], O_RDWR);

    if (fd == -1)
        err(1, "open %s", argv[2]);

    datalen = strlen(argv[3]);
    iov.iov_base = (void*)argv[3];
    iov.iov_len  = datalen;

    /* compose the message */
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    /* attach open fd */
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    msg.msg_controllen = cmsg->cmsg_len;

    if (sendmsg(socket_fd, &msg, 0) != datalen)
        err(1, "sendmsg");

    if (argc >= 5) {
        char buf[4097];
        int len;

        for (;;) {
            len = read(socket_fd, buf, sizeof(buf)-1);
            if (!len)
                break;
            else if (len == -1)
                err(1, "read from socket");
            buf[len] = 0;
            write(1, buf, len);
            if (strstr(buf, argv[4]))
                break;
        }
    }

    return 0;
}
