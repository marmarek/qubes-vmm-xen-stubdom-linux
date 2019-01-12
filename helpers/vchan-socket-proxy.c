/**
 * @file
 * @section AUTHORS
 *
 * Copyright (C) 2010  Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *
 *  Authors:
 *       Rafal Wojtczuk  <rafal@invisiblethingslab.com>
 *       Daniel De Graaf <dgdegra@tycho.nsa.gov>
 *       Marek Marczykowski-Górecki  <marmarek@invisiblethingslab.com>
 *
 * @section LICENSE
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; If not, see <http://www.gnu.org/licenses/>.
 *
 * @section DESCRIPTION
 *
 * This is a vchan to unix socket proxy. Vchan server is set, and on client
 * connection, local socket connection is established. Communication is bidirectional.
 * One client is served at a time, clients needs to coordinate this themselves.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <libxenvchan.h>

static void usage(char** argv)
{
    fprintf(stderr, "usage:\n"
        "\t%s domainid nodepath socket-path [-v]\n",
        argv[0]);
    exit(1);
}

#define BUFSIZE 8192
char inbuf[BUFSIZE];
char outbuf[BUFSIZE];
int insiz = 0;
int outsiz = 0;
int socket_fd = -1;
struct libxenvchan *ctrl = 0;

static void vchan_wr(void) {
    int ret;

    if (!insiz)
        return;
    ret = libxenvchan_write(ctrl, inbuf, insiz);
    if (ret < 0) {
        fprintf(stderr, "vchan write failed\n");
        exit(1);
    }
    fprintf(stderr, "written %d bytes to vchan\n", ret);
    if (ret > 0) {
        insiz -= ret;
        memmove(inbuf, inbuf + ret, insiz);
    }
}

static void socket_wr(void) {
    int ret;

    if (!outsiz)
        return;
    ret = write(socket_fd, outbuf, outsiz);
    if (ret < 0 && errno != EAGAIN)
        exit(1);
    if (ret > 0) {
        outsiz -= ret;
        memmove(outbuf, outbuf + ret, outsiz);
    }
}

static int set_nonblocking(int fd, int nonblocking) {
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        return -1;

    if (nonblocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1)
        return -1;

    return 0;
}

static int connect_socket(const char *path) {
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path));
    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }

    set_nonblocking(fd, 1);

    return fd;
}

static void discard_buffers(void) {
    /* discard local buffers */
    insiz = 0;
    outsiz = 0;

    /* discard remaining incoming data */
    while (libxenvchan_data_ready(ctrl)) {
        if (libxenvchan_read(ctrl, inbuf, BUFSIZE) == -1) {
            perror("vchan write");
            exit(1);
        }
    }
}

/**
    Simple libxenvchan application, both client and server.
    Both sides may write and read, both from the libxenvchan and from
    stdin/stdout (just like netcat).
*/

int main(int argc, char **argv)
{
    int ret;
    int libxenvchan_fd;
    int max_fd;
    int verbose = 0;
    if (argc < 4)
        usage(argv);

    if (argc >= 5 && strcmp(argv[4], "-v") == 0)
        verbose = 1;

    ctrl = libxenvchan_server_init(NULL, atoi(argv[1]), argv[2], 0, 0);
    if (!ctrl) {
        perror("libxenvchan_server_init");
        exit(1);
    }

    libxenvchan_fd = libxenvchan_fd_for_select(ctrl);
    for (;;) {
        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        if (socket_fd != -1) {
            if (insiz != BUFSIZE)
                FD_SET(socket_fd, &rfds);
            if (outsiz)
                FD_SET(socket_fd, &wfds);
        }
        FD_SET(libxenvchan_fd, &rfds);
        max_fd =  libxenvchan_fd > socket_fd ? libxenvchan_fd : socket_fd;
        ret = select(max_fd + 1, &rfds, &wfds, NULL, NULL);
        if (ret < 0) {
            perror("select");
            exit(1);
        }
        if (FD_ISSET(libxenvchan_fd, &rfds)) {
            libxenvchan_wait(ctrl);
            if (socket_fd == -1) {
                if (libxenvchan_is_open(ctrl) == 1) {
                    if (verbose)
                        fprintf(stderr, "vchan client connected, connecting to %s\n",
                                argv[3]);
                    socket_fd = connect_socket(argv[3]);
                    if (socket_fd == -1) {
                        perror("socket connect");
                        exit(1);
                    }
                    if (verbose)
                        fprintf(stderr, "connected to %s\n", argv[3]);
                }
                continue;
            } else {
                if (!libxenvchan_is_open(ctrl)) {
                    if (verbose)
                        fprintf(stderr, "vchan client disconnected\n");
                    while (outsiz)
                        socket_wr();
                    close(socket_fd);
                    socket_fd = -1;
                    discard_buffers();
                    continue;
                }
            }
            vchan_wr();
        }

        /* socket_fd guaranteed to be != -1 */

        if (FD_ISSET(socket_fd, &rfds)) {
            ret = read(socket_fd, inbuf + insiz, BUFSIZE - insiz);
            if (ret < 0 && errno != EAGAIN)
                exit(1);
            if (verbose)
                fprintf(stderr, "from-unix: %.*s\n", ret, inbuf + insiz);
            if (ret == 0) {
                /* EOF on socket, write everything in the buffer and close the
                 * socket */
                while (insiz) {
                    vchan_wr();
                    libxenvchan_wait(ctrl);
                }
                close(socket_fd);
                socket_fd = -1;
                /* TODO: maybe signal the vchan client somehow? */
                continue;
            }
            if (ret)
                insiz += ret;
            vchan_wr();
        }
        if (FD_ISSET(socket_fd, &wfds))
            socket_wr();
        while (libxenvchan_data_ready(ctrl) && outsiz < BUFSIZE) {
            ret = libxenvchan_read(ctrl, outbuf + outsiz, BUFSIZE - outsiz);
            if (ret < 0)
                exit(1);
            if (verbose)
                fprintf(stderr, "from-vchan: %.*s\n", ret, outbuf + outsiz);
            outsiz += ret;
            socket_wr();
        }
    }
}
