#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "parser/nodes.h"
#include "variant.h"

namespace acu::semanal {
struct Type;
namespace types {
struct Func;
struct Struct;
struct Ptr;
struct Array;
struct Float;
struct UInt;
struct Int;
struct Bool;
struct None;
struct Nothing;
}
enum class Qualifier : std::uint8_t { Unknown, Let, Var, Val };
struct QualType {
    Qualifier qual;
    const Type* type;

    [[nodiscard]] inline bool is_nothing() const;
    [[nodiscard]] inline bool is_none() const;
    [[nodiscard]] inline bool is_bool() const;
    [[nodiscard]] inline bool is_int() const;
    [[nodiscard]] inline bool is_uint() const;
    [[nodiscard]] inline bool is_float() const;
    [[nodiscard]] inline bool is_array() const;
    [[nodiscard]] inline bool is_ptr() const;
    [[nodiscard]] inline bool is_integer() const;
    [[nodiscard]] inline bool is_number() const;

    [[nodiscard]] bool has_qualifier() const {
        return qual != Qualifier::Unknown;
    }

    [[nodiscard]] inline types::Int as_int() const;
    [[nodiscard]] inline types::UInt as_uint() const;
    [[nodiscard]] inline types::Float as_float() const;
    [[nodiscard]] inline types::Array as_array() const;
    [[nodiscard]] inline types::Ptr as_ptr() const;
    [[nodiscard]] inline const types::Func& as_func() const;
    [[nodiscard]] inline const types::Struct& as_struct() const;

    [[nodiscard]] inline const types::Int* get_int() const;
    [[nodiscard]] inline const types::UInt* get_uint() const;
    [[nodiscard]] inline const types::Float* get_float() const;
    [[nodiscard]] inline const types::Array* get_array() const;
    [[nodiscard]] inline const types::Ptr* get_ptr() const;
    [[nodiscard]] inline const types::Func* get_func() const;
    [[nodiscard]] inline const types::Struct* get_struct() const;

    template <typename... Funcs>
    auto visit(Funcs&&... funcs) const;

    bool operator==(const QualType& other) const noexcept {
        return this->qual == other.qual && this->type == other.type;
    }
};
namespace types {
struct Nothing {};
struct None {};
struct Bool {};
struct Int {
    std::optional<std::uint8_t> bits;
};
struct UInt {
    std::optional<std::uint32_t> bits;
};
struct Float {
    enum class Size : std::uint8_t {
        F32 = 32,
        F64 = 64,
    };
    Size size = Size::F64;
};
struct Array {
    QualType type;
    std::uint64_t length;
};
struct Ptr {
    QualType type;
};
struct Func {
    struct Arg {
        std::string_view name;
        QualType type;
    };
    std::vector<Arg> args;
    QualType return_type;
    std::uint32_t min_pos_args;
    std::uint32_t max_pos_args;
};
struct Struct {
    struct Field {
        std::string_view name;
        QualType type;
    };
    const nodes::Struct* def;
    std::vector<Field> fields;
};
inline bool operator==(Nothing, Nothing) noexcept { return true; }
inline bool operator==(None, None) noexcept { return true; }
inline bool operator==(Bool, Bool) noexcept { return true; }
inline bool operator==(Int int1, Int int2) noexcept {
    return int1.bits == int2.bits;
}
inline bool operator==(UInt int1, UInt int2) noexcept {
    return int1.bits == int2.bits;
}
inline bool operator==(Float float1, Float float2) noexcept {
    return float1.size == float2.size;
}
inline bool operator==(Array arr1, Array arr2) noexcept {
    return arr1.type == arr2.type && arr1.length == arr2.length;
}
inline bool operator==(Ptr ptr1, Ptr ptr2) noexcept {
    return ptr1.type == ptr2.type;
}
inline bool operator==(const Func& func1, const Func& func2) noexcept {
    if (func1.args.size() != func2.args.size()) return false;
    if (func1.min_pos_args != func2.min_pos_args) return false;
    if (func1.max_pos_args != func2.max_pos_args) return false;
    for (const auto& [arg1, arg2] : std::views::zip(func1.args, func2.args)) {
        if (arg1.name != arg2.name) return false;
        if (arg1.type != arg2.type) return false;
    }
    return func1.return_type == func2.return_type;
}

inline bool operator==(const Struct& struct1, const Struct& struct2) noexcept {
    if (struct1.def != struct2.def) return false;
    return std::ranges::all_of(
        std::views::zip(struct1.fields, struct2.fields),
        [](const auto& fields) {
            const auto& [field1, field2] = fields;
            if (field1.name != field2.name) return false;
            if (field1.type != field2.type) return false;
            return true;
        }
    );
}

}
struct Type : utils::Variant<
                  types::Nothing,
                  types::None,
                  types::Bool,
                  types::Int,
                  types::UInt,
                  types::Float,
                  types::Array,
                  types::Ptr,
                  types::Func,
                  types::Struct> {};

bool QualType::is_nothing() const { return type->is<types::Nothing>(); }
bool QualType::is_none() const { return type->is<types::None>(); }
bool QualType::is_bool() const { return type->is<types::Bool>(); }
bool QualType::is_int() const { return type->is<types::Int>(); }
bool QualType::is_uint() const { return type->is<types::UInt>(); }
bool QualType::is_float() const { return type->is<types::Float>(); }
bool QualType::is_array() const { return type->is<types::Array>(); }
bool QualType::is_ptr() const { return type->is<types::Ptr>(); }
bool QualType::is_integer() const { return is_int() || is_uint(); }
bool QualType::is_number() const { return is_integer() || is_float(); }

types::Int QualType::as_int() const { return type->get<types::Int>(); }
types::UInt QualType::as_uint() const { return type->get<types::UInt>(); }
types::Float QualType::as_float() const { return type->get<types::Float>(); }
types::Array QualType::as_array() const { return type->get<types::Array>(); }
types::Ptr QualType::as_ptr() const { return type->get<types::Ptr>(); }
const types::Func& QualType::as_func() const {
    return type->get<types::Func>();
}
const types::Struct& QualType::as_struct() const {
    return type->get<types::Struct>();
}

const types::Int* QualType::get_int() const {
    return type->get_if<types::Int>();
}
const types::UInt* QualType::get_uint() const {
    return type->get_if<types::UInt>();
}
const types::Float* QualType::get_float() const {
    return type->get_if<types::Float>();
}
const types::Array* QualType::get_array() const {
    return type->get_if<types::Array>();
}
const types::Ptr* QualType::get_ptr() const {
    return type->get_if<types::Ptr>();
}
const types::Func* QualType::get_func() const {
    return type->get_if<types::Func>();
}
const types::Struct* QualType::get_struct() const {
    return type->get_if<types::Struct>();
}

template <typename... Funcs>
auto QualType::visit(Funcs&&... funcs) const {
    return type->visit(std::forward<Funcs>(funcs)...);
}

}
namespace acu {
template <>
struct hash<semanal::QualType> {
    std::size_t operator()(semanal::QualType type) const {
        std::size_t result = 0;
        hash_combine(result, type.qual);
        hash_combine(result, type.type);
        return result;
    }
};
template <>
struct hash<semanal::Type> {
    std::size_t operator()(const semanal::Type& type) const noexcept;
};

namespace semanal {
class TypePool {
public:
    TypePool();
    const Type* get(const Type& type);
    [[nodiscard]] const Type* get_nothing() const { return nothing_; }
    [[nodiscard]] const Type* get_none() const { return none_; }
    [[nodiscard]] const Type* get_bool() const { return bool_; }
    [[nodiscard]] const Type* get_int() const { return int_; }
    [[nodiscard]] const Type* get_uint() const { return uint_; }
    [[nodiscard]] const Type* get_float() const { return float_; }
    QualType get(Qualifier qual, const Type& type) {
        return {.qual = qual, .type = get(type)};
    }
    [[nodiscard]] QualType get_nothing(Qualifier qual) const {
        return {.qual = qual, .type = nothing_};
    }
    [[nodiscard]] QualType get_none(Qualifier qual) const {
        return {.qual = qual, .type = none_};
    }
    [[nodiscard]] QualType get_bool(Qualifier qual) const {
        return {.qual = qual, .type = bool_};
    }
    [[nodiscard]] QualType get_int(Qualifier qual) const {
        return {.qual = qual, .type = int_};
    }
    [[nodiscard]] QualType get_uint(Qualifier qual) const {
        return {.qual = qual, .type = uint_};
    }
    [[nodiscard]] QualType get_float(Qualifier qual) const {
        return {.qual = qual, .type = float_};
    }

private:
    std::unordered_set<Type, hash<Type>> types_;
    const Type* nothing_;
    const Type* none_;
    const Type* bool_;
    const Type* int_;
    const Type* uint_;
    const Type* float_;
};
}
}
