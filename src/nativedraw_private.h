#ifndef _NATIVE_DRAW_PRIVATE_H
#define _NATIVE_DRAW_PRIVATE_H

#include "nativedraw.h"

#include <unordered_map>

namespace ND_NAMESPACE {

// From boost::hash_combine
template <class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

// Desc must have a hash() function.
// Rsrc must be a copyable type; it would be unwise for Rsrc to be
// a heavyweight object. (Presumably Rsrc is a pointer or a handle)
template <typename Desc, typename Rsrc>
class ResourceManager
{
public:
    using CreateFunc = std::function<Rsrc(const Desc&, float)>;  // create(desc, dpi)
    using DestroyFunc = std::function<void(Rsrc)>;               // destroy(rsrc)

    // Virtual functions are not available in base class
    // destructors, so need to pass the functions as function objs
    ResourceManager(CreateFunc create, DestroyFunc destroy)
        : mCreate(create), mDestroy(destroy)
    {}

    virtual ~ResourceManager() {
        for (auto &hash_rsrc : mHash2Rsrc) {
            mDestroy(hash_rsrc.second);
        }
    }

    Rsrc get(const Desc& desc, float dpi)
    {
        HashType hash = desc.hash();
        hash_combine(hash, dpi);
        auto it = mHash2Rsrc.find(hash);
        if (it == mHash2Rsrc.end()) {
            mHash2Rsrc[hash] = mCreate(desc, dpi);
            it = mHash2Rsrc.find(hash);
        }
        return it->second;
    }

protected:
    using HashType = std::size_t;
    std::unordered_map<HashType, Rsrc> mHash2Rsrc;
    CreateFunc mCreate;
    DestroyFunc mDestroy;
};

} // namespace $ND_NAMESPACE
#endif // _NATIVE_DRAW_PRIVATE_H
