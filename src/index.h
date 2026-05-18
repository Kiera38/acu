#pragma once

#include <concepts>
#include <ranges>
#include <span>
#include <vector>

#include "hash.h"

namespace acu {
template <typename T>
concept Index = requires(T i) {
    { i.index } -> std::convertible_to<std::size_t>;
};

template <Index I>
struct hash<I> {
    std::size_t operator()(I i) const {
        return std::hash<std::size_t> {}(i.index);
    }
};

template <Index I>
struct equal_to {
    bool operator()(I i1, I i2) const { return i1.index == i2.index; }
};

template <typename T>
struct Ref {
    std::uint32_t index;
    auto operator<=>(const Ref<T>&) const = default;
};

template <Index I>
class IndexIterator {
public:
    using difference_type = std::ptrdiff_t;
    using value_type = I;
    using pointer = const I*;
    using reference = const I&;
    using iterator_category = std::random_access_iterator_tag;

    IndexIterator(std::size_t idx) : index_(idx) {}

    I operator*() const { return I {static_cast<decltype(I::index)>(index_)}; }
    I operator[](difference_type n) const {
        return I {static_cast<decltype(I::index)>(index_ + n)};
    }

    IndexIterator& operator++() {
        ++index_;
        return *this;
    }
    IndexIterator operator++(int) {
        IndexIterator tmp = *this;
        ++index_;
        return tmp;
    }
    IndexIterator& operator--() {
        --index_;
        return *this;
    }
    IndexIterator operator--(int) {
        IndexIterator tmp = *this;
        --index_;
        return tmp;
    }

    IndexIterator& operator+=(difference_type n) {
        index_ += n;
        return *this;
    }
    IndexIterator& operator-=(difference_type n) {
        index_ -= n;
        return *this;
    }
    IndexIterator operator+(difference_type n) const {
        return IndexIterator(index_ + n);
    }
    IndexIterator operator-(difference_type n) const {
        return IndexIterator(index_ - n);
    }
    difference_type operator-(const IndexIterator& other) const {
        return index_ - other.index_;
    }

    bool operator==(const IndexIterator& other) const {
        return index_ == other.index_;
    }
    bool operator!=(const IndexIterator& other) const {
        return index_ != other.index_;
    }
    bool operator<(const IndexIterator& other) const {
        return index_ < other.index_;
    }
    bool operator<=(const IndexIterator& other) const {
        return index_ <= other.index_;
    }
    bool operator>(const IndexIterator& other) const {
        return index_ > other.index_;
    }
    bool operator>=(const IndexIterator& other) const {
        return index_ >= other.index_;
    }

private:
    std::size_t index_;
};

template <typename T, Index I = Ref<T>>
struct RefRange {
    std::uint32_t start;
    std::uint32_t size;

    using iterator = IndexIterator<I>;

    iterator begin() const { return iterator {start}; }
    iterator end() const { return iterator {start + size}; }

    [[nodiscard]] bool empty() const { return size == 0; }

    I operator[](size_t i) const {return {start + i};}
};

template <typename T, Index I = Ref<T>>
class IndexSpan {
public:
    using value_type = T;
    using index_type = I;

    IndexSpan() = default;
    IndexSpan(T* data, std::size_t size) : span_(data, size) {}
    IndexSpan(std::span<T> span) : span_(span) {}

    [[nodiscard]] decltype(auto) operator[](I i) const {
        return span_[i.index];
    }
    [[nodiscard]] decltype(auto) at(I i) const { return span_.at(i.index); }

    [[nodiscard]] std::size_t size() const { return span_.size(); }
    [[nodiscard]] bool empty() const { return span_.empty(); }

    auto begin() { return span_.begin(); }
    auto end() { return span_.end(); }
    auto begin() const { return span_.begin(); }
    auto end() const { return span_.end(); }

    using index_iterator = IndexIterator<I>;
    using index_range = RefRange<T, I>;

    index_iterator index_begin() const { return index_iterator(0); }
    index_iterator index_end() const { return index_iterator(span_.size()); }

    index_range indices() const { return index_range(0, span_.size()); }

    [[nodiscard]] decltype(auto) front() const { return span_.front(); }
    [[nodiscard]] decltype(auto) back() const { return span_.back(); }

    [[nodiscard]] I last_index() const {
        return I {static_cast<decltype(I::index)>(span_.size() - 1)};
    }

    [[nodiscard]] std::span<T> data() const { return span_; }

    IndexSpan<T, I> subspan(
        I start, std::size_t count = std::dynamic_extent
    ) const {
        return IndexSpan<T, I>(span_.subspan(start.index, count));
    }

private:
    std::span<T> span_;
};

template <typename T, Index I = Ref<T>>
class IndexVector {
public:
    using value_type = T;
    using index_type = I;

    IndexVector() = default;
    IndexVector(std::size_t size, T items) : vec(size, items) {}
    IndexVector(std::size_t size) : vec(size) {}
    template <std::input_iterator InputIt>
    IndexVector(InputIt first, InputIt last) : vec(first, last) {}

    I push_back(const T& val) {
        auto index = static_cast<decltype(I::index)>(vec.size());
        vec.push_back(val);
        return I {index};
    }

    I push_back(T&& val) {
        auto index = static_cast<decltype(I::index)>(vec.size());
        vec.push_back(std::move(val));
        return I {index};
    }

    template <typename... Args>
    I emplace_back(Args&&... args) {
        auto index = static_cast<decltype(I::index)>(vec.size());
        vec.emplace_back(std::forward<Args>(args)...);
        return I {index};
    }

    decltype(auto) operator[](I i) { return vec[i.index]; }
    decltype(auto) operator[](I i) const { return vec[i.index]; }

    decltype(auto) at(I i) { return vec.at(i.index); }
    decltype(auto) at(I i) const { return vec.at(i.index); }

    [[nodiscard]] std::size_t size() const { return vec.size(); }
    [[nodiscard]] bool empty() const { return vec.empty(); }

    auto begin() const { return vec.begin(); }
    auto end() const { return vec.end(); }
    auto begin() { return vec.begin(); }
    auto end() { return vec.end(); }

    using index_iterator = IndexIterator<I>;
    using index_range = RefRange<T, I>;

    index_iterator index_begin() const { return index_iterator(0); }
    index_iterator index_end() const { return index_iterator(vec.size()); }

    index_range indices() { return index_range(0, vec.size()); }
    index_range indices() const { return index_range(0, vec.size()); }

    decltype(auto) front() { return vec.front(); }
    decltype(auto) front() const { return vec.front(); }
    decltype(auto) back() { return vec.back(); }
    decltype(auto) back() const { return vec.back(); }

    [[nodiscard]] I last_index() const {
        return I {static_cast<decltype(I::index)>(vec.size() - 1)};
    }

    void clear() { vec.clear(); }
    void reserve(std::size_t n) { vec.reserve(n); }
    void resize(std::size_t n) { vec.resize(n); }
    void resize(std::size_t n, const T& i) { vec.resize(n, i); }

    template <std::ranges::range R>
    RefRange<T, I> append_range(R&& range) {
        auto start = static_cast<std::uint32_t>(vec.size());
        vec.append_range(std::forward<R>(range));
        return RefRange<T, I> {
            .start = start,
            .size = static_cast<std::uint32_t>(vec.size() - start)
        };
    }

    IndexSpan<T, I> data() { return IndexSpan<T, I>(vec); }
    IndexSpan<const T, I> data() const { return IndexSpan<const T, I>(vec); }

    std::span<const T> range(RefRange<T, I> range) const {
        return std::span(vec).subspan(range.start, range.size);
    }

    std::span<T> range(RefRange<T, I> range) {
        return std::span(vec).subspan(range.start, range.size);
    }

private:
    std::vector<T> vec;
};

template <Index I, typename T>
class IndexMap {
public:
    template <typename IT>
    IndexMap(RefRange<IT, I> range) : start(range.start), vec(range.size) {}
    template <typename IT>
    IndexMap(RefRange<IT, I> range, T i)
        : start(range.start), vec(range.size, i) {}

    decltype(auto) operator[](I i) { return vec[i.index - start]; }
    decltype(auto) operator[](I i) const { return vec[i.index - start]; }

    decltype(auto) at(I i) { return vec.at(i.index - start); }
    decltype(auto) at(I i) const { return vec.at(i.index - start); }

    [[nodiscard]] std::size_t size() const { return vec.size(); }

    auto begin() const { return vec.begin(); }
    auto end() const { return vec.end(); }
    auto begin() { return vec.begin(); }
    auto end() { return vec.end(); }

    using index_iterator = IndexIterator<I>;
    using index_range = RefRange<T, I>;

    index_iterator index_begin() const { return index_iterator(start); }
    index_iterator index_end() const { return index_iterator(vec.size()); }

    index_range indices() { return index_range(start, vec.size()); }
    index_range indices() const { return index_range(start, vec.size()); }

private:
    std::vector<T> vec;
    std::uint32_t start;
};

}
