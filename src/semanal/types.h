#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "source.h"
#include "variant.h"

namespace acu::types {
struct TypeId {
    std::uint32_t index {0};

    bool operator==(TypeId other) const { return index == other.index; }
};

constexpr TypeId None {0};
constexpr TypeId Nothing {1};
constexpr TypeId Bool {2};

constexpr TypeId Int8 {3};
constexpr TypeId Int16 {4};
constexpr TypeId Int32 {5};
constexpr TypeId Int64 {6};
constexpr TypeId Int {6};

constexpr TypeId UInt8 {7};
constexpr TypeId UInt16 {8};
constexpr TypeId UInt32 {9};
constexpr TypeId UInt64 {10};
constexpr TypeId UInt {10};

constexpr TypeId Float32 {11};
constexpr TypeId Float64 {12};
constexpr TypeId Float {12};

constexpr TypeId Const {13};

enum class Specifier : std::uint8_t { None, Let, Var, Val };

struct SpecType {
    TypeId type;
    Specifier specifier = Specifier::None;

    bool operator==(const SpecType& other) const {
        if(specifier == Specifier::None || other.specifier == Specifier::None) {
            return type == other.type;
        }
        return type == other.type && specifier == other.specifier;
    }
};

struct Type {
    struct None {};
    struct Nothing {};
    struct Bool {};
    struct Int {
        std::uint8_t bits;
        bool is_signed;
    };
    enum class Float : std::uint8_t { F32 = 32, F64 = 64 };

    struct Func {
        std::vector<SpecType> params;
        SpecType return_type;
    };

    struct Array {
        SpecType item;
        std::uint64_t length;
    };

    struct Ptr {
        SpecType type;
    };

    struct StructField {
        std::string_view name;
        SpecType type;
    };

    struct Struct {
        std::string_view name;
        std::vector<StructField> fields;
        const Source* source;
        Location location;
    };

    struct Const {};

    utils::Variant<
        None,
        Nothing,
        Bool,
        Int,
        Float,
        Func,
        Array,
        Ptr,
        Struct,
        Const>
        data;
};

class TypePool {
public:
    TypePool();
    TypeId add_int(std::uint8_t bits, bool is_signed = true);
    TypeId add_func(const Type::Func& func);
    TypeId add_array(SpecType item, std::uint64_t length);
    TypeId add_ptr(SpecType type);
    TypeId add_struct(const Type::Struct& struct_def);
    void set_struct_fields(TypeId type, std::vector<Type::StructField> fields);
    [[nodiscard]] std::string to_string(TypeId id) const;
    [[nodiscard]] std::string to_string(SpecType type) const;

    [[nodiscard]] bool is_int(TypeId type) const {
        return types_[type.index].data.is<Type::Int>();
    }

    [[nodiscard]] const Type& get(TypeId id) const { return types_[id.index]; }
    [[nodiscard]] size_t type_count() const { return types_.size(); }

private:
    template <class T>
    static void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct TypeIdHash {
        std::size_t operator()(TypeId id) const {
            return std::hash<std::uint32_t> {}(id.index);
        }
    };

    struct SpecTypeHash {
        std::size_t operator()(const SpecType& type) const {
            std::size_t result = 0;
            hash_combine(result, type.type.index);
            hash_combine(result, type.specifier);
            return result;
        }
    };

    struct SpecTypeEqual {
        bool operator()(const SpecType& type1, const SpecType& type2) const {
            return type1.type == type2.type &&
                   type1.specifier == type2.specifier;
        }
    };

    struct IntHash {
        std::size_t operator()(Type::Int type) const {
            std::size_t result = 0;
            hash_combine(result, type.bits);
            hash_combine(result, type.is_signed);
            return result;
        }
    };

    struct IntEqual {
        bool operator()(Type::Int type1, Type::Int type2) const {
            return type1.bits == type2.bits &&
                   type1.is_signed == type2.is_signed;
        }
    };

    struct FuncHash {
        std::size_t operator()(const Type::Func& func) const {
            std::size_t result = 0;
            for (const auto& param : func.params) {
                hash_combine(result, SpecTypeHash {}(param));
            }
            hash_combine(result, SpecTypeHash {}(func.return_type));
            return result;
        }
    };

    struct FuncEqual {
        bool operator()(
            const Type::Func& func1, const Type::Func& func2
        ) const {
            return func1.params == func2.params &&
                   func1.return_type == func2.return_type;
        }
    };

    struct ArrayHash {
        std::size_t operator()(const Type::Array& type) const {
            std::size_t result = 0;
            hash_combine(result, SpecTypeHash {}(type.item));
            hash_combine(result, type.length);
            return result;
        }
    };

    struct ArrayEqual {
        bool operator()(
            const Type::Array& array1, const Type::Array& array2
        ) const {
            return array1.item == array2.item && array1.length == array2.length;
        }
    };

    std::vector<Type> types_;
    std::unordered_map<Type::Int, TypeId, IntHash, IntEqual> ints_;
    std::unordered_map<Type::Func, TypeId, FuncHash, FuncEqual> funcs_;
    std::unordered_map<Type::Array, TypeId, ArrayHash, ArrayEqual> arrays_;
    std::unordered_map<SpecType, TypeId, SpecTypeHash, SpecTypeEqual> ptrs_;
    std::unordered_map<std::string_view, TypeId> structs_;
};

}
