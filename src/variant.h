#pragma once

#include <type_traits>
#include <variant>

namespace acu::utils {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

template <typename... T>
struct Variant {
    std::variant<T...> value;
    // Constructor from any compatible type (perfect forwarding)
    template <typename U>
        requires(
            std::is_constructible_v<std::variant<T...>, U> &&
            !std::is_same_v<std::decay_t<U>, Variant>
        )
    Variant(U&& u) : value(std::forward<U>(u)) {}
    Variant() = default;

    // Check if holds alternative of type U
    template <typename U>
    [[nodiscard]] constexpr bool is() const {
        return std::holds_alternative<U>(value);
    }

    // Get reference to held value of type U (throws if wrong type)
    template <typename U>
    U& get() {
        return std::get<U>(value);
    }

    // Get const reference to held value of type U (throws if wrong type)
    template <typename U>
    const U& get() const {
        return std::get<U>(value);
    }

    // Get pointer to held value of type U (returns nullptr if wrong type)
    template <typename U>
    [[nodiscard]] constexpr U* get_if() {
        return std::get_if<U>(&value);
    }

    template <typename U>
    [[nodiscard]] constexpr const U* get_if() const {
        return std::get_if<U>(&value);
    }

    // Visit with a single function
    template <typename Func>
    auto visit(Func&& func) & {
        return std::visit(std::forward<Func>(func), value);
    }

    template <typename Func>
    auto visit(Func&& func) const& {
        return std::visit(std::forward<Func>(func), value);
    }

    template <typename Func>
    auto visit(Func&& func) && {
        return std::visit(std::forward<Func>(func), std::move(value));
    }

    template <typename Func>
    auto visit(Func&& func) const&& {
        return std::visit(std::forward<Func>(func), std::move(value));
    }

    // Visit with multiple functions (overloaded)
    template <typename... Func>
    auto visit(Func&&... funcs) & {
        return std::visit(Overloaded {std::forward<Func>(funcs)...}, value);
    }

    template <typename... Func>
    auto visit(Func&&... funcs) const& {
        return std::visit(Overloaded {std::forward<Func>(funcs)...}, value);
    }

    template <typename... Func>
    auto visit(Func&&... funcs) && {
        return std::visit(
            Overloaded {std::forward<Func>(funcs)...}, std::move(value)
        );
    }

    template <typename... Func>
    auto visit(Func&&... funcs) const&& {
        return std::visit(
            Overloaded {std::forward<Func>(funcs)...}, std::move(value)
        );
    }

    // Index of the currently held alternative
    [[nodiscard]] constexpr std::size_t index() const noexcept {
        return value.index();
    }

    // Check if valueless by exception
    [[nodiscard]] constexpr bool valueless_by_exception() const noexcept {
        return value.valueless_by_exception();
    }

    // Emplace a new value
    template <typename U, typename... Args>
    U& emplace(Args&&... args) {
        return value.template emplace<U>(std::forward<Args>(args)...);
    }

    template <typename U, typename... Args>
    U& emplace(std::initializer_list<U> il, Args&&... args) {
        return value.template emplace<U>(il, std::forward<Args>(args)...);
    }

    // Helper method to safely get value with fallback
    template <typename U>
    U get_or(U&& fallback) const {
        if (is<U>()) {
            return std::get<U>(value);
        }
        return std::forward<U>(fallback);
    }

    // Helper method to apply function if type matches
    template <typename U, typename Func>
    bool apply_if(Func&& func) {
        if (is<U>()) {
            std::invoke(std::forward<Func>(func), std::get<U>(value));
            return true;
        }
        return false;
    }

    // Equality comparison
    bool operator==(const Variant& other) const { return value == other.value; }

    // Inequality comparison
    bool operator!=(const Variant& other) const { return value != other.value; }
};
}  // namespace acu::utils
