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
#include "mapgeopackage.h"

/* GNU needs this for strcasestr */
#define _GNU_SOURCE

#include <string.h>
#include <stdlib.h>


#define GPKG_MAGIC1  0x47
#define GPKG_MAGIC2  0x50
#define GPKG_VERSION 0x00

#define GPKG_HEADER_LEN   8
#define GPKG_DEFAULT_SRID 0

#define GPKG_FLAG_LITTLEENDIAN      0x01
#define GPKG_FLAG_ENVELOPE          0x0F
#define GPKG_FLAG_EMPTY_GEOMETRY    0x10
#define GPKG_FLAG_HAS_EXTENSION     0x20



#define WKB_GEOMETRY            0
#define WKB_POINT               1
#define WKB_LINESTRING          2
#define WKB_POLYGON             3
#define WKB_MULTIPOINT          4
#define WKB_MULTILINESTRING     5
#define WKB_MULTIPOLYGON        6
#define WKB_GEOMETRYCOLLECTION  7




static int8_t arch_is_little_endian = -1;

static uint8_t isArchLittleEndian()
{
    if(arch_is_little_endian < 0) {
        // checking if target CPU is a little-endian one
        union endian {
            unsigned char byte[4];
            int int_value;
        } convert;
        convert.int_value = 1;
        arch_is_little_endian = (convert.byte[0] == 0) ? 0 : 1;
    }

    return arch_is_little_endian;
}


static int isArchEndian(uint8_t little_endian)
{
    return (isArchLittleEndian() == little_endian) ? 1 : 0;
}


static void string_trim(char *str)
{
    int i;

    // Send nulls home without supper.
    if(!str) return;

    // Move non-white string to the front.
    i = strspn(str, " ");
    if(i) {
        memmove(str, str + i, strlen(str) - i + 1);
    }
    // Nothing left? Exit.
    if(strlen(str) == 0) {
        return;
    }
    // Null-terminate end of non-white string.
    for(i=strlen(str)-1; i>=0; i--) { // step backwards from end
        if(str[i] != ' ') {
            str[i+1] = '\0';
            return;
        }
    }
    return;
}


char * stream_clone(const unsigned char * stream, uint64_t length)
{
    char * text = (char*) malloc(length + 1);
    strlcpy(text, stream, length + 1);
    string_trim(text);
    return text;
}


uint8_t stream_get_uint8(stream_t * stream)
{
    return *(stream->data++);
}


uint32_t stream_get_uint32(stream_t * stream)
{
    // fetches a 32bit uint from BLOB respecting declared endiannes
    union endian {
        uint8_t byte[4];
        uint32_t int_value;
    } convert;

    // Litte-Endian architecture [e.g. x86, PPC]
    if (isArchEndian(stream->byte_order)) {
        convert.byte[0] = stream_get_uint8(stream);
        convert.byte[1] = stream_get_uint8(stream);
        convert.byte[2] = stream_get_uint8(stream);
        convert.byte[3] = stream_get_uint8(stream);
    } else {
        convert.byte[3] = stream_get_uint8(stream);
        convert.byte[2] = stream_get_uint8(stream);
        convert.byte[1] = stream_get_uint8(stream);
        convert.byte[0] = stream_get_uint8(stream);
    }

    return convert.int_value;
}


double stream_get_double(stream_t * stream)
{
    // fetches a 64bit double from BLOB respecting declared endiannes
    union endian {
        uint8_t byte[8];
        double double_value;
    } convert;

    // Litte-Endian architecture [e.g. x86, PPC]
    if (isArchEndian(stream->byte_order)) {
        convert.byte[0] = stream_get_uint8(stream);
        convert.byte[1] = stream_get_uint8(stream);
        convert.byte[2] = stream_get_uint8(stream);
        convert.byte[3] = stream_get_uint8(stream);
        convert.byte[4] = stream_get_uint8(stream);
        convert.byte[5] = stream_get_uint8(stream);
        convert.byte[6] = stream_get_uint8(stream);
        convert.byte[7] = stream_get_uint8(stream);
    } else {
        convert.byte[7] = stream_get_uint8(stream);
        convert.byte[6] = stream_get_uint8(stream);
        convert.byte[5] = stream_get_uint8(stream);
        convert.byte[4] = stream_get_uint8(stream);
        convert.byte[3] = stream_get_uint8(stream);
        convert.byte[2] = stream_get_uint8(stream);
        convert.byte[1] = stream_get_uint8(stream);
        convert.byte[0] = stream_get_uint8(stream);
    }

    return convert.double_value;
}



static pointObj parsePoint(stream_t * stream, int dimension)
{
    pointObj point;
    point.x = stream_get_double(stream);
    point.y = stream_get_double(stream);

    if(dimension > 2) // z or m value
        stream_get_double(stream);
    if(dimension > 3) // m value
        stream_get_double(stream);

    return point;
}


static void readPoint(shapeObj * shape, stream_t * stream, int dimension)
{
    lineObj * line = msSmallMalloc(sizeof(lineObj));
    line->numpoints = 0;
    line->point = 0;

    pointObj point = parsePoint(stream, dimension);
    msAddPointToLine(line, &point);

    msAddLine(shape, line);
    free(line->point);
    free(line);
}


static void readPoints(shapeObj * shape, stream_t * stream, int dimension)
{
    uint32_t numCoords = stream_get_uint32(stream);

    if (numCoords) {
        lineObj * line = msSmallMalloc(sizeof(lineObj));
        line->numpoints = 0;
        line->point = 0;

        uint32_t index;
        for (index = 0; index < numCoords; index++) {
            pointObj point = parsePoint(stream, dimension);
            msAddPointToLine(line, &point);
        }

        msAddLine(shape, line);
        free(line->point);
        free(line);
    }
}


static void readPolygon(shapeObj * shape, stream_t * stream, int dimension)
{
    uint32_t numRings = stream_get_uint32(stream);

    uint32_t index ;
    for (index = 0; index < numRings; index++) {
        readPoints(shape, stream, dimension);
    }
}


static void readGeometrie(shapeObj * shape, stream_t * stream, uint8_t dimension)
{
    stream->byte_order = stream_get_uint8(stream);
    uint32_t type = stream_get_uint32(stream);

    uint8_t geomType = type % 1000;
    dimension = 2 + (type - geomType) / 1000;

    switch (geomType) {
        case WKB_POINT:
            shape->type = MS_SHAPE_POINT;
            readPoint(shape, stream, dimension);
            break;

        case WKB_LINESTRING:
            shape->type = MS_SHAPE_LINE;
            readPoints(shape, stream, dimension);
            break;

        case WKB_POLYGON:
            shape->type = MS_SHAPE_POLYGON;
            readPolygon(shape, stream, dimension);
            break;
    }
}


static void readGeometries(shapeObj * shape, stream_t * stream, int dimension)
{
    uint32_t numGeoms = stream_get_uint32(stream);

    uint32_t index;
    for (index = 0; index < numGeoms; index++) {
        readGeometrie(shape, stream, dimension);
    }
}


/**
 * @brief Reads the shapeObj form the geopackage geometry.
 * @param shape
 * @param data
 * @param n_bytes
 * @return
 */
static int readWKB(shapeObj * shape, stream_t * stream) {
    stream->byte_order = stream_get_uint8(stream);

    uint32_t type = stream_get_uint32(stream);
    uint8_t geomType = type % 1000;
    uint8_t dimension = 2 + (type - geomType) / 1000;

    switch (geomType) {
        case WKB_POINT:
            shape->type = MS_SHAPE_POINT;
            readPoint(shape, stream, dimension);
            break;

        case WKB_LINESTRING:
            shape->type = MS_SHAPE_LINE;
            readPoints(shape, stream, dimension);
            break;

        case WKB_POLYGON:
            shape->type = MS_SHAPE_POLYGON;
            readPolygon(shape, stream, dimension);
            break;

        case WKB_MULTIPOINT:
            shape->type = MS_SHAPE_POINT;
            readGeometries(shape, stream, dimension);
            break;

        case WKB_MULTILINESTRING:
            shape->type = MS_SHAPE_LINE;
            readGeometries(shape, stream, dimension);
            break;

        case WKB_MULTIPOLYGON:
            shape->type = MS_SHAPE_POLYGON;
            readGeometries(shape, stream, dimension);
            break;
    }

    return MS_SUCCESS;
}


/**
 * @brief Reads the shapeObj form the geopackage geometry.
 * @param shape
 * @param data
 * @param n_bytes
 * @return
 */
int readShape(shapeObj * shape, const unsigned char * data, int n_bytes)
{
    stream_t stream_data;
    stream_data.data = data;
    stream_t * stream = &stream_data;

    uint8_t magic1 = stream_get_uint8(stream);
    uint8_t magic2 = stream_get_uint8(stream);
    uint8_t version = stream_get_uint8(stream);
    uint8_t flags = stream_get_uint8(stream);
    uint32_t srid = stream_get_uint32(stream);

    if (n_bytes <= GPKG_HEADER_LEN) {
        msSetError(MS_MEMERR, "Geopackage Header", "header length is too short!");
    }

    if ((magic1 != GPKG_MAGIC1) || (magic2 != GPKG_MAGIC2)) {
        msSetError(MS_MEMERR, "Geopackage Header", "magic value is incorrect!");
    }

    if (version == GPKG_VERSION) {
        msSetError(MS_MEMERR, "Geopackage Header", "version is not supported!");
    }

    stream_data.byte_order = flags & GPKG_FLAG_LITTLEENDIAN;

    int envelope_code = ((flags >> 1) & 0x07);
    if (flags & GPKG_FLAG_HAS_EXTENSION) {
        msSetError(MS_MEMERR, "Geopackage Header", "unsupported geopackage binary type (extended geopackage binary)\n");
    }

    if(envelope_code > 0) {
        stream_get_double(stream);
        stream_get_double(stream);
        stream_get_double(stream);
        stream_get_double(stream);
    }
    if(envelope_code > 1) {
        stream_get_double(stream);
        stream_get_double(stream);
    }
    if(envelope_code > 3) {
        stream_get_double(stream);
        stream_get_double(stream);
    }

    readWKB(shape, stream);
    msComputeBounds(shape);

    return 1;
}
