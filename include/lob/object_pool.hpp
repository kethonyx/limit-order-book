#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace lob {

// Fixed-size object pool with an intrusive free list.
//
// Memory is allocated in chunks of `chunk_size` slots; acquire()/release() are
// O(1) and never touch the heap except when a new chunk is needed. Freed slots
// are reused LIFO, which also keeps recently used memory warm in cache.
// Addresses are stable for the lifetime of the pool.
//
// Trade-offs: memory is only returned to the OS when the pool is destroyed,
// and not thread-safe (the book is single-threaded).
template <class T>
class ObjectPool {
    // The pool does not track which slots are live, so it cannot run
    // destructors on teardown; restrict it to types that don't need them.
    static_assert(std::is_trivially_destructible_v<T>, "ObjectPool requires trivially destructible T");

public:
    explicit ObjectPool(std::size_t chunk_size = 4096) : chunk_size_(chunk_size) {}

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    // Moving transfers the chunks (slot addresses are unchanged) and leaves
    // the source empty; a defaulted move would leave it with a dangling free_.
    ObjectPool(ObjectPool&& other) noexcept
        : chunk_size_(other.chunk_size_),
          chunks_(std::move(other.chunks_)),
          free_(std::exchange(other.free_, nullptr)),
          in_use_(std::exchange(other.in_use_, 0)) {}

    ObjectPool& operator=(ObjectPool&& other) noexcept {
        if (this != &other) {
            chunk_size_ = other.chunk_size_;
            chunks_ = std::move(other.chunks_);
            other.chunks_.clear();
            free_ = std::exchange(other.free_, nullptr);
            in_use_ = std::exchange(other.in_use_, 0);
        }
        return *this;
    }
    ~ObjectPool() = default;

    template <class... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        if (free_ == nullptr) grow();
        Slot* slot = free_;
        free_ = slot->next;
        ++in_use_;
        return ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
    }

    void release(T* obj) noexcept {
        // T is trivially destructible, so the slot can be reused directly.
        Slot* slot = reinterpret_cast<Slot*>(obj);
        slot->next = free_;
        free_ = slot;
        --in_use_;
    }

    [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return chunks_.size() * chunk_size_; }

private:
    // A slot holds either a live T or, while free, the link to the next free slot.
    union Slot {
        Slot* next;
        alignas(T) std::byte storage[sizeof(T)];
    };

    void grow() {
        auto chunk = std::make_unique_for_overwrite<Slot[]>(chunk_size_);
        // Thread the new slots onto the free list.
        for (std::size_t i = 0; i < chunk_size_; ++i) {
            chunk[i].next = free_;
            free_ = &chunk[i];
        }
        chunks_.push_back(std::move(chunk));
    }

    std::size_t chunk_size_;
    std::vector<std::unique_ptr<Slot[]>> chunks_;
    Slot* free_ = nullptr;
    std::size_t in_use_ = 0;
};

}  // namespace lob
