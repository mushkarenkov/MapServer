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

/* GNU needs this for strcasestr */
#define _GNU_SOURCE

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "mapserver.h"

#include "mapgeopackage.h"

#include <item_api_c.h>

// Specific information needed for managing this layer.
typedef struct oqlLayerInfo {
    ItemContext context;
    ItemResult result;

    long index;

    // aux
    char * from;
    char * geom;
    char * attributes;
    char * where;
    char * oql;
} oqlLayerInfo;

// const void * oqlContext = NULL;

// /**
//  * @brief msOqlCloseConnection
//  *
//  * Handler registered with msConnPoolRegister so that Mapserver
//  * can clean up open connections during a shutdown.
//  *
//  * @param conn
//  */
// void msOqlCloseConnection(void * conn)
// {
//     //    torm_close_connection(conn);
// }

char * msOqlBuildOqlFrom(layerObj * layer)
{

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;

    if (!layer->data) {
        // layerinfo->from = NULL;
        msSetError(MS_MISCERR, "layer->data is not set.", "msOqlBuildOqlFrom()");
        return NULL;
    }

    layerinfo->from = stream_clone(layer->data, strlen(layer->data));
    return layerinfo->from;
}

int msOqlReadShapeItems(layerObj * layer, shapeObj * shape, ItemResult * result)
{
    shape->values = (char **)msSmallMalloc(sizeof(char *) * layer->numitems);

    for (int i = 0; i < layer->numitems; i++) {
        // Iterate the columns
        shape->values[i] = oqlGetValueAsChar(result, i + 1);
    }
    shape->numvalues = layer->numitems;

    return MS_SUCCESS;
}

char * msOqlBuildOqlAttributes(layerObj * layer)
{
    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;

    //    if(layer->debug > 1) {
    //        msDebug("msOqlBuildSQLItems: %d items requested.\n", layer->numitems);
    //    }

    // Geom attribute
    layerinfo->geom = oqlGeometryAttribute(layerinfo->from, layerinfo->context);
    if (layerinfo->geom == NULL) {
        msSetError(MS_MISCERR, "Geometry attribute is not set", "msOqlBuildOqlAttributes");
        return NULL;
    }

    static char * gg = " AS geom";
    char * geom = (char *)msSmallMalloc(strlen(layerinfo->geom) + strlen(gg) + 1); // +1 for the null terminator
    strcpy(geom, layerinfo->geom);
    strcat(geom, gg);

    // Other attributes
    if (layer->numitems == 0) {
        layerinfo->attributes = geom;
    } else {
        int length = strlen(geom) + 1;

        for (int i = 0; i < layer->numitems; i++) {
            length += strlen(layer->items[i]) + 2; // +2 for ', '
        }

        layerinfo->attributes = (char *)msSmallMalloc(length);
        strcpy(layerinfo->attributes, geom);

        for (int i = 0; i < layer->numitems; ++i) {
            strcat(layerinfo->attributes, ", ");
            strcat(layerinfo->attributes, layer->items[i]);
        }

        msFree(geom);
    }

    return layerinfo->attributes;
}

char * msOqlBuildOqlWhere(layerObj * layer, rectObj * rect)
{
    const static char * where = "WHERE ";
    const static int strlenWhere = 6;

    const static char * andFilter = " AND ";
    const static int strlenAnd = 5;

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;

    char * bbox = oqlBbox(layerinfo->geom, &rect->minx, &rect->miny, &rect->maxx, &rect->maxy);
    int lengthProcessing = strlenWhere + strlen(bbox);

    int numfilters = 0;
    char ** filters = msSmallMalloc(layer->numprocessing * sizeof(char*));
    const char * key = "NATIVE_FILTER";
    int len = strlen(key);
    for (int i = 0; i < layer->numprocessing; ++i) {
        if (strncasecmp(layer->processing[i], key, len) == 0 && layer->processing[i][len] == '=') {
            const char * filter = layer->processing[i] + len + 1;
            filters[numfilters++] = filter;
            lengthProcessing += strlenAnd + strlen(filter);
        }
    }

    layerinfo->where = msSmallMalloc(lengthProcessing + 1); // +1 for the null terminator
    strcpy(layerinfo->where, where);
    strcat(layerinfo->where, bbox);
    msFree(bbox);

    for (int i = 0; i < numfilters; ++i) {
        strcat(layerinfo->where, andFilter);
        strcat(layerinfo->where, filters[i]);
    }

    msFree(filters);

    return layerinfo->where;
}

/**
 * @brief Returns malloc'ed char* that must be freed by caller.
 * @param layer
 * @param rect
 * @param uid
 */
char * msOqlBuildOql(layerObj * layer, rectObj * rect)
{
    static const char * oqlTemplate = "SELECT %s FROM %s %s";

    //    if (layer->debug) {
    //        msDebug("msOqlBuildSQL called.\n");
    //    }
    //    assert(layer->layerinfo != NULL);

    if (!msOqlBuildOqlFrom(layer)) {
        msSetError(MS_MISCERR, "Failed to biuld oql from", "msOqlBuildOql()");
        return NULL;
    }

    if (!msOqlBuildOqlAttributes(layer)) {
        msSetError(MS_MISCERR, "Failed to build oql select list", "msOqlBuildOql()");
        return NULL;
    }

    if (!msOqlBuildOqlWhere(layer, rect)) {
        msSetError(MS_MISCERR, "Failed to build oql where", "msOqlBuildOql()");
        return NULL;
    }

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;
    layerinfo->oql = msSmallMalloc(strlen(oqlTemplate) + strlen(layerinfo->from) + strlen(layerinfo->attributes) + strlen(layerinfo->where) + 1);
    sprintf(layerinfo->oql, oqlTemplate, layerinfo->attributes, layerinfo->from, layerinfo->where);

    return layerinfo->oql;
}

/**
 * @brief Checks if the connection is already open
 *
 * @param layer
 */
int msOqlLayerIsOpen(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerIsOpen called by layer %s.\n", layer->name);
    }

    return layer->layerinfo ? MS_TRUE : MS_FALSE;
}

/*
 * Registered vtable->LayerOpen function.
 */
int msOqlLayerOpen(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerOpen called by layer %s.\n", layer->name);
    }

    if (!layer->layerinfo) {
        msSetError(MS_MISCERR, "Uninitialized layer info data", "msOqlLayerOpen()");
        return MS_FAILURE;
    }

    return MS_SUCCESS;
}

void msOqlFreeLayerInfo(oqlLayerInfo * layerinfo)
{
    if (!layerinfo) {
        return;
    }

    if (layerinfo->result)
        oqlFreeResult(layerinfo->result);

    if (layerinfo->attributes)
        msFree(layerinfo->attributes);

    if (layerinfo->from)
        msFree(layerinfo->from);

    if (layerinfo->geom)
        msFree(layerinfo->geom);

    if (layerinfo->oql)
        msFree(layerinfo->oql);

    if (layerinfo->where)
        msFree(layerinfo->where);

    msFree(layerinfo);
}

void msOqlResetLayerInfo(layerObj * layer, void * context)
{
    assert(layer != NULL);

    if (layer->layerinfo) {
        msOqlFreeLayerInfo(layer->layerinfo);
    }

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)msSmallMalloc(sizeof(oqlLayerInfo));
    layerinfo->attributes = NULL;
    layerinfo->context = context;
    layerinfo->from = NULL;
    layerinfo->geom = NULL;
    layerinfo->index = 0;
    layerinfo->oql = NULL;
    layerinfo->result = NULL;
    layerinfo->where = NULL;

    layer->layerinfo = (void *)layerinfo;
}

/*
 * Registered vtable->LayerClose function.
 */
int msOqlLayerClose(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerClose called by layer %s.\n", layer->name);
    }

    msOqlFreeLayerInfo(layer->layerinfo);
    layer->layerinfo = NULL;

    return MS_SUCCESS;
}

/*
 * Registered vtable->LayerWhichShapes function.
 */
int msOqlLayerWhichShapes(layerObj * layer, rectObj rect, int isQuery)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerWhichShapes called by layer %s(%f, %f, %f, %f).\n", layer->name, rect.minx, rect.miny, rect.maxx, rect.maxy);
    }

    assert(layer->layerinfo != NULL);

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;
    layerinfo->index = 0;

    char * oql = msOqlBuildOql(layer, &rect);

    if (oql == NULL) {
        return MS_FAILURE;
    }

    layerinfo->result = oqlExec(oql, layerinfo->context);

    if (layerinfo->result == NULL) {
        return MS_FAILURE;
    }

    return MS_SUCCESS;
}

bool msIsShapeTypeCompatible(enum MS_LAYER_TYPE ltype, enum MS_SHAPE_TYPE stype)
{
    switch (stype) {
    case MS_SHAPE_POINT:
        return ltype == MS_LAYER_POINT || ltype == MS_LAYER_CIRCLE;
    case MS_SHAPE_LINE:
        return ltype == MS_LAYER_POINT || ltype == MS_LAYER_LINE;
    case MS_SHAPE_POLYGON:
        return true;
    case MS_SHAPE_NULL:
    default:
        return false;
    }
}

/*
 * Registered vtable->LayerNextShape function.
 */
int msOqlLayerNextShape(layerObj * layer, shapeObj * shape)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerNextShape called by layer %s.\n", layer->name);
    }

    assert(layer->layerinfo != NULL);

    oqlLayerInfo * layerinfo = (oqlLayerInfo *)layer->layerinfo;

    ItemResult result = layerinfo->result;

    if (oqlNextResult(result)) {
        int geomIndex = 0; // TODO Should be done better
        ByteArray ba;
        oqlGetGeomAsBytes(result, geomIndex, &ba);

        if (readShape(shape, ba._data, ba._size)) {
            if (msIsShapeTypeCompatible(layer->type, shape->type)) {
                shape->index = layerinfo->index++;
                msOqlReadShapeItems(layer, shape, result);
            } else {
                msFreeShape(shape);
                msSetError(
                    MS_TYPEERR,
                    "Geometry type is incompatible with layer type",
                    "msOqlLayerNextShape()");
            }
        }

        oqlFreeByteArray(&ba);
        return MS_SUCCESS;
    }

    return MS_DONE;
}

/**
 * @brief Our iteminfo is list of indexes from 1..numitems.
 * @param layer
 * @return
 */
int msOqlLayerInitItemInfo(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerInitItemInfo called by layer %s.\n", layer->name);
    }

    if (layer->numitems == 0) {
        return MS_SUCCESS;
    }

    if (layer->iteminfo) {
        msFree(layer->iteminfo);
    }

    layer->iteminfo = msSmallMalloc(sizeof(int) * layer->numitems);

    if (!layer->iteminfo) {
        msSetError(MS_MEMERR, "Out of memory.", "msOqlLayerInitItemInfo()");
        return MS_FAILURE;
    }

    int * itemindexes = (int *)layer->iteminfo;
    int i = 0;
    for (i; i < layer->numitems; i++) {
        itemindexes[i] = i;
    }

    return MS_SUCCESS;
}

/**
 * @brief Free the item info
 * @param layer
 */
void msOqlLayerFreeItemInfo(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msOqlLayerFreeItemInfo called by layer %s.\n", layer->name);
    }

    if (layer->iteminfo) {
        msFree(layer->iteminfo);
    }

    layer->iteminfo = NULL;
}

/*
 * Registered vtable->LayerGetShape function. For pulling from a prepared and
 * undisposed result set.
 */
int msOqlLayerGetShape(layerObj * layer, shapeObj * shape, resultObj * record)
{
    if (layer->debug) {
        msDebug("msOqlLayerGetShape() not supported for Torm.\n");
    }

    return MS_SUCCESS;
}

/*
 * Registered vtable->LayerGetItems function. Query the database for
 * column information about the requested layer. Rather than look in
 * system tables, we just run a zero-cost query and read out of the
 * result header.
 */
int msOqlLayerGetItems(layerObj * layer)
{
    if (layer->debug) {
        msDebug("msOqlLayerGetItems() not supported for Torm.\n");
    }

    return MS_SUCCESS;
}

/*
 * Registered vtable->LayerGetExtent function. Query the database for
 * the extent of the requested layer.
 */
int msOqlLayerGetExtent(layerObj * layer, rectObj * extent)
{
    if (layer->debug) {
        msDebug("msOqlLayerGetExtent() not supported for Torm.\n");
    }

    return MS_SUCCESS;
}

char * msOqlEscapeSQLParam(layerObj * layer, const char * pszString)
{
    if (layer->debug) {
        msDebug("msOqlEscapeSQLParam() not supported for Torm.\n");
    }

    return NULL;
}

void msOqlEnablePaging(layerObj * layer, int value)
{
    if (layer->debug) {
        msDebug("msOqlEnablePaging() not supported for Torm.\n");
    }

    return;
}

int msOqlGetPaging(layerObj * layer)
{
    if (layer->debug) {
        msDebug("msOqlGetPaging() not supported for Torm.\n");
    }

    return MS_SUCCESS;
}

/*
 * Registered vtable->LayerTranslateFilter function.
 */
int msOqlLayerTranslateFilter(layerObj * layer, expressionObj * filter, char * filteritem)
{
    if (!filter->string) {
        return MS_SUCCESS; /* not an error, just nothing to do */
    }

    if (layer->debug) {
        msDebug("msOqlLayerTranslateFilter() not supported for Torm.\n");
    }

    return MS_FAILURE;
}

int msOqlLayerInitializeVirtualTable(layerObj * layer)
{
    assert(layer != NULL);
    assert(layer->vtable != NULL);

    //    layer->vtable->LayerTranslateFilter = msOqlLayerTranslateFilter;
    layer->vtable->LayerInitItemInfo = msOqlLayerInitItemInfo;
    layer->vtable->LayerFreeItemInfo = msOqlLayerFreeItemInfo;
    layer->vtable->LayerOpen = msOqlLayerOpen;
    layer->vtable->LayerIsOpen = msOqlLayerIsOpen;
    layer->vtable->LayerWhichShapes = msOqlLayerWhichShapes;
    layer->vtable->LayerNextShape = msOqlLayerNextShape;
    //    layer->vtable->LayerGetShape = msOqlLayerGetShape;
    layer->vtable->LayerClose = msOqlLayerClose;
    //    layer->vtable->LayerGetItems = msOqlLayerGetItems;
    //    layer->vtable->LayerGetExtent = msOqlLayerGetExtent;
    layer->vtable->LayerApplyFilterToLayer = msLayerApplyCondSQLFilterToLayer;
    /* layer->vtable->LayerGetAutoStyle, not supported for this layer */
    /* layer->vtable->LayerCloseConnection = msOqlLayerClose; */
    layer->vtable->LayerSetTimeFilter = msLayerMakeBackticsTimeFilter;
    /* layer->vtable->LayerCreateItems, use default */
    /* layer->vtable->LayerGetNumFeatures, use default */
    /* layer->vtable->LayerGetAutoProjection, use defaut*/
    //    layer->vtable->LayerEscapeSQLParam = msOqlEscapeSQLParam;
    //    layer->vtable->LayerEnablePaging = msOqlEnablePaging;
    //    layer->vtable->LayerGetPaging = msOqlGetPaging;

    return MS_SUCCESS;
}

// void msOqlSetContext(const void * context)
// {
//     oqlContext = context;
// }
