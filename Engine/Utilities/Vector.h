// Copyright (c) Arash Khatami
// Distributed under the MIT license. See the LICENSE file in the project root for more information.
#pragma once
#include "../Common/CommonDefinitions.h"
#include "../Common/PrimitiveTypes.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

namespace primal::utl
{

// A vector class similar to std::vector with basic functionality.
// The user can specify in the template argument whether they want
// elements' destructor to be called when being removed or while
// clearing/destructing the vector.
template <typename T, bool destruct = true>
class vector
{
    // Items are relocated (when growing or erasing) with realloc()/memcpy() unless that would break
    // them: types that aren't trivially copyable but have a move constructor are move-constructed
    // instead. E.g. std::string/std::wstring and std::unordered_map in libstdc++ hold pointers into
    // themselves; MSVC's happen to survive a byte copy. Types that can't be moved (DISABLE_MOVE)
    // keep the bitwise relocation the engine relies on, and so does vector<T, false> (used by
    // free_list), whose storage may contain destroyed items.
    static constexpr bool relocate_with_move{
        destruct && !std::is_trivially_copyable_v<T> && std::is_move_constructible_v<T>};

  public:
    // Default constructor. Doesn't allocate memory.
    constexpr vector() = default;

    // Constructor resizes the vector and initializes 'count' items.
    constexpr explicit vector(u64 count)
    {
        resize(count);
    }

    // Constructor resizes the vector and initializes 'count' items using 'value'.
    constexpr explicit vector(u64 count, const T &value)
    {
        resize(count, value);
    }

    // Copy-constructor. Constructs by copying another vector. The items
    // in the copied vector must be copyable.
    constexpr vector(const vector &o)
    {
        *this = o;
    }

    // Move-constructor. Constructs by moving another vector.
    // The original vector will be empty after move.
    constexpr vector(vector &&o) noexcept
        : _capacity{o._capacity}
        , _size{o._size}
        , _data{o._data}
    {
        o.reset();
    }

    // Copy-assignment operator. Clears this vector and copies items
    // from another vector. The items must be copyable.
    constexpr auto operator=(const vector &o) -> vector &
    {
        assert(this != std::addressof(o));
        if (this != std::addressof(o))
        {
            clear();
            reserve(o._size);
            for (const auto &item : o)
            {
                emplace_back(item);
            }
            assert(_size == o._size);
        }

        return *this;
    }

    // Move-assignment operator. Frees all resources in this vector and
    // moves the other vector into this one.
    constexpr auto operator=(vector &&o) noexcept -> vector &
    {
        assert(this != std::addressof(o));
        if (this != std::addressof(o))
        {
            destroy();
            move(o);
        }

        return *this;
    }

    // Destructs the vector and its items as specified in template argument
    ~vector()
    {
        destroy();
    }

    // Inserts an item at the end of the vector by copying 'value'.
    constexpr auto push_back(const T &value) -> void
    {
        emplace_back(value);
    }

    // Inserts an item at the end of the vector by moving 'value'.
    constexpr auto push_back(T &&value) -> void
    {
        emplace_back(std::move(value));
    }

    // Copy- or move-constructs an item at the end of the vector.
    template <typename... params>
    constexpr auto emplace_back(params &&...p) -> decltype(auto)
    {
        if (_size == _capacity)
        {
            reserve(((_capacity + 1) * 3) >> 1); // reserve 50% more
        }
        assert(_size < _capacity);

        T *const item{new (std::addressof(_data[_size])) T(std::forward<params>(p)...)};
        ++_size;
        return *item;
    }

    // Resizes the vector and initializes new items with their default value.
    constexpr auto resize(u64 new_size) -> void
    {
        static_assert(std::is_default_constructible_v<T>, "Type must be default-constructible.");

        if (new_size > _size)
        {
            reserve(new_size);
            while (_size < new_size)
            {
                emplace_back();
            }
        }
        else if (new_size < _size)
        {
            if constexpr (destruct)
            {
                destruct_range(new_size, _size);
            }

            _size = new_size;
        }

        // Do nothing if new_size == _size.
        assert(new_size == _size);
    }

    // Resizes the vector and initializes new items by copying 'value'.
    constexpr auto resize(u64 new_size, const T &value) -> void
    {
        static_assert(std::is_copy_constructible_v<T>, "Type must be copy-constructible.");

        if (new_size > _size)
        {
            reserve(new_size);
            while (_size < new_size)
            {
                emplace_back(value);
            }
        }
        else if (new_size < _size)
        {
            if constexpr (destruct)
            {
                destruct_range(new_size, _size);
            }

            _size = new_size;
        }

        // Do nothing if new_size == _size.
        assert(new_size == _size);
    }

    // Allocates memory to contain the specified number of items.
    constexpr auto reserve(u64 new_capacity) -> void
    {
        if (new_capacity > _capacity)
        {
            if constexpr (relocate_with_move)
            {
                T *const new_buffer{static_cast<T *>(std::malloc(new_capacity * sizeof(T)))};
                assert(new_buffer);
                if (new_buffer)
                {
                    for (u64 i{0}; i < _size; ++i)
                    {
                        new (std::addressof(new_buffer[i])) T(std::move(_data[i]));
                        _data[i].~T();
                    }
                    std::free(_data);
                    _data = new_buffer;
                    _capacity = new_capacity;
                }
            }
            else
            {
                // NOTE: realoc() will automatically copy the data in the buffer
                //       if a new region of memory is allocated.
                // void* cast: deliberate bitwise relocation (see relocate_with_move; GCC -Wclass-memaccess).
                void *new_buffer{std::realloc(static_cast<void *>(_data), new_capacity * sizeof(T))};
                assert(new_buffer);
                if (new_buffer)
                {
                    _data = static_cast<T *>(new_buffer);
                    _capacity = new_capacity;
                }
            }
        }
    }

    // Removes the item at specified index.
    constexpr auto erase(u64 index) -> T *
    {
        assert(_data && index < _size);
        return erase(std::addressof(_data[index]));
    }

    // Removes the item at specified location.
    constexpr auto erase(T *const item) -> T *
    {
        assert(_data && item >= std::addressof(_data[0]) && item < std::addressof(_data[_size]));
        if constexpr (destruct)
        {
            item->~T();
        }
        --_size;
        if (item < std::addressof(_data[_size]))
        {
            if constexpr (relocate_with_move)
            {
                for (T *at{item}; at < std::addressof(_data[_size]); ++at)
                {
                    new (at) T(std::move(*(at + 1)));
                    (at + 1)->~T();
                }
            }
            else
            {
                // void* casts: deliberate bitwise relocation (see relocate_with_move; clang -Wnontrivial-memcall).
                std::memmove(
                    static_cast<void *>(item),
                    static_cast<const void *>(item + 1),
                    (std::addressof(_data[_size]) - item) * sizeof(T));
            }
        }

        return item;
    }

    // Same as erase() but faster because it just copies the last item.
    constexpr auto erase_unordered(u64 index) -> T *
    {
        assert(_data && index < _size);
        return erase_unordered(std::addressof(_data[index]));
    }

    // Same as erase() but faster because it just copies the last item.
    constexpr auto erase_unordered(T *const item) -> T *
    {
        assert(_data && item >= std::addressof(_data[0]) && item < std::addressof(_data[_size]));
        if constexpr (destruct)
        {
            item->~T();
        }
        --_size;
        if (item < std::addressof(_data[_size]))
        {
            if constexpr (relocate_with_move)
            {
                new (item) T(std::move(_data[_size]));
                _data[_size].~T();
            }
            else
            {
                // void* casts: deliberate bitwise relocation (see relocate_with_move; clang -Wnontrivial-memcall).
                std::memcpy(
                    static_cast<void *>(item), static_cast<const void *>(std::addressof(_data[_size])), sizeof(T));
            }
        }

        return item;
    }

    // Clears the vector and destructs items as specified in template argument.
    constexpr auto clear() -> void
    {
        if constexpr (destruct)
        {
            destruct_range(0, _size);
        }
        _size = 0;
    }

    // Swaps two vectors
    constexpr auto swap(vector &o) noexcept -> void
    {
        if (this != std::addressof(o))
        {
            std::swap(_capacity, o._capacity);
            std::swap(_size, o._size);
            std::swap(_data, o._data);
        }
    }

    // Pointer to the start of data. Might be null.
    [[nodiscard]] constexpr auto data() -> T *
    {
        return _data;
    }

    // Pointer to the start of data. Might be null.
    [[nodiscard]] constexpr auto data() const -> T *
    {
        return _data;
    }

    // Returns true if vector is empty.
    [[nodiscard]] constexpr auto empty() const -> bool
    {
        return _size == 0;
    }

    // Return the number of items in the vector.
    [[nodiscard]] constexpr auto size() const -> u64
    {
        return _size;
    }

    // Returns the current capacity of the vector.
    [[nodiscard]] constexpr auto capacity() const -> u64
    {
        return _capacity;
    }

    // Indexing operator. Returns a reference to the item at specified index.
    [[nodiscard]] constexpr auto operator[](u64 index) -> T &
    {
        assert(_data && index < _size);
        return _data[index];
    }

    // Indexing operator. Returns a constant reference to the item at specified index.
    [[nodiscard]] constexpr auto operator[](u64 index) const -> const T &
    {
        assert(_data && index < _size);
        return _data[index];
    }

    // Returns a reference to the first item. Will fault the application if called
    // when the vector is empty.
    [[nodiscard]] constexpr auto front() -> T &
    {
        assert(_data && _size);
        return _data[0];
    }

    // Returns a constant reference to the first item. Will fault the application
    //  if called when the vector is empty.
    [[nodiscard]] constexpr auto front() const -> const T &
    {
        assert(_data && _size);
        return _data[0];
    }

    // Returns a reference to the last item. Will fault the application if called
    // when the vector is empty.
    [[nodiscard]] constexpr auto back() -> T &
    {
        assert(_data && _size);
        return _data[_size - 1];
    }

    // Returns a constant reference to the last item. Will fault the application
    //  if called when the vector is empty.
    [[nodiscard]] constexpr auto back() const -> const T &
    {
        assert(_data && _size);
        return _data[_size - 1];
    }

    // Returns a pointer to the first item. Returns null when vector is empty.
    [[nodiscard]] constexpr auto begin() -> T *
    {
        return std::addressof(_data[0]);
    }

    // Returns a constant pointer to the first item. Returns null when vector is empty.
    [[nodiscard]] constexpr auto begin() const -> const T *
    {
        return std::addressof(_data[0]);
    }

    // Returns a pointer to the last item. Returns null when vector is empty.
    [[nodiscard]] constexpr auto end() -> T *
    {
        assert(!(_data == nullptr && _size > 0));
        return std::addressof(_data[_size]);
    }

    // Returns a constant pointer to the last item. Returns null when vector is empty.
    [[nodiscard]] constexpr auto end() const -> const T *
    {
        assert(!(_data == nullptr && _size > 0));
        return std::addressof(_data[_size]);
    }

  private:
    constexpr auto move(vector &o) -> void
    {
        _capacity = o._capacity;
        _size = o._size;
        _data = o._data;
        o.reset();
    }

    constexpr auto reset() -> void
    {
        _capacity = 0;
        _size = 0;
        _data = nullptr;
    }

    constexpr auto destruct_range(u64 first, u64 last) -> void
    {
        assert(destruct);
        assert(first <= _size && last <= _size && first <= last);
        if (_data)
        {
            for (; first != last; ++first)
            {
                _data[first].~T();
            }
        }
    }

    constexpr auto destroy() -> void
    {
        assert([&] { return _capacity ? _data != nullptr : _data == nullptr; }());
        clear();
        _capacity = 0;
        if (_data)
        {
            std::free(_data);
        }
        _data = nullptr;
    }

    u64 _capacity{0};
    u64 _size{0};
    T *_data{nullptr};
};
}
