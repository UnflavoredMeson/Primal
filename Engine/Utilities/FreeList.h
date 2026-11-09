// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "CommonHeaders.h"

namespace primal::utl
{

#if USE_STL_VECTOR
#pragma message("WARNING: using utl::free_list with std::vector results in duplicate calls to class destructor!")
#endif

template <typename T>
class free_list
{
    static_assert(sizeof(T) >= sizeof(u32));

  public:
    free_list() = default;
    explicit free_list(u32 count)
    {
        _array.reserve(count);
    }

    ~free_list()
    {
        assert(!_size);
#if USE_STL_VECTOR
        memset(_array.data(), 0, _array.size() * sizeof(T));
#endif
    }

    template <class... params>
    constexpr auto add(params &&...p) -> u32
    {
        u32 id{u32_invalid_id};
        if (_next_free_index == u32_invalid_id)
        {
            id = (u32)_array.size();
            _array.emplace_back(std::forward<params>(p)...);
        }
        else
        {
            id = _next_free_index;
            assert(id < _array.size() && already_removed(id, true));
            _next_free_index = *(const u32 *const)std::addressof(_array[id]);
            new (std::addressof(_array[id])) T(std::forward<params>(p)...);
        }
        ++_size;
        return id;
    }

    constexpr auto remove(u32 id) -> void
    {
        assert(id < _array.size() && !already_removed(id, false));
        T &item{_array[id]};
        item.~T();
        // Fill the destroyed item's storage with a debug pattern (void* cast: T needn't be trivially copyable).
        DEBUG_OP(std::memset((void *)std::addressof(_array[id]), 0xcc, sizeof(T)));
        *(u32 *const)std::addressof(_array[id]) = _next_free_index;
        _next_free_index = id;
        --_size;
    }

    constexpr auto size() const -> u32
    {
        return _size;
    }

    constexpr auto capacity() const -> u32
    {
        return (u32)_array.size();
    }

    constexpr auto empty() const -> bool
    {
        return _size == 0;
    }

    [[nodiscard]] constexpr auto operator[](u32 id) -> T &
    {
        assert(id < _array.size() && !already_removed(id, false));
        return _array[id];
    }

    [[nodiscard]] constexpr auto operator[](u32 id) const -> const T &
    {
        assert(id < _array.size() && !already_removed(id, false));
        return _array[id];
    }

  private:
    constexpr auto already_removed(u32 id, bool return_value_when_sizeof_t_equals_4) const -> bool
    {
        // NOTE: when sizeof(T) == sizeof(u32) we can't test if the item was already removed!
        if constexpr (sizeof(T) > sizeof(u32))
        {
            u32 i{sizeof(u32)}; // skip the first 4 bytes.
            const u8 *const p{(const u8 *const)std::addressof(_array[id])};
            // NOTE: test the bound first, p[sizeof(T)] is past the item (past the array for the last one).
            while ((i < sizeof(T)) && (p[i] == 0xcc))
                ++i;
            return i == sizeof(T);
        }
        else
        {
            return return_value_when_sizeof_t_equals_4;
        }
    }
#if USE_STL_VECTOR
    utl::vector<T> _array;
#else
    utl::vector<T, false> _array;
#endif
    u32 _next_free_index{u32_invalid_id};
    u32 _size{0};
};
}
