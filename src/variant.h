#pragma once

#include <type_traits>
#include <variant>

namespace acu::utils {

template <typename... T>
struct Overloaded : T... {
    using T::operator()...;
    // Explicit constructor to help some compilers
    Overloaded(T&&... args) : T(std::forward<T>(args))... {}
};

template <typename... T>
Overloaded(T...) -> Overloaded<T...>;

template <typename... T>
struct Variant {
    std::variant<T...> value;

    template <typename U>
        requires(
            std::is_constructible_v<std::variant<T...>, U> &&
            !std::is_same_v<std::decay_t<U>, Variant>
        )
    Variant(U&& u) : value(std::forward<U>(u)) {}
    Variant() = default;

    template <typename U>
    [[nodiscard]] constexpr bool is() const {
        return std::holds_alternative<U>(value);
    }
    template <typename U>
    U& get() {
        return std::get<U>(value);
    }
    template <typename U>
    const U& get() const {
        return std::get<U>(value);
    }
    template <typename U>
    [[nodiscard]] constexpr U* get_if() {
        return std::get_if<U>(&value);
    }
    template <typename U>
    [[nodiscard]] constexpr const U* get_if() const {
        return std::get_if<U>(&value);
    }

    // Simplified visit to help MSVC
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

    // Overloaded visit
    template <typename... Funcs>
    auto visit(Funcs&&... funcs) & {
        return std::visit(
            Overloaded<Funcs...> {std::forward<Funcs>(funcs)...}, value
        );
    }

    template <typename... Funcs>
    auto visit(Funcs&&... funcs) const& {
        return std::visit(
            Overloaded<Funcs...> {std::forward<Funcs>(funcs)...}, value
        );
    }

    template <typename... Funcs>
    auto visit(Funcs&&... funcs) && {
        return std::visit(
            Overloaded<Funcs...> {std::forward<Funcs>(funcs)...},
            std::move(value)
        );
    }

    template <typename... Funcs>
    auto visit(Funcs&&... funcs) const&& {
        return std::visit(
            Overloaded<Funcs...> {std::forward<Funcs>(funcs)...},
            std::move(value)
        );
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept {
        return value.index();
    }
    [[nodiscard]] constexpr bool valueless_by_exception() const noexcept {
        return value.valueless_by_exception();
    }

    template <typename U, typename... Args>
    U& emplace(Args&&... args) {
        return value.template emplace<U>(std::forward<Args>(args)...);
    }

    template <typename U, typename... Args>
    U& emplace(std::initializer_list<U> il, Args&&... args) {
        return value.template emplace<U>(il, std::forward<Args>(args)...);
    }

    template <typename U>
    U get_or(U&& fallback) const {
        if (is<U>()) return std::get<U>(value);
        return std::forward<U>(fallback);
    }

    template <typename U, typename Func>
    bool apply_if(Func&& func) {
        if (is<U>()) {
            std::invoke(std::forward<Func>(func), std::get<U>(value));
            return true;
        }
        return false;
    }

    bool operator==(const Variant& other) const { return value == other.value; }
    bool operator!=(const Variant& other) const { return value != other.value; }
};
}
