#ifndef HELPERS_H
#define HELPERS_H

#include <cstddef>
#include <vector>

template<typename T>
static size_t sizeof_vector(const std::vector<T>& vec)
{
    return sizeof(T) * vec.size();
}

template<bool val, typename T>
struct bool_value {
    static constexpr bool value = val;
};

#endif // HELPERS_H
