#include "type.h"

namespace acu::semanal {
TypePool::TypePool() {
    nothing_ = get({types::Nothing {}});
    none_ = get({types::None {}});
    bool_ = get({types::Bool {}});
    int_ = get({types::Int {}});
    uint_ = get({types::UInt {}});
    float_ = get({types::Float {}});
}

const Type* TypePool::get(const Type& type) {
    if (auto it = types_.find(type); it != types_.end()) {
        return &*it;
    }
    return &*types_.insert(type).first;
}
}

namespace acu {
std::size_t hash<semanal::Type>::operator()(
    const semanal::Type& type
) const noexcept {
    namespace types = semanal::types;
    std::size_t result = 0;
    hash_combine(result, type.index());
    type.visit(
        [&](types::Int int_) { hash_combine(result, int_.bits); },
        [&](types::UInt uint) { hash_combine(result, uint.bits); },
        [&](types::Float float_) {
            hash_combine(result, static_cast<std::size_t>(float_.size));
        },
        [&](types::Array array) {
            hash_combine(result, array.type);
            hash_combine(result, array.length);
        },
        [&](types::Ptr ptr) { hash_combine(result, ptr.type); },
        [&](const types::Func& func) {
            hash_combine(result, func.args.size());
            hash_combine(result, func.min_pos_args);
            hash_combine(result, func.max_pos_args);
            for (const auto& arg : func.args) {
                hash_combine(result, arg.name);
                hash_combine(result, arg.type);
            }
            hash_combine(result, func.return_type);
        },
        [&](const types::Struct& struct_) {
            hash_combine(result, struct_.def);
            for (const auto& field : struct_.fields) {
                hash_combine(result, field.name);
                hash_combine(result, field.type);
            }
        },
        [](auto) {}
    );
    return result;
}
}