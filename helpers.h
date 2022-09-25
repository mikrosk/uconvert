/*
 * uconvert: bitmap converter into Atari ST/STE/TT/Falcon-specific format
 *
 * Copyright (c) 2022 Miro Kropacek <miro.kropacek@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

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
