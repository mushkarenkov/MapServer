/*
 * Copyright (c) 2001-2023 Territorium Online Srl / TOL GmbH. All Rights Reserved.
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
#include "maptime.h"

#include <sqlite3.h>
#include "mapgeopackage.h"



// Specific information needed for managing this layer.
typedef struct gpkgLayerInfo {
    sqlite3 * conn;
    sqlite3_stmt * stmt;
    long index;

    unsigned char * fid;
    unsigned char * geom;
    unsigned char * data;
} gpkgLayerInfo;



/**
 * @brief msGeopackageCloseConnection
 *
 * Handler registered with msConnPoolRegister so that Mapserver
 * can clean up open connections during a shutdown.
 *
 * @param conn
 */
void msGeopackageCloseConnection(void * conn)
{
    sqlite3_close(conn);
}


/**
 * @brief Parse the DATA string for geometry column name, table name,
 *        unique id column, srid, and SQL string.
 *
 * @param layer
 * @return
 */
int msGeopackageParseData(layerObj *layer)
{
    char *data, *offset, *offset_tmp;
    char *offset_data, *offset_geom, *offset_fid;

    assert(layer != NULL);
    assert(layer->layerinfo != NULL);

    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) (layer->layerinfo);

    if (layer->debug) {
        msDebug("msGeopackageParseData called.\n");
    }

    if (!layer->data) {
        msSetError(MS_QUERYERR, "Missing DATA clause. DATA statement must contain 'geometry_column from table_name' or 'geometry_column from (sub-query) as sub'.", "msGeopackageParseData()");
        return MS_FAILURE;
    }

    data = layer->data;

    if(layerinfo->fid) {
        free(layerinfo->fid);
        layerinfo->fid = NULL;
    }
    if(layerinfo->geom) {
        free(layerinfo->geom);
        layerinfo->geom= NULL;
    }
    if(layerinfo->data) {
        free(layerinfo->data);
        layerinfo->data = NULL;
    }

    // Look for the optional ' using unique ID' string first.
    offset_fid = strcasestr(data, " using unique ");
    if (offset_fid) {
        // Find the end of this case 'using unique fid using srid=33'
        offset_tmp = strstr(offset_fid + 14, " ");
        // Find the end of this case 'using srid=33 using unique ftab_id'
        if (!offset_tmp) {
            offset_tmp = offset_fid + strlen(offset_fid);
        }
        layerinfo->fid = stream_clone(offset_fid + 14, offset_tmp - (offset_fid + 14));
    }

    offset = offset_fid;
    // No offset_fid? Move it to the end of the string.
    if (!offset) {
        offset = data + strlen(data);
    }

    // Scan for the 'geometry from table'.
    // Find the first non-white character to start from
    offset_geom = data;
    while( *offset_geom == ' ' || *offset_geom == '\t' || *offset_geom == '\n' || *offset_geom == '\r' )
        offset_geom++;

    // Find the end of the geom column name
    offset_data = strcasestr(data, " from ");
    if (!offset_data) {
        msSetError(MS_QUERYERR, "Error parsing Geopackage DATA variable. Must contain 'geometry from table' or 'geometry from (subselect) as foo'. %s", "msGeopackageParseData()", data);
        return MS_FAILURE;
    }

    // Copy the geometry column name & the table name or sub-select clause
    layerinfo->geom = stream_clone(offset_geom, offset_data - offset_geom);
    layerinfo->data = stream_clone(offset_data + 6, offset - (offset_data + 6));

    // Something is wrong, our goemetry column and table references are not there.
    if (strlen(layerinfo->data) < 1 || strlen(layerinfo->geom) < 1) {
        msSetError(MS_QUERYERR, "Error parsing Geopackage DATA variable. Must contain 'geometry from table' or 'geometry from (subselect) as foo'. %s", "msGeopackageParseData()", data);
        return MS_FAILURE;
    }

    // We didn't find a ' using unique ' in the DATA string so try and find a primary key on the table.
    if (!(layerinfo->fid)) {
        if (strstr(layerinfo->data, " ")) {
            msSetError(MS_QUERYERR, "Error parsing Geopackage DATA variable.  You must specify 'using unique' when supplying a subselect in the data definition.", "msGeopackageParseData()");
            return MS_FAILURE;
        }

        layerinfo->fid = "fid";
    }

    if (layer->debug) {
        msDebug("msGeopackageParseData: unique_column=%s, geom_column_name=%s, table_name=%s\n", layerinfo->fid, layerinfo->geom, layerinfo->data);
    }
    return MS_SUCCESS;
}


/**
 * @brief Returns malloc'ed char* that must be freed by caller.
 *
 * @param layer
 */
char * msGeopackageBuildSQLSelect(layerObj *layer)
{
    char * sqlSelect = NULL;

    if (layer->debug) {
        msDebug("msGeopackageBuildSQLItems called.\n");
    }
    assert( layer->layerinfo != NULL);

    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) layer->layerinfo;

    if (!layerinfo->geom) {
        msSetError(MS_MISCERR, "layerinfo->geom is not initialized.", "msGeopackageBuildSQLItems()");
        return NULL;
    }

    if(layer->debug > 1) {
        msDebug("msGeopackageBuildSQLItems: %d items requested.\n", layer->numitems);
    }

    // Build SQL to pull all the items.
    if (layer->numitems == 0) {
        sqlSelect = msStrdup(layerinfo->geom);
    } else {
        int length = strlen(layerinfo->geom) + 2;
        int i;
        for (i = 0; i < layer->numitems; i++) {
            length += strlen(layer->items[i]) + 3; /* itemname + "", */
        }
        sqlSelect = (char *) msSmallMalloc(length);
        sqlSelect[0] = '\0';
        for (i = 0; i < layer->numitems; i++) {
            strlcat(sqlSelect, "\"", length);
            strlcat(sqlSelect, layer->items[i], length);
            strlcat(sqlSelect, "\",", length);
        }
        strlcat(sqlSelect, layerinfo->geom, length);
    }

    return sqlSelect;
}

/**
 * @brief Returns malloc'ed char* that must be freed by caller.
 *
 * @param layer
 * @param rect
 */
char * msGeopackageBuildSQLFrom(layerObj *layer, rectObj *rect)
{
    if (layer->debug) {
        msDebug("msGeopackageBuildSQLFrom called.\n");
    }
    assert( layer->layerinfo != NULL);

    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) layer->layerinfo;

    if (!layerinfo->data) {
        msSetError(MS_MISCERR, "Layerinfo->fromsource is not initialized.", "msGeopackageBuildSQLFrom()");
        return NULL;
    }

    return stream_clone(layerinfo->data, strlen(layerinfo->data));
}


/**
 * @brief Returns malloc'ed char* that must be freed by caller.
 *
 * @param layer
 * @param rect
 * @param uid
 */
char * msGeopackageBuildSQLWhere(layerObj *layer, rectObj *rect, long *uid)
{
    char * sqlWhere = NULL;
    char *strRect = 0;
    char *strFilter1=0;
//    char *strFilter2=0;
//    char *strUid = 0;
//    char *strOrderBy = 0;
//    char *strLimit = 0;
//    char *strOffset = 0;
    size_t strRectLength = 0;
    size_t strFilterLength1=0;
//    size_t strFilterLength2=0;
//    size_t strUidLength = 0;
//    size_t strOrderByLength = 0;
//    size_t strLimitLength = 0;
//    size_t strOffsetLength = 0;
    size_t bufferSize = 0;
    int insert_and = 0;
    gpkgLayerInfo *layerinfo;

    if (layer->debug) {
        msDebug("msGeopackageBuildSQLFrom called.\n");
    }

    assert( layer->layerinfo != NULL);

    layerinfo = (gpkgLayerInfo *) layer->layerinfo;

    if (!layerinfo->fid) {
        msSetError(MS_MISCERR, "Layerinfo->fromsource is not initialized.", "msGeopackageBuildSQLWhere()");
        return NULL;
    }


    // Populate strLimit, if necessary.
//    if (layerinfo->paging && layer->maxfeatures >= 0) {
//        static char *strLimitTemplate = " limit %d";
//        strLimit = msSmallMalloc(strlen(strLimitTemplate) + 12);
//        sprintf(strLimit, strLimitTemplate, layer->maxfeatures);
//        strLimitLength = strlen(strLimit);
//    }

    // Populate strOffset, if necessary.
//    if ( layerinfo->paging && layer->startindex > 0 ) {
//        static char *strOffsetTemplate = " offset %d";
//        strOffset = msSmallMalloc(strlen(strOffsetTemplate) + 12);
//        sprintf(strOffset, strOffsetTemplate, layer->startindex-1);
//        strOffsetLength = strlen(strOffset);
//    }

//    Populate strRect, if necessary.
    if (rect && layerinfo->geom) {
        unsigned char buffer[1024];
        sprintf(&buffer, "%s IN (SELECT id FROM rtree_%s_%s WHERE maxx>=? AND minx<=? AND maxy>=? AND miny<=?)", layerinfo->fid, layerinfo->data, layerinfo->geom);
        strRect = stream_clone(&buffer, strlen(&buffer));
        strRectLength = strlen(strRect);

//        char *strBox = 0;
//        char *strSRID = 0;
//        size_t strBoxLength = 0;
//        static char *strRectTemplate = "%s && %s";

//        /* We see to set the SRID on the box, but to what SRID? */
//        strSRID = msPostGISBuildSQLSRID(layer);
//        if ( ! strSRID ) {
//            free( strLimit );
//            free( strOffset );
//            return NULL;
//        }

//        strBox = msPostGISBuildSQLBox(layer, rect, strSRID);
//        if ( strBox ) {
//            strBoxLength = strlen(strBox);
//        } else {
//            msSetError(MS_MISCERR, "Unable to build box SQL.", "msGeopackageBuildSQLWhere()");
//            free( strLimit );
//            free( strOffset );
//            return NULL;
//        }

//        strRect = (char*) msSmallMalloc(strlen(strRectTemplate) + strBoxLength + strlen(layerinfo->geom) + 1);
//        sprintf(strRect, strRectTemplate, layerinfo->geom, strBox);
//        strRectLength = strlen(strRect);
//        free(strBox);
//        free(strSRID);
    }

    /* Handle a translated filter (RFC91). */
    if (layer->filter.native_string) {
        static char *strFilterTemplate = "(%s)";
        strFilter1 = (char *) msSmallMalloc(strlen(strFilterTemplate) + strlen(layer->filter.native_string)+1);
        sprintf(strFilter1, strFilterTemplate, layer->filter.native_string);
        strFilterLength1 = strlen(strFilter1);
    }

//    /* Handle a native filter set as a PROCESSING option (#5001). */
//    if(msLayerGetProcessingKey(layer, "NATIVE_FILTER") != NULL) {
//        static char *strFilterTemplate = "(%s)";
//        char *native_filter = msLayerGetProcessingKey(layer, "NATIVE_FILTER");
//        strFilter2 = (char *) msSmallMalloc(strlen(strFilterTemplate) + strlen(native_filter)+1);
//        sprintf(strFilter2, strFilterTemplate, native_filter);
//        strFilterLength2 = strlen(strFilter2);
//    }

//    Populate strUid, if necessary.
//    if (uid) {
//        static char *strUidTemplate = "\"%s\" = %ld";
//        strUid = (char*)msSmallMalloc(strlen(strUidTemplate) + strlen(layerinfo->uid) + 64);
//        sprintf(strUid, strUidTemplate, layerinfo->uid, *uid);
//        strUidLength = strlen(strUid);
//    }

//    Populate strOrderBy, if necessary
//    if(layer->sortBy.nProperties > 0) {
//        char* pszTmp = msLayerBuildSQLOrderBy(layer);
//        strOrderBy = msStringConcatenate(strOrderBy, " ORDER BY ");
//        strOrderBy = msStringConcatenate(strOrderBy, pszTmp);
//        msFree(pszTmp);
//        strOrderByLength = strlen(strOrderBy);
//    }

//    bufferSize = strRectLength + 5 + (strFilterLength1 + 5) + (strFilterLength2 + 5) + strUidLength
//            + strLimitLength + strOffsetLength + strOrderByLength + 1;
    bufferSize = strRectLength + 5 + strFilterLength1 + 1;
    sqlWhere = (char*)msSmallMalloc(bufferSize);
    *sqlWhere = '\0';
    if (strRect) {
        strlcat(sqlWhere, strRect, bufferSize);
        insert_and++;
        free(strRect);
    }
    if (strFilter1) {
        if ( insert_and ) {
            strlcat(sqlWhere, " and ", bufferSize);
        }
        strlcat(sqlWhere, strFilter1, bufferSize);
        free(strFilter1);
        insert_and++;
    }
//    if (strFilter2) {
//        if ( insert_and ) {
//            strlcat(sqlWhere, " and ", bufferSize);
//        }
//        strlcat(sqlWhere, strFilter2, bufferSize);
//        free(strFilter2);
//        insert_and++;
//    }
//    if (strUid) {
//        if ( insert_and ) {
//            strlcat(sqlWhere, " and ", bufferSize);
//        }
//        strlcat(sqlWhere, strUid, bufferSize);
//        free(strUid);
//        insert_and++;
//    }

//    if (strOrderBy) {
//        strlcat(sqlWhere, strOrderBy, bufferSize);
//        free(strOrderBy);
//    }

//    if (strLimit) {
//        strlcat(sqlWhere, strLimit, bufferSize);
//        free(strLimit);
//    }
//    if (strOffset) {
//        strlcat(sqlWhere, strOffset, bufferSize);
//        free(strOffset);
//    }

    return sqlWhere;
}


/**
 * @brief Read the items for the shape
 * @param layer
 * @param shape
 * @param stmt
 * @return
 */
int msGeopackageReadShapeItems(layerObj * layer, shapeObj * shape, sqlite3_stmt * stmt)
{
    shape->values = (char **) msSmallMalloc(sizeof(char *) * layer->numitems);

    int i;
    for (i = 0; i < layer->numitems; i++) {
        // Iterate the columns
        const unsigned char * value = sqlite3_column_text(stmt, i);
        if(value) {
            shape->values[i] = stream_clone(value, strlen(value));
        } else
            shape->values[i] = msStrdup("");
    }
    shape->numvalues = layer->numitems;

    return MS_SUCCESS;
}


/**
 * @brief Returns malloc'ed char* that must be freed by caller.
 * @param layer
 * @param rect
 * @param uid
 */
char * msGeopackageBuildSQL(layerObj *layer, rectObj *rect, long *uid)
{
    char * sql = NULL;
    char * sqlFrom = NULL;
    char * sqlWhere = NULL;
    char * sqlSelect = NULL;
    char * sqlTemplate = NULL;
    static char * staticSQLTemplate = "SELECT %s FROM %s WHERE %s";

    if (layer->debug) {
        msDebug("msGeopackageBuildSQL called.\n");
    }
    assert(layer->layerinfo != NULL);


    sqlSelect = msGeopackageBuildSQLSelect(layer);
    if (!sqlSelect) {
        msSetError(MS_MISCERR, "Failed to build SQL items.", "msGeopackageBuildSQL()");
        return NULL;
    }


    sqlFrom = msGeopackageBuildSQLFrom(layer, rect);
    if (!sqlFrom) {
        msSetError(MS_MISCERR, "Failed to build SQL 'from'.", "msGeopackageBuildSQL()");
        return NULL;
    }


    sqlWhere = msGeopackageBuildSQLWhere(layer, rect, uid);
    if (!sqlWhere) {
        msSetError(MS_MISCERR, "Failed to build SQL 'where'.", "msGeopackageBuildSQL()");
        return NULL;
    }

    sqlTemplate = staticSQLTemplate;
    sql = msSmallMalloc(strlen(sqlTemplate) + strlen(sqlFrom) + strlen(sqlSelect) + strlen(sqlWhere) + 1);
    sprintf(sql, sqlTemplate, sqlSelect, sqlFrom, sqlWhere);

    msFree(sqlFrom);
    msFree(sqlWhere);
    msFree(sqlSelect);

    return sql;
}


/**
 * @brief Checks if the connection is already open
 *
 * @param layer
 */
int msGeoPackageLayerIsOpen(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerIsOpen called by layer %s.\n", layer->name);
    }

    return layer->layerinfo ? MS_TRUE : MS_FALSE;
}


/*
 * Registered vtable->LayerOpen function.
 */
int msGeoPackageLayerOpen(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerOpen called by layer %s.\n", layer->name);
    }

    if (layer->layerinfo) {
        return MS_SUCCESS;  /* already open */
    }

    if (!layer->connection) {
        msSetError(MS_QUERYERR, "Nothing specified in CONNECTION statement.", "msGeoPackageLayerOpen()");
        return MS_FAILURE;
    }


    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) msSmallMalloc(sizeof(gpkgLayerInfo));
    layerinfo->fid = NULL;
    layerinfo->geom = NULL;
    layerinfo->data = NULL;

    // Get a database connection from the pool.
    layerinfo->conn = msConnPoolRequest(layer);

    if (!layerinfo->conn) {
        int result = sqlite3_open_v2(layer->connection, &(layerinfo->conn), SQLITE_OPEN_READONLY, 0);
        if (result != SQLITE_OK) {
            msSetError(MS_QUERYERR, "Can't open database: %s\n", sqlite3_errmsg(layerinfo->conn));
            sqlite3_close(layerinfo->conn);
            return MS_FAILURE;
        }

        msConnPoolRegister(layer, layerinfo->conn, msGeopackageCloseConnection);
    }

    layer->layerinfo = (void *) layerinfo;
    msGeopackageParseData(layer);

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerClose function.
 */
int msGeoPackageLayerClose(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerClose called by layer %s.\n", layer->name);
    }

    if (layer->layerinfo) {
        gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) layer->layerinfo;

        if(layerinfo->stmt)
            sqlite3_finalize(layerinfo->stmt);

        if(layerinfo->fid)
            msFree(layerinfo->fid);

        if(layerinfo->geom)
            msFree(layerinfo->geom);

        if(layerinfo->data)
            msFree(layerinfo->data);

        // required: PROCESSING "CLOSE_CONNECTION=DEFER"
        msConnPoolRelease(layer, layerinfo->conn);
        msFree(layerinfo);

        layer->layerinfo = NULL;
    }

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerWhichShapes function.
 */
int msGeoPackageLayerWhichShapes(layerObj * layer, rectObj rect, int isQuery)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerWhichShapes called by layer %s(%f, %f, %f, %f).\n", layer->name, rect.minx, rect.miny, rect.maxx, rect.maxy);
    }

    assert(layer->layerinfo != NULL);

    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) layer->layerinfo;
    layerinfo->stmt = NULL;
    layerinfo->index = 0;

    char * buffer = msGeopackageBuildSQL(layer, &rect, 0);

    sqlite3_prepare_v2(layerinfo->conn, buffer, -1, &layerinfo->stmt, NULL);
    sqlite3_bind_double(layerinfo->stmt, 1, rect.minx);
    sqlite3_bind_double(layerinfo->stmt, 2, rect.maxx);
    sqlite3_bind_double(layerinfo->stmt, 3, rect.miny);
    sqlite3_bind_double(layerinfo->stmt, 4, rect.maxy);

    msFree(buffer);

    if (layer->debug) {
        fprintf(stderr, "SQL: %s\n", sqlite3_expanded_sql(layerinfo->stmt));
        msDebug("msGeoPackageLayerWhichShapes SQL: %s.\n", sqlite3_expanded_sql(layerinfo->stmt));
    }

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerNextShape function.
 */
int msGeoPackageLayerNextShape(layerObj * layer, shapeObj * shape)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerNextShape called by layer %s.\n", layer->name);
    }

    assert(layer->layerinfo != NULL);

    gpkgLayerInfo * layerinfo = (gpkgLayerInfo *) layer->layerinfo;

    if(sqlite3_step(layerinfo->stmt) == SQLITE_ROW)
    {
        int n_cols = layer->numitems;
        int n_bytes = sqlite3_column_bytes(layerinfo->stmt, n_cols);
        unsigned char * data = sqlite3_column_blob(layerinfo->stmt, n_cols);

        if(readShape(shape, data, n_bytes) && shape->type == MS_SHAPE_NULL)
            return MS_SUCCESS;

        shape->index = layerinfo->index++;
        return msGeopackageReadShapeItems(layer, shape, layerinfo->stmt);
    }

    sqlite3_finalize(layerinfo->stmt);
    layerinfo->stmt = NULL;
    return MS_DONE;
}


/**
 * @brief Our iteminfo is list of indexes from 1..numitems.
 * @param layer
 * @return
 */
int msGeoPackageLayerInitItemInfo(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerInitItemInfo called by layer %s.\n", layer->name);
    }

    if (layer->numitems == 0) {
        return MS_SUCCESS;
    }

    if (layer->iteminfo) {
        free(layer->iteminfo);
    }

    layer->iteminfo = msSmallMalloc(sizeof(int) * layer->numitems);

    if (!layer->iteminfo) {
        msSetError(MS_MEMERR, "Out of memory.", "msGeoPackageLayerInitItemInfo()");
        return MS_FAILURE;
    }

    int * itemindexes = (int *) layer->iteminfo;
    int i = 0;
    for (i; i < layer->numitems; i++) {
        itemindexes[i] = i; /* Last item is always the geometry. The rest are non-geometry. */
    }

    return MS_SUCCESS;
}


/**
 * @brief Free the item info
 * @param layer
 */
void msGeoPackageLayerFreeItemInfo(layerObj * layer)
{
    assert(layer != NULL);

    if (layer->debug) {
        msDebug("msGeoPackageLayerFreeItemInfo called by layer %s.\n", layer->name);
    }

    if (layer->iteminfo) {
        free(layer->iteminfo);
    }

    layer->iteminfo = NULL;
}


/*
 * Registered vtable->LayerGetShape function. For pulling from a prepared and
 * undisposed result set.
 */
int msGeoPackageLayerGetShape(layerObj * layer, shapeObj * shape, resultObj * record)
{
    if (layer->debug) {
        msDebug("msGeoPackageLayerGetShape() not supported for Geopackage.\n");
    }

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerGetItems function. Query the database for
 * column information about the requested layer. Rather than look in
 * system tables, we just run a zero-cost query and read out of the
 * result header.
 */
int msGeoPackageLayerGetItems(layerObj * layer)
{
    if (layer->debug) {
        msDebug("msGeoPackageLayerGetItems() not supported for Geopackage.\n");
    }

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerGetExtent function. Query the database for
 * the extent of the requested layer.
 */
int msGeoPackageLayerGetExtent(layerObj * layer, rectObj * extent)
{
    if (layer->debug) {
        msDebug("msGeoPackageLayerGetExtent() not supported for Geopackage.\n");
    }

    return MS_SUCCESS;
}


char * msGeoPackageEscapeSQLParam(layerObj * layer, const char * pszString)
{
    if (layer->debug) {
        msDebug("msGeoPackageEscapeSQLParam() not supported for Geopackage.\n");
    }

    return NULL;
}


void msGeoPackageEnablePaging(layerObj * layer, int value)
{
    if (layer->debug) {
        msDebug("msGeoPackageEnablePaging() not supported for Geopackage.\n");
    }

    return;
}


int msGeoPackageGetPaging(layerObj * layer)
{
    if (layer->debug) {
        msDebug("msGeoPackageGetPaging() not supported for Geopackage.\n");
    }

    return MS_SUCCESS;
}


/*
 * Registered vtable->LayerTranslateFilter function.
 */
int msGeoPackageLayerTranslateFilter(layerObj * layer, expressionObj * filter, char * filteritem)
{
    if (!filter->string) {
        return MS_SUCCESS;    /* not an error, just nothing to do */
    }

    if (layer->debug) {
        msDebug("msGeoPackageLayerTranslateFilter() not supported for Geopackage.\n");
    }

    return MS_FAILURE;
}



int msGeoPackageLayerInitializeVirtualTable(layerObj * layer)
{
    assert(layer != NULL);
    assert(layer->vtable != NULL);

//    layer->vtable->LayerTranslateFilter = msGeoPackageLayerTranslateFilter;
    layer->vtable->LayerInitItemInfo = msGeoPackageLayerInitItemInfo;
    layer->vtable->LayerFreeItemInfo = msGeoPackageLayerFreeItemInfo;
    layer->vtable->LayerOpen = msGeoPackageLayerOpen;
    layer->vtable->LayerIsOpen = msGeoPackageLayerIsOpen;
    layer->vtable->LayerWhichShapes = msGeoPackageLayerWhichShapes;
    layer->vtable->LayerNextShape = msGeoPackageLayerNextShape;
//    layer->vtable->LayerGetShape = msGeoPackageLayerGetShape;
    layer->vtable->LayerClose = msGeoPackageLayerClose;
//    layer->vtable->LayerGetItems = msGeoPackageLayerGetItems;
//    layer->vtable->LayerGetExtent = msGeoPackageLayerGetExtent;
    layer->vtable->LayerApplyFilterToLayer = msLayerApplyCondSQLFilterToLayer;
    /* layer->vtable->LayerGetAutoStyle, not supported for this layer */
    /* layer->vtable->LayerCloseConnection = msGeoPackageLayerClose; */
    layer->vtable->LayerSetTimeFilter = msLayerMakeBackticsTimeFilter;
    /* layer->vtable->LayerCreateItems, use default */
    /* layer->vtable->LayerGetNumFeatures, use default */
    /* layer->vtable->LayerGetAutoProjection, use defaut*/
//    layer->vtable->LayerEscapeSQLParam = msGeoPackageEscapeSQLParam;
//    layer->vtable->LayerEnablePaging = msGeoPackageEnablePaging;
//    layer->vtable->LayerGetPaging = msGeoPackageGetPaging;

    return MS_SUCCESS;
}
