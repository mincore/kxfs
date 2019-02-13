/* ===================================================
 * Copyright (C) 2019 chenshuangping All Right Reserved.
 *      Author: mincore@163.com
 *    Filename: crc16.h
 *     Created: 2019-02-13 05:43
 * Description:
 * ===================================================
 */
#ifndef _CRC16_H
#define _CRC16_H

#include <stdlib.h>
#include <stdint.h>

uint16_t crc16(uint16_t crc, const uint8_t *buffer, size_t len);

#endif
