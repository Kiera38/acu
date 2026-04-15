#include "types.h"

#include <format>
#include <ranges>
#include <string>
#include <vector>

#include "ir.h"

namespace acu::types {

std::string_view Type::UsedStruct::name() const {
    return pool->get(type).data.get<Type::Struct>().name;
}

std::span<const Type::StructField> Type::UsedStruct::fields() const {
    return pool->get(type).data.get<Type::Struct>().fields;
}

const Source& Type::UsedStruct::source() const {
    return *pool->get(type).data.get<Type::Struct>().source;
}

Location Type::UsedStruct::location() const {
    return pool->get(type).data.get<Type::Struct>().location;
}

TypePool::TypePool() {
    types_.push_back({Type::None {}});
    types_.push_back({Type::Nothing {}});
    types_.push_back({Type::Bool {}});
    add_int(8);
    add_int(16);
    add_int(32);
    add_int(64);

    add_int(8, false);
    add_int(16, false);
    add_int(32, false);
    add_int(64, false);

    types_.push_back({Type::Float::F32});
    types_.push_back({Type::Float::F64});

    types_.push_back({Type::Const {}});
}

TypeId TypePool::add_int(std::uint8_t bits, bool is_signed) {
    if (auto it = ints_.find({bits, is_signed}); it != ints_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({Type::Int {.bits = bits, .is_signed = is_signed}});
    auto [it, inserted] =
        ints_.insert({{.bits = bits, .is_signed = is_signed}, id});
    return it->second;
}

TypeId TypePool::add_func(const Type::Func& func) {
    if (auto it = funcs_.find(func); it != funcs_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({func});
    auto [it, inserted] = funcs_.insert({func, id});
    return it->second;
}

TypeId TypePool::add_array(SpecType item, std::uint64_t length) {
    Type::Array array {.item = item, .length = length};
    if (auto it = arrays_.find(array); it != arrays_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({array});
    auto [it, inserted] = arrays_.insert({array, id});
    return it->second;
}

TypeId TypePool::add_ptr(SpecType type) {
    if (auto it = ptrs_.find(type); it != ptrs_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({Type::Ptr {type}});
    auto [it, inserted] = ptrs_.insert({type, id});
    return it->second;
}

TypeId TypePool::add_struct(const Type::Struct& struct_def) {
    if (auto it = structs_.find(struct_def.name); it != structs_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({struct_def});
    auto [it, inserted] = structs_.insert({struct_def.name, id});
    return it->second;
}

TypeId TypePool::add_used_struct(Type::UsedStruct used_struct) {
    if (auto it = used_structs_.find(used_struct); it != used_structs_.end()) {
        return it->second;
    }
    TypeId id {static_cast<std::uint32_t>(types_.size())};
    types_.push_back({used_struct});
    auto [it, inserted] = used_structs_.insert({used_struct, id});
    return it->second;
}

void TypePool::set_struct_fields(
    TypeId type, std::vector<Type::StructField> fields
) {
    types_[type.index].data.get<Type::Struct>().fields = std::move(fields);
}

std::string TypePool::to_string(TypeId id) const {
    const auto& type = get(id);
    return type.data.visit(
        [&](Type::None) -> std::string { return "None"; },
        [&](Type::Nothing) -> std::string { return "Nothing"; },
        [&](Type::Bool) -> std::string { return "Bool"; },
        [&](Type::Int data) -> std::string {
            return std::format(
                "{}{}", data.is_signed ? "Int" : "UInt", data.bits
            );
        },
        [&](Type::Float data) -> std::string {
            return data == Type::Float::F32 ? "Float32" : "Float64";
        },
        [&](const Type::Func& data) -> std::string {
            std::string result = "func(";
            for (size_t i = 0; i < data.params.size(); ++i) {
                result += to_string(data.params[i]);
                if (i + 1 < data.params.size()) {
                    result += ", ";
                }
            }
            result += ") -> " + to_string(data.return_type);
            return result;
        },
        [&](const Type::Array& data) -> std::string {
            return std::format(
                "Array[{}, {}]", to_string(data.item), data.length
            );
        },
        [&](const Type::Ptr& data) -> std::string {
            return std::format("Ptr[{}]", to_string(data.type));
        },
        [&](const Type::Struct& data) -> std::string {
            return std::string(data.name);
        },
        [&](const Type::UsedStruct data) -> std::string {
            return data.pool->to_string(data.type);
        },
        [&](Type::Const) -> std::string { return "struct"; }
    );
}

std::string specifier_to_string(Specifier specifier) {
    switch (specifier) {
        case Specifier::None: return "";
        case Specifier::Let: return "let";
        case Specifier::Var: return "var";
        case Specifier::Val: return "val";
    }
    return "";
}

std::string TypePool::to_string(SpecType type) const {
    if (type.specifier == Specifier::None) {
        return to_string(type.type);
    }
    return std::format(
        "{} {}", specifier_to_string(type.specifier), to_string(type.type)
    );
}

TypeId TypePool::copy(const TypePool& pool, TypeId type) {
    if (type.index <= Const.index) {
        return type;
    }
    return pool.get(type).data.visit(
        [&](const Type::Func& func) {
            std::vector<SpecType> params;
            params.reserve(func.params.size());
            for (auto param : func.params) {
                params.push_back(copy(pool, param));
            }
            return add_func({
                .params = std::move(params),
                .return_type = copy(pool, func.return_type),
            });
        },
        [&](const Type::Array& array) {
            return add_array(copy(pool, array.item), array.length);
        },
        [&](const Type::Ptr& ptr) {
            return add_ptr(copy(pool, ptr.type));
        },
        [&](const Type::Struct&) {
            return add_used_struct({.pool = &pool, .type = type});
        },
        [&](const Type::UsedStruct& used_struct) {
            return add_used_struct(used_struct);
        },
        [&](const auto&) { return type; }
    );
}

}

namespace acu::ir {
types::TypeId Package::func_type(FuncRef ref) {
    const auto& func = funcs_[ref];
    auto params =
        func.params() |
        std::views::transform([](const Param& param) { return param.type; }) |
        std::ranges::to<std::vector<types::SpecType>>();
    types::Type::Func type {
        .params = std::move(params), .return_type = func.return_type()
    };
    return types_.add_func(type);
}
}
