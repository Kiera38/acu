#pragma once

#include <functional>

namespace acu {
template<typename T>
struct hash {
    std::size_t operator()(const T& v) const {
        return std::hash<T>{}(v);
    }
};

template <class T>
void hash_combine(std::size_t& seed, const T& v) {
    //using std::hash;
    hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

}