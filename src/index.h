#pragma once

#include <concepts>
#include <ranges>
#include <span>
#include <vector>

namespace acu {
template <typename T>
concept Index = requires(T i) {
    { i.index } -> std::convertible_to<std::size_t>;
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
    I operator[](difference_type n) const { return I {static_cast<decltype(I::index)>(index_ + n)}; }

    IndexIterator& operator++() { ++index_; return *this; }
    IndexIterator operator++(int) { IndexIterator tmp = *this; ++index_; return tmp; }
    IndexIterator& operator--() { --index_; return *this; }
    IndexIterator operator--(int) { IndexIterator tmp = *this; --index_; return tmp; }

    IndexIterator& operator+=(difference_type n) { index_ += n; return *this; }
    IndexIterator& operator-=(difference_type n) { index_ -= n; return *this; }
    IndexIterator operator+(difference_type n) const { return IndexIterator(index_ + n); }
    IndexIterator operator-(difference_type n) const { return IndexIterator(index_ - n); }
    difference_type operator-(const IndexIterator& other) const { return index_ - other.index_; }

    bool operator==(const IndexIterator& other) const { return index_ == other.index_; }
    bool operator!=(const IndexIterator& other) const { return index_ != other.index_; }
    bool operator<(const IndexIterator& other) const { return index_ < other.index_; }
    bool operator<=(const IndexIterator& other) const { return index_ <= other.index_; }
    bool operator>(const IndexIterator& other) const { return index_ > other.index_; }
    bool operator>=(const IndexIterator& other) const { return index_ >= other.index_; }

private:
    std::size_t index_;
};


template <Index I>
class IndexRange {
public:
    using index_iterator = IndexIterator<I>;

    IndexRange(index_iterator b, index_iterator e) : begin_(b), end_(e) {}

    index_iterator begin() const { return begin_; }
    index_iterator end() const { return end_; }

private:
    index_iterator begin_;
    index_iterator end_;
};

template <typename T, Index I>
class IndexSpan {
public:
    using value_type = T;
    using index_type = I;

    IndexSpan() = default;
    IndexSpan(T* data, std::size_t size) : span_(data, size) {}
    IndexSpan(std::span<T> span) : span_(span) {}

    [[nodiscard]] T& operator[](I i) const { return span_[i.index]; }
    [[nodiscard]] T& at(I i) const { return span_.at(i.index); }

    [[nodiscard]] std::size_t size() const { return span_.size(); }
    [[nodiscard]] bool empty() const { return span_.empty(); }

    auto begin() { return span_.begin(); }
    auto end() { return span_.end(); }
    auto begin() const { return span_.begin(); }
    auto end() const { return span_.end(); }

    using index_iterator = IndexIterator<I>;
    using index_range = IndexRange<I>;

    index_iterator index_begin() const { return index_iterator(0); }
    index_iterator index_end() const { return index_iterator(span_.size()); }

    index_range indices() const { return index_range(index_iterator(0), index_iterator(span_.size())); }

    [[nodiscard]] T& front() const { return span_.front(); }
    [[nodiscard]] T& back() const { return span_.back(); }

    [[nodiscard]] I last_index() const {
        return I {static_cast<decltype(I::index)>(span_.size() - 1)};
    }

    [[nodiscard]] std::span<T> data() const { return span_; }

    IndexSpan<T, I> subspan(I start, std::size_t count = std::dynamic_extent) const {
        return IndexSpan<T, I>(span_.subspan(start.index, count));
    }

private:
    std::span<T> span_;
};

template <typename T, Index I>
class IndexVector {
public:
    using value_type = T;
    using index_type = I;

    IndexVector() = default;
    IndexVector(std::size_t size, T items) : vec(size, items) {}

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

    T& operator[](I i) { return vec[i.index]; }
    const T& operator[](I i) const { return vec[i.index]; }

    T& at(I i) { return vec.at(i.index); }
    const T& at(I i) const { return vec.at(i.index); }

    [[nodiscard]] std::size_t size() const { return vec.size(); }
    [[nodiscard]] bool empty() const { return vec.empty(); }

    auto begin() const { return vec.begin(); }
    auto end() const { return vec.end(); }

    using index_iterator = IndexIterator<I>;
    using index_range = IndexRange<I>;

    index_iterator index_begin() const { return index_iterator(0); }
    index_iterator index_end() const { return index_iterator(vec.size()); }

    index_range indices() { return index_range(index_iterator(0), index_iterator(vec.size())); }
    index_range indices() const { return index_range(index_iterator(0), index_iterator(vec.size())); }

    T& front() { return vec.front(); }
    const T& front() const { return vec.front(); }
    T& back() { return vec.back(); }
    const T& back() const { return vec.back(); }

    [[nodiscard]] I last_index() const {
        return I {static_cast<decltype(I::index)>(vec.size() - 1)};
    }

    void clear() { vec.clear(); }
    void reserve(std::size_t n) { vec.reserve(n); }
    void resize(std::size_t n) { vec.resize(n); }

    template <std::ranges::range R>
    I append_range(R&& range) {
        auto index = static_cast<decltype(I::index)>(vec.size());
        vec.append_range(std::forward<R>(range));
        return I {index};
    }

    IndexSpan<T, I> data() { return IndexSpan<T, I>(vec); }
    IndexSpan<const T, I> data() const { return IndexSpan<const T, I>(vec); }

private:
    std::vector<T> vec;
};

}
