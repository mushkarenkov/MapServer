#include "mapserv-api_cpp.h"

#include "mapserver.h"
#include "maperror.h"
#include "mapcopy.h"
#include "mapproject.h"

#include "IOOql/mapcriteria.h"
#include "IOModel/model/metaclassifier.h"
#include <IOItem/itemcontext.h>
#include "IOVariant/variant/reprojectgeometry.h"

#include <IOBase/Logger>

using namespace ms;

Map::Map(mapObj * map)
    : _map(map)
    , _error({})
{
}

Map::~Map()
{
    msFreeMap(_map);
}

Map::Map(const Map & map)
    : _map(cloneMapObj(map._map))
    , _error(map._error)
{
}

Map::Map(Map && map) noexcept
    : _map(std::exchange(map._map, nullptr))
    , _error(std::exchange(map._error, {}))
{
}

mapObj * Map::cloneMapObj(const mapObj * map)
{
    if (map == nullptr) {
        return nullptr;
    }

    mapObj * m = msNewMapObj();

    if (m == NULL) {
        _error = ms::error();
        return nullptr;
    }

    if (msCopyMap(m, map) != MS_SUCCESS) {
        msFreeMap(m);
        _error = ms::error();
        return nullptr;
    }

    return m;
}

void Map::swap(Map & m) noexcept
{
    std::swap(_map, m._map);
    std::swap(_error, m._error);
}

Image Map::draw(mapObj * map) const
{
    if (imageObj * img = msDrawMap(map, MS_FALSE)) {
        return Image(img);
    } else {
        return Image("Failed to draw map: " + ms::error());
    }
}

Map & Map::operator=(const Map & map)
{
    if (this == &map) {
        return *this;
    }

    Map copy(map);
    swap(copy);
    return *this;
}

Map & Map::operator=(Map && map) noexcept
{
    if (this == &map) {
        return *this;
    }

    Map moved(std::move(map));
    swap(moved);
    return *this;
}

Map Map::createFromFile(const std::string & filename)
{
    return Map(msLoadMap(filename.c_str(), NULL, NULL));
}

Map Map::createFromString(std::string mapstring)
{
    return Map(msLoadMapFromString(mapstring.data(), NULL, NULL));
}

Map Map::createOfflineMap(Map m)
{
    mapObj * map = m._map;

    // clear fontset and symbolset filenames so they are not written to the new mapfile.
    msFree(map->fontset.filename);
    map->fontset.filename = NULL;
    msFree(map->symbolset.filename);
    map->symbolset.filename = NULL;

    // cycle through all symbols and set them to inmapfile so we can do away with the symbolfile. Any truetype or pixmap symbols (not supported) are replaced with a basic symbol (works for point, line and poly)
    for (int i = 1; i < map->symbolset.numsymbols; i++) {
        symbolObj * symbol = map->symbolset.symbol[i];
        symbol->inmapfile = 1;
        if (symbol->type == MS_SYMBOL_TRUETYPE || symbol->type == MS_SYMBOL_PIXMAP) {
            symbol->type = MS_SYMBOL_ELLIPSE;
            symbol->filled = MS_TRUE;
            symbol->numpoints = 1;
            symbol->points[0].x = 1;
            symbol->points[0].y = 1;
            if (symbol->imagepath) {
                msFree(symbol->imagepath);
                symbol->imagepath = NULL;
            }
        }
    }

    // Removing PROJ_DATA path
    // msSetConfigOption(map, "PROJ_DATA", NULL);

    // do we need to transform projections to inline notation?
    //  map->projection?

    // define this if not already done via configure/makefile to include a standard font that is already available in mapserver
    // #define _DEJAVU_SANS_CONDENSED_H

    // cycle through all layers and disable (for output in new mapfile) all layers that are not of connectiontype TORM
    for (int i = 0; i < map->numlayers; i++) {
        layerObj * layer = map->layers[i];
        if (layer->connectiontype != MS_PLUGIN) {
            layer->status = MS_DELETE;
        } else {
            // cycle through all classes and labels and replace all fonts (we do not support a font file with custom fonts) with the special keyword _ms_default_ (which is the internal name of dejavu sans, see above)
            for (int j = 0; j < layer->numclasses; j++) {
                classObj * clazz = layer->_class[j];
                for (int k = 0; k < clazz->numlabels; k++) {
                    labelObj * label = clazz->labels[k];
                    msFree(label->font);
                    label->font = msStrdup("_ms_default_");
                }
            }
        }
    }

    return m;
}

Image Map::draw(io::ItemContext * context, const io::MapParams & params) const
{
    Map m(*this); // clone mapObj

    if (!m.isValid()) {
        return Image("Unable to create an image from an invalid map: " + m.error());
    }

    mapObj * map = m._map;

    if (params._width > 0 && params._height > 0) {
        if (msMapSetSize(map, params._width, params._height) == MS_FAILURE) {
            return Image("Failed to set map size");
        }
    }

    if (params._extent._bbox[0] < params._extent._bbox[2] && params._extent._bbox[1] < params._extent._bbox[3]) {
        if (msMapSetExtent(map, params._extent._bbox[0], params._extent._bbox[1], params._extent._bbox[2], params._extent._bbox[3]) == MS_FAILURE) {
            return Image("Unable to set extent: " + ms::error());
        }
    }

    if (params._dpi > 0) {
        map->resolution = params._dpi;
    }

    map->imagetype = params._img.empty() ? NULL : msStrdup(params._img.c_str());

    if (msPostMapParseOutputFormatSetup(map) == MS_FAILURE) {
        return Image(ms::error());
    }

    // Projection
    static const std::string proj = "+init=epsg:";

    if (params._srid > 0 && msLoadProjectionString(&map->projection, (proj + std::to_string(params._srid)).c_str()) == -1) {
        return Image(ms::error());
    }

    // Extent
    rectObj rect = {params._extent._bbox[0],
                    params._extent._bbox[1],
                    params._extent._bbox[2],
                    params._extent._bbox[3]};

    if (params._srid != params._extent._srid) {
        // Transform extent coordinates
        projectionObj extent_proj;
        bool ok = msInitProjection(&extent_proj) == 0 && msLoadProjectionString(&extent_proj, (proj + std::to_string(params._extent._srid)).c_str()) == 0 && msProjectRect(&extent_proj, &map->projection, &rect) != MS_FAILURE;
        msFreeProjection(&extent_proj);

        if (!ok) {
            return Image(ms::error());
        }
    }

    map->extent.minx = rect.minx;
    map->extent.miny = rect.miny;
    map->extent.maxx = rect.maxx;
    map->extent.maxy = rect.maxy;

    if (!MS_VALID_EXTENT(map->extent)) {
        return Image("Invalid extent");
    }

    auto addLayer = [&map, &context](const std::string & name) -> layerObj * {
        // Add a new layer with wkt geometry
        layerObj * layer = msGrowMapLayers(map);

        if (layer == nullptr || initLayer(layer, map) == -1) {
            return nullptr;
        }

        // Changing name
        MS_COPYSTRING(layer->name, name.c_str());

        // Inreasing layer counter
        map->layerorder[layer->index] = layer->index = map->numlayers++;

        // Context
        msOqlResetLayerInfo(layer, context);

        return layer;
    };

    auto addClass = [](layerObj * layer) -> classObj * {
        // Adding a new class
        classObj * cl = msGrowLayerClasses(layer);

        if (cl == nullptr || initClass(cl) != 0) {
            return nullptr;
        }

        // Class name must be set to be listed in legend
        cl->name = msStrdup(layer->name);

        // Reference to parent layer
        cl->layer = layer;

        // Inreasing class counter
        layer->numclasses++;

        return cl;
    };

    typedef io::MapParams::Layer::Type MapLayerType;
    typedef io::MapParams::Style MapLayerStyle;

    auto addStyle = [](classObj * cl, const std::unordered_map<MapLayerType, MapLayerStyle> & styles) -> styleObj * {
        // Adding a new style
        styleObj * st = msGrowClassStyles(cl);

        if (st == nullptr || initStyle(st) != MS_SUCCESS) {
            return nullptr;
        }

        // Inreasing style counter
        cl->numstyles++;

        // Setting the new style from request
        static const std::map<MS_LAYER_TYPE, MapLayerType> kLayerTypeMap{
            {MS_LAYER_TYPE::MS_LAYER_POINT, MapLayerType::E_Point},
            {MS_LAYER_TYPE::MS_LAYER_LINE, MapLayerType::E_Line},
            {MS_LAYER_TYPE::MS_LAYER_POLYGON, MapLayerType::E_Polygon},
        };

        MS_LAYER_TYPE type = cl->layer->type;
        const auto itype = kLayerTypeMap.find(type);

        if (itype == kLayerTypeMap.end()) {
            LOG_ERROR(r << "Layer '%1' has unsupported geometry type: '%2'" << QString(cl->layer->name) << type);
            return nullptr;
        }

        const auto istyle = styles.find(itype->second);

        if (istyle == styles.end()) {
            static const std::map<MS_LAYER_TYPE, std::string> kTypeStringMap{
                {MS_LAYER_TYPE::MS_LAYER_POINT, "Point"},
                {MS_LAYER_TYPE::MS_LAYER_LINE, "Line"},
                {MS_LAYER_TYPE::MS_LAYER_POLYGON, "Polygon"},
            };

            LOG_ERROR(r << "Undefined style for geometry type: '%1'" << kTypeStringMap.at(type).c_str());
            return nullptr;
        }

        MapLayerStyle style = istyle->second;

        auto setColor = [](const std::string & hexstr, colorObj & clr) {
            int len = hexstr.length();

            if ((len != 7 && len != 9) || hexstr.at(0) != '#') {
                LOG_ERROR(r << "Invalid color: '%1'" << hexstr.c_str());
                return false;
            }

            clr.red = msHexToInt(hexstr.substr(1, 2).data());
            clr.green = msHexToInt(hexstr.substr(3, 2).data());
            clr.blue = msHexToInt(hexstr.substr(5, 2).data());
            clr.alpha = (len == 9) ? msHexToInt(hexstr.substr(7, 2).data()) : 255;

            return true;
        };

        if (!setColor(style._color, st->color) || !setColor(style._outlinecolor, st->outlinecolor)) {
            return nullptr;
        }

        // st->opacity = 100;

        if (st->opacity < 100) {
            int alpha = MS_NINT(st->opacity * 2.55);

            st->color.alpha = alpha;
            st->outlinecolor.alpha = alpha;

            st->mincolor.alpha = alpha;
            st->maxcolor.alpha = alpha;
        }

        st->size = 1;

        if (!style._symbol.empty()) {
            st->symbolname = msStrdup(style._symbol.c_str());
        }

        st->width = 1;

        // const int n = style._pattern.size();
        // if (n > MS_MAXPATTERNLENGTH) {
        //     LOG_ERROR(r << "Pattern array out size = %1 > %2" << n << MS_MAXPATTERNLENGTH);
        //     return nullptr;
        // }

        // st->patternlength = n;

        // for (int i = 0; i < n; ++i) {
        //     st->pattern[i] = style._pattern[i];
        // }

        return st;
    };

    std::vector<bool> visibles(map->numlayers, false);
    for (const io::MapParams::Layer & layer : params._layers) {
        if (!layer._geometry.empty()) {
            // Geometry layer
            layerObj * wktlayer = addLayer(layer._name.empty() ? "wkt_" + std::to_string(map->numlayers) : layer._name);

            if (wktlayer == nullptr) {
                return Image(ms::error());
            }

            // Enable
            wktlayer->status = MS_ON;

            // Adding feature
            wktlayer->connectiontype = MS_INLINE;
            shapeObj * shape = msShapeFromWKT(layer._geometry.c_str());

            if (shape == nullptr) {
                return Image("Failed to create shape from WKT: " + ms::error());
            }

            wktlayer->type = MS_LAYER_TYPE(shape->type);
            shape->index = 0;

            featureListNodeObjPtr list = insertFeatureList(&(wktlayer->features), shape);

            msFreeShape(shape);
            msFree(shape);

            if (list == NULL) {
                return Image("Failed to insert shape: " + ms::error());
            }

            // Adding a new class
            classObj * cl = addClass(wktlayer);

            if (cl == nullptr) {
                return Image(ms::error());
            }

            // Adding a new style
            if (addStyle(cl, layer._style) == nullptr) {
                return Image(ms::error());
            }

            continue;
        }

        // Name must be set
        const std::string & lname = layer._name;

        if (lname.empty()) {
            return Image("Undefined layer name");
        }

        int idx = msGetLayerIndex(map, lname.c_str());

        if (idx == -1) {
            // return err("Undefined layer: " + QString::fromStdString(lname));
            LOG_ERROR(r << "Ignoring undefined layer '%1'" << lname.c_str());
            continue;
        }

        layerObj * l = GET_LAYER(map, idx);

        // Just visualize this layer if niether style nor filter is specified
        if (layer._style.empty() && layer._filter.empty()) {
            // Set this layer as visible
            visibles[idx] = true;

            // Context
            msOqlResetLayerInfo(l, context);

            // Enable
            l->status = MS_ON;

            continue;
        }

        // Visualize cloned layer with modified filter and/or style
        layerObj * acetatelayer = addLayer(std::string(l->name) + "_" + std::to_string(map->numlayers));

        if (msCopyLayer(acetatelayer, l) == MS_FAILURE) {
            return Image(ms::error());
        }

        // Enable
        acetatelayer->status = MS_ON;

        // Filter
        if (!layer._filter.empty()) {
            // Reproject geometries in the filter expression to layer's projection
            char * clsname = l->data;
            const io::MetaClassifier * cls = context->getModel()->classifier(clsname);

            if (cls == nullptr) {
                return Image("Model does not contains metaclassifier: '" + std::string(clsname) + "'");
            }

            int srid = cls->srid();

            auto addProcessing = [&acetatelayer](const std::string & filter) {
                msLayerAddProcessing(acetatelayer, ("NATIVE_FILTER=" + filter).c_str());
            };

            if (srid > 0) {
                QString f = QString::fromStdString(layer._filter.c_str());
                io::Expression * in = io::Expression::of(f);
                Reprojector repr(0, srid);
                io::ReprojectGeometry reproject(&repr);
                in->accept(reproject, &f);
                delete in;
                addProcessing(f.toStdString());
            } else {
                addProcessing(layer._filter);
            }
        }

        if (layer._style.empty()) {
            continue;
        }

        // Delete all classes and insert a new one with the new style
        for (int i = 0; i < acetatelayer->numclasses; ++i) {
            classObj * c = msRemoveClass(acetatelayer, i);

            if (c == nullptr || freeClass(c) == MS_FAILURE) {
                return Image(ms::error());
            }

            msFree(c);
        }

        // Adding a new class
        classObj * cl = addClass(acetatelayer);

        if (cl == nullptr) {
            return Image(ms::error());
        }

        // Adding a new style
        if (addStyle(cl, layer._style) == nullptr) {
            return Image(ms::error());
        }
    }

    // Delete non-listed layers
    if (!params._layers.empty()) {
        for (uint j = 0, i = 0; i < visibles.size(); ++i) {
            if (visibles.at(i)) {
                ++j;
                continue;
            }

            if (layerObj * removed = msRemoveLayer(map, j)) {
                freeLayer(removed);
                msFree(removed);
                continue;
            }

            return Image(ms::error());
        }
    }

    // Rotation
    if (msMapSetRotation(map, params._rotation) == MS_FAILURE) {
        return Image(ms::error());
    }

    // Finalize map setup
    // (Functions called at the end of map file parsing)
    {
        /*** Make config options current ***/
        msApplyMapConfigOptions(map);

        /*** Compute rotated extent info if applicable ***/
        msMapComputeGeotransform(map);

        /*** OUTPUTFORMAT related setup ***/
        if (msPostMapParseOutputFormatSetup(map) == MS_FAILURE)
            return Image(ms::error());

        // if (loadSymbolSet(&(map->symbolset), map) == -1)
        //     return err();

        // if (resolveSymbolNames(map) == MS_FAILURE)
        //     return err();

        // if (msLoadFontSet(&(map->fontset), map) == -1)
        //     return err();
    }

    // Image
    return draw(map);
}

Image Map::draw() const
{
    Map m(*this); // clone mapObj

    if (!m.isValid()) {
        return Image("Unable to create an image from an invalid map: " + m.error());
    }

    // Image
    return draw(m._map);
}

Catalog Map::catalog(int width, int height) const
{
    Map m(*this); // clone mapObj

    Catalog catalog{};

    if (!m.isValid()) {
        catalog._error = "Unable to create a catalog from an invalid map: " + m.error();
        return catalog;
    }

    catalog._extent = {_map->extent.minx, _map->extent.miny, _map->extent.maxx, _map->extent.maxy};

    int nlayers = m._map->numlayers;
    for (int i = 0; i < nlayers; ++i) {
        layerObj * l = GET_LAYER(m._map, i);

        Catalog::Layer lr{l->name ? l->name : "", {}};

        for (int j = 0; j < l->numclasses; ++j) {
            classObj * c = l->_class[j];

            imageObj * img = msCreateLegendIcon(m._map, l, c, width, height, MS_TRUE);

            if (img == nullptr) {
                catalog._error = ms::error();
                return catalog;
            }

            Catalog::Legend lg{
                c->name ? c->name : "",
                Image(img),
                c->minscaledenom,
                c->maxscaledenom};

            lr._lagends.push_back(std::move(lg));
        }

        catalog._layers.push_back(std::move(lr));
    }
}

std::string Map::string() const
{
    return msWriteMapToString(_map);
}

const std::string & Map::error() const
{
    return _error;
}

bool Map::isValid() const
{
    return _map != nullptr && _error.empty();
}

Image::Image(imageObj * img)
    : _img(img)
    , _error({})
{
}

Image::Image(std::string err)
    : _img(nullptr)
    , _error(err)
{
}

Image::~Image()
{
    msFreeImage(_img);
}

// Image::Image(Image && img) noexcept
//     : _img(std::exchange(img._img, nullptr))
//     , _error(std::exchange(img._error, {}))
// {
// }

// Image & Image::operator=(Image && img) noexcept
// {
//     if (this == &img) {
//         return *this;
//     }

//     _error = std::move(img._error);
//     _img = std::move(img._img);
//     return *this;
// }

int Image::width() const
{
    return isValid() ? _img->width : 0;
}

int Image::height() const
{
    return isValid() ? _img->height : 0;
}

void Image::render(quint8 * bytebuffer) const
{
    if (!isValid()) {
        return;
    }

    rendererVTableObj * renderer = _img->format->vtable;

    if (renderer->supports_pixel_buffer) {
        rasterBufferObj data;

        if (renderer->getRasterBufferHandle(_img, &data) != MS_FAILURE) {
            unsigned int row;
            unsigned int col;
            quint8 *a, *r, *g, *b;

            for (row = 0; row < data.height; row++) {
                r = data.data.rgba.r + row * data.data.rgba.row_step;
                g = data.data.rgba.g + row * data.data.rgba.row_step;
                b = data.data.rgba.b + row * data.data.rgba.row_step;

                if (data.data.rgba.a) { // RGB_ALPHA
                    a = data.data.rgba.a + row * data.data.rgba.row_step;

                    for (col = 0; col < data.width; col++) {
                        unsigned int ptr = data.width * 4 * row + col * 4;
                        bytebuffer[ptr] = *r;
                        bytebuffer[ptr + 1] = *g;
                        bytebuffer[ptr + 2] = *b;
                        bytebuffer[ptr + 3] = *a;

                        a += data.data.rgba.pixel_step;
                        r += data.data.rgba.pixel_step;
                        g += data.data.rgba.pixel_step;
                        b += data.data.rgba.pixel_step;
                    }
                } else { // RGB
                    for (col = 0; col < data.width; col++) {
                        unsigned int ptr = data.width * 4 * row + col * 4;
                        bytebuffer[ptr] = *r;
                        bytebuffer[ptr + 1] = *g;
                        bytebuffer[ptr + 2] = *b;
                        bytebuffer[ptr + 3] = 255;

                        r += data.data.rgba.pixel_step;
                        g += data.data.rgba.pixel_step;
                        b += data.data.rgba.pixel_step;
                    }
                }
            }
        }
    }
}

const std::string & Image::error() const
{
    return _error;
}

bool Image::isValid() const
{
    return _img != nullptr && _error.empty();
}

Reprojector::Reprojector(int sridSource, int sridTarget)
    : io::Reprojector(0, 0, "MapServerReprojector")
{
    set(sridSource, sridTarget);
}

Reprojector::Reprojector(void * pSource, int sridSource, void * pTarget, int sridTarget)
    : io::Reprojector(sridSource, sridTarget, "MapServerReprojector")
{
    auto cloneProjection = [this](projectionObj * src, int & srid) -> projectionObj * {
        if (src == NULL) {
            srid = 0;
            return nullptr;
        }

        projectionObj * proj = new projectionObj;

        if (msInitProjection(proj) != 0 || msCopyProjection(proj, src) != MS_SUCCESS) {
            delete proj;
            srid = 0;
            return nullptr;
        }

        return static_cast<projectionObj *>(_projections[srid] = proj);
    };

    projectionObj * projSource = cloneProjection(static_cast<projectionObj *>(pSource), _sridSource);
    projectionObj * projTarget = cloneProjection(static_cast<projectionObj *>(pTarget), _sridTarget);

    if (projSource != nullptr && projTarget != nullptr) {
        if (reprojectionObj * reprojector = msProjectCreateReprojector(projSource, projTarget)) {
            _reprojectors[_sridSource][_sridTarget] = _reprojector = reprojector;
        }
    }
}

Reprojector::~Reprojector()
{
    for (auto & pr : _projections) {
        msFreeProjection(static_cast<projectionObj *>(pr.second));
        msFree(static_cast<projectionObj *>(pr.second));
    }

    for (auto & mrpr : _reprojectors) {
        for (auto & rpr : mrpr.second)
            msProjectDestroyReprojector(static_cast<reprojectionObj *>(rpr.second));
    }
}

void Reprojector::reproject(double * x, double * y, double *) const
{
    if (!isValid()) {
        LOG1_ERROR((*_logger), r << "Invalid reprojector");
        return;
    }

    pointObj p{*x, *y, 0, 0};
    int ok = msProjectPointEx(static_cast<reprojectionObj *>(_reprojector), &p);

    if (ok != MS_SUCCESS) {
        LOG1_ERROR((*_logger), r << "Error in reprojecting a point");
        return;
    }

    *x = p.x;
    *y = p.y;
}

void Reprojector::setSridSource(int srid)
{
    set(srid, _sridTarget);
}

void Reprojector::setSridTarget(int srid)
{
    set(_sridSource, srid);
}

bool Reprojector::isValid() const
{
    return _reprojector != nullptr;
}

void Reprojector::set(int source, int target)
{
    if (source == _sridSource && target == _sridTarget) {
        return;
    }

    _reprojector = nullptr;
    _sridSource = source;
    _sridTarget = target;

    if (source == target) {
        return;
    }

    if (auto rs = _reprojectors.find(source); rs != _reprojectors.end()) {
        if (auto rt = rs->second.find(target); rt != rs->second.end()) {
            _reprojector = _reprojectors[source][target];
            return;
        }
    }

    auto getProjection = [this](int & srid) -> projectionObj * {
        if (auto p = _projections.find(srid); p != _projections.end()) {
            return static_cast<projectionObj *>(p->second);
        }

        // static const std::string init = "+init=epsg:";
        static const std::string init = "init=epsg:";
        projectionObj * proj = new projectionObj;

        if (msInitProjection(proj) != 0 || msLoadProjectionString(proj, (init + std::to_string(srid)).c_str()) != 0) {
            delete proj;
            srid = 0;
            return nullptr;
        }

        return static_cast<projectionObj *>(_projections[srid] = proj);
    };

    projectionObj * sourceProj = getProjection(_sridSource);
    projectionObj * targetProj = getProjection(_sridTarget);

    if (sourceProj != nullptr && targetProj != nullptr) {
        if (reprojectionObj * reprojector = msProjectCreateReprojector(sourceProj, targetProj)) {
            _reprojectors[_sridSource][_sridTarget] = _reprojector = reprojector;
        }
    }
}

std::string ms::error()
{
    errorObj * ms_error = msGetErrorObj();

    QStringList msgList;

    while (ms_error && ms_error->code != MS_NOERR) {
        msgList.append(QString("%1: (%2) %3").arg(ms_error->routine, QString::number(ms_error->code), ms_error->message));
        msIO_fprintf(stderr, "%s: %s %s <br>\n", ms_error->routine, msGetErrorCodeString(ms_error->code), ms_error->message);
        ms_error->isreported = MS_TRUE;
        ms_error = ms_error->next;
    }

    return msgList.join(";\n").toStdString();
}

void ms::cleanup()
{
    msCleanup();
}

std::optional<std::string> ms::init()
{
    if (msSetup() != MS_SUCCESS) {
        return ms::error();
    }

    return std::nullopt;
}

void ms::setProjData(const char * proj_data)
{
    msSetPROJ_DATA(proj_data, NULL);
}
