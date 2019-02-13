/* ===================================================
 * Copyright (C) 2017 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: net.h
 *     Created: 2017-10-16 15:45
 * Description:
 * ===================================================
 */
#ifndef _NET_H
#define _NET_H

extern "C" {
int netlisten(int istcp, const char *server, int port);
int netaccept(int fd, char *server, int *port);
int netconnect(int istcp, const char *server, int port);
int netnonblock(int fd);
int netkeepalive(int fd);
}

#endif
