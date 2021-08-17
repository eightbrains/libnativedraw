//-----------------------------------------------------------------------------
// Copyright 2021 Eight Brains Studios, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#ifndef _NATIVE_DRAW_PRIVATE_H
#define _NATIVE_DRAW_PRIVATE_H

#include "nativedraw.h"

#include <functional>
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

struct BezierPath::Impl
{
    struct Command
    {
        enum Action { kMoveTo = 0, kLineTo, kQuadraticTo, kCubicTo, kClose };

        Action cmd;
        Point p1;
        Point p2;
        Point p3;

        explicit Command(Action c) : cmd(c) { assert(c == kClose); }

        Command(Action c, const Point& p)
            : cmd(c), p1(p)
        {
            assert(c == kMoveTo || c == kLineTo);
        }

        Command(Action c, const Point& p1_, const Point& p2_)
            : cmd(c), p1(p1_), p2(p2_)
        {
            assert(c == kQuadraticTo);
        }

        Command(Action c, const Point& p1_, const Point& p2_, const Point& p3_)
            : cmd(c), p1(p1_), p2(p2_), p3(p3_)
        {
            assert(c == kCubicTo);
        }
    };

    std::vector<Command> commands;
};

// Returns an array such that out[i] where i is an index into a UTF-16 string,
// gives the index into utf8. Multibyte UTF-16 characters have the same index
// for each byte, which eliminates the need for error checking in the unfortunate
// event of a bug that results in lookup up in the middle of a character.
std::vector<int> utf8IndicesForUTF16Indices(const char *utf8);

} // namespace $ND_NAMESPACE
#endif // _NATIVE_DRAW_PRIVATE_H
