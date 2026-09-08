#ifndef MAPSERV_API_CPP_H
#define MAPSERV_API_CPP_H

#if defined(MAPSERVER_LIBRARY)
#define MAPSERVER_API Q_DECL_EXPORT
#else
#define MAPSERVER_API Q_DECL_IMPORT
#endif

#include <string>

#include <IOVariant/variant/reprojector.h>

struct mapObj;
struct imageObj;

namespace io
{
class ItemContext;
class MapParams;
}; // namespace io

namespace ms
{

MAPSERVER_API std::string error();
MAPSERVER_API void cleanup();
MAPSERVER_API std::optional<std::string> init();
MAPSERVER_API void setProjData(const char * proj_data);

class MAPSERVER_API Image final
{
public:
    friend class Map;

    ~Image();
    Image(Image &) = delete;
    Image(Image &&) noexcept = default;
    Image & operator=(Image &) = delete;
    Image & operator=(Image &&) noexcept = default;

    int width() const;
    int height() const;
    void render(quint8 * bytebuffer) const;

    const std::string & error() const;
    bool isValid() const;

private:
    explicit Image(imageObj * img);
    explicit Image(std::string err);

    imageObj * _img;
    std::string _error;
};

struct MAPSERVER_API Catalog final {
    struct Legend {
        std::string _name;
        Image _icon;
        double _minscale;
        double _maxscale;
        Legend(Legend &) = delete;
        Legend(Legend &&) noexcept = default;
        Legend & operator=(Legend &) = delete;
        Legend & operator=(Legend &&) noexcept = default;
    };

    struct Layer {
        std::string _name;
        std::vector<Legend> _lagends;
        Layer(Layer &) = delete;
        Layer(Layer &&) noexcept = default;
        Layer & operator=(Layer &) = delete;
        Layer & operator=(Layer &&) noexcept = default;
    };

    std::vector<Layer> _layers;
    std::array<double, 4> _extent;
    std::string _error;

    Catalog(Catalog &) = delete;
    Catalog(Catalog &&) noexcept = default;
    Catalog & operator=(Catalog &) = delete;
    Catalog & operator=(Catalog &&) noexcept = default;
};

class MAPSERVER_API Map final
{
public:
    ~Map();
    Map(const Map & map);
    Map(Map && map) noexcept;
    Map & operator=(const Map & map);
    Map & operator=(Map && map) noexcept;

    static Map createFromFile(const std::string & filename);
    static Map createFromString(std::string mapstring);
    static Map createOfflineMap(Map map);

    Image draw(io::ItemContext * context, const io::MapParams & params) const;
    Image draw() const;
    Catalog catalog(int width, int height) const;
    std::string string() const;

    const std::string & error() const;
    bool isValid() const;

private:
    explicit Map(mapObj * map);

    mapObj * cloneMapObj(const mapObj * map);
    void swap(Map & m) noexcept;

    Image draw(mapObj * map) const;

    mapObj * _map = nullptr;
    std::string _error;
};

class MAPSERVER_API Reprojector : public io::Reprojector
{
public:
    Reprojector(int sridSource, int sridTarget);
    Reprojector(void * pSource, int sridSource, void * pTarget, int sridTarget);
    virtual ~Reprojector();

    virtual void reproject(double * x, double * y, double * z = nullptr) const override;

    virtual void setSridSource(int srid) override;
    virtual void setSridTarget(int srid) override;

    virtual bool isValid() const override;

private:
    std::unordered_map<int, void *> _projections;
    std::unordered_map<int, std::unordered_map<int, void *>> _reprojectors;
    void * _reprojector = nullptr;

    void set(int source, int target);
};

} // namespace ms

#endif /* MAPSERV_API_H */
