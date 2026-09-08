/*
 * Copyright (c) 2001-2026 Territorium Online Srl / TOL GmbH. All Rights Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code as defined in and that are
 * subject to the Territorium Online License Version 1.0. You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at http://www.tol.info/license/
 * and read it before using this file.
 *
 * The Original Code and all software distributed under the License are distributed on an 'AS IS'
 * basis, WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, AND TERRITORIUM ONLINE HEREBY
 * DISCLAIMS ALL SUCH WARRANTIES, INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. Please see the License for
 * the specific language governing rights and limitations under the License.
 */
#ifndef MAPGEOPACKAGE_H
#define MAPGEOPACKAGE_H

#include <stdint.h>
#include "mapserver.h"

typedef struct stream_struct {
  uint8_t byte_order;
  unsigned char *data;
} stream_t;

/**
 * @brief Clones a stream of chars of predefined length
 *
 * @param stream
 * @param length
 * @return
 */
char *stream_clone(const unsigned char *stream, uint64_t length);

/**
 * Import an UINT-8 value in endian-aware fashion
 *
 * you are expected to pass an input buffer corresponding to an
 * allocation size of (at least) 1 byte.
 *
 * @param stream endian-dependent representation (input buffer).
 * @return the internal UINT value
 */
uint8_t stream_get_uint8(stream_t *stream);

/**
 * Import an UINT-32 value in endian-aware fashion
 *
 * you are expected to pass an input buffer corresponding to an
 * allocation size of (at least) 4 bytes.
 *
 * @param stream endian-dependent representation (input buffer).
 * @return the internal UINT value
 */
uint32_t stream_get_uint32(stream_t *stream);

/**
 * Import an DOUBLE-64 in endian-aware fashion
 *
 * you are expected to pass an input buffer corresponding to an
 * allocation size of (at least) 8 bytes.
 *
 * @param stream endian-dependent representation (input buffer).
 * @return the internal DOUBLE value
 */
double stream_get_double(stream_t *stream);

/**
 * @brief Reads the geometry from geopackage fromat and writes it to the shape
 * object.
 * @param shape
 * @param data
 * @param n_bytes
 * @return
 */
int readShape(shapeObj *shape, const unsigned char *data, int n_bytes);

#endif // MAPGEOPACKAGE_H
