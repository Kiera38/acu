#pragma once

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

    Variant(std::variant<T...> val) : value(std::move(val)) {}

    template <typename U>
    [[nodiscard]] bool is() const {
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
    [[nodiscard]] U* get_if() {
        return std::get_if<U>(&value);
    }

    template <typename U>
    [[nodiscard]] const U* get_if() const {
        return std::get_if<U>(&value);
    }

    template <typename Func>
    auto visit(Func&& func) {
        return std::visit(std::forward<Func>(func), value);
    }

    template <typename Func>
    auto visit(Func&& func) const {
        return std::visit(std::forward<Func>(func), value);
    }

    template <typename... Func>
    auto visit(Func&&... funcs) {
        return std::visit(Overloaded {std::forward<Func>(funcs)...}, value);
    }

    template <typename... Func>
    auto visit(Func&&... funcs) const {
        return std::visit(Overloaded {std::forward<Func>(funcs)...}, value);
    }
};
}  // namespace acu::utils
