/* ===================================================
 * Copyright (C) 2018 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: log.h
 *     Created: 2018-01-02 16:13
 * Description:
 * ===================================================
 */
#ifndef _LOG_H
#define _LOG_H

#include <stdio.h>

void log_init(FILE* fp);
void log_exit();
FILE* get_log_fp();

#define LOG_INFO(fmt, ...) do { \
    fprintf (get_log_fp(), "LOG_INFO [%s:%s:%d] " fmt, __FILE__, __func__, __LINE__, ## __VA_ARGS__); \
} while (0)

#define LOG_WARN(fmt, ...) do { \
    fprintf (get_log_fp(), "LOG_WARN [%s:%s:%d] " fmt, __FILE__, __func__, __LINE__, ## __VA_ARGS__); \
} while (0)

#define LOG_ERROR(fmt, ...) do { \
    fprintf (get_log_fp(), "LOG_ERROR [%s:%s:%d] " fmt, __FILE__, __func__, __LINE__, ## __VA_ARGS__); \
} while (0)

#ifdef DEBUG
#define LOG_DEBUG(fmt, ...) do { \
    fprintf (get_log_fp(), "LOG_DEBUG [%s:%s:%d] " fmt, __FILE__, __func__, __LINE__, ## __VA_ARGS__); \
} while (0)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#endif
