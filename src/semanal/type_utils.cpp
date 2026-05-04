#include "semanal/type_utils.h"

#include "semanal/types.h"
#include "type_utils.h"

namespace acu::semanal {

bool can_convert(
    types::TypeId from, types::TypeId to, const types::TypePool& pool
) {
    if (from == to || from == types::Nothing) {
        return true;
    }

    const auto& from_data = pool.get(from).data;
    const auto& to_data = pool.get(to).data;

    if (auto f_int = from_data.get_if<types::Type::Int>()) {
        if (auto t_int = to_data.get_if<types::Type::Int>()) {
            if (f_int->is_signed == t_int->is_signed) {
                return f_int->bits <= t_int->bits;
            }
            if (!f_int->is_signed && t_int->is_signed) {
                return f_int->bits < t_int->bits;
            }
            if (f_int->is_signed && !t_int->is_signed) {
                return t_int->bits > f_int->bits;
            }
        }
        if (to_data.is<types::Type::Bool>() ||
            to_data.is<types::Type::Float>()) {
            return true;
        }
    }

    if (auto f_uint = from_data.get_if<types::Type::Int>();
        f_uint && !f_uint->is_signed) {
        if (to_data.is<types::Type::Bool>() ||
            to_data.is<types::Type::Float>()) {
            return true;
        }
    }

    if (auto f_float = from_data.get_if<types::Type::Float>()) {
        if (auto t_float = to_data.get_if<types::Type::Float>()) {
            return static_cast<uint8_t>(*f_float) <=
                   static_cast<uint8_t>(*t_float);
        }
    }

    return false;
}

bool can_cast(
    types::TypeId from, types::TypeId to, const types::TypePool& pool
) {
    if (can_convert(from, to, pool)) {
        return true;
    }

    const auto& from_tp = pool.get(from);
    const auto& to_tp = pool.get(to);

    if (from_tp.data.is<types::Type::Int>() &&
        to_tp.data.is<types::Type::Int>()) {
        return true;
    }

    if (from_tp.data.is<types::Type::Float>() &&
        to_tp.data.is<types::Type::Float>()) {
        return true;
    }

    if (from_tp.data.is<types::Type::Int>() &&
        to_tp.data.is<types::Type::Ptr>()) {
        return true;
    }
    if (from_tp.data.is<types::Type::Ptr>() &&
        to_tp.data.is<types::Type::Int>()) {
        return true;
    }

    if (auto at = from_tp.data.get_if<types::Type::Array>()) {
        if (auto pt = to_tp.data.get_if<types::Type::Ptr>()) {
            if (at->item == pt->type) {
                return true;
            }
        }
    }

    return false;
}

std::optional<types::TypeId> unify(
    types::TypeId type1, types::TypeId type2, const types::TypePool& pool
) {
    if (type1 == type2) {
        return type1;
    }
    if (can_convert(type1, type2, pool)) {
        return type2;
    }
    if (can_convert(type2, type1, pool)) {
        return type1;
    }
    return std::nullopt;
}

types::TypeId TypeVar::add_type(
    types::SpecType tp,
    Location loc,
    const Source& source,
    const types::TypePool& pool,
    ErrorHandler& err_handler
) {
    if (locked) {
        if (type.has_value() && type->type != tp.type) {
            if (!can_convert(tp.type, type->type, pool)) {
                report_error(
                    tp.type, type->type, loc, source, pool, err_handler, false
                );
            }
        }
    } else {
        if (type.has_value()) {
            auto unified = unify(type->type, tp.type, pool);
            if (!unified.has_value()) {
                report_error(
                    tp.type, type->type, loc, source, pool, err_handler, true
                );
            } else {
                type->type = *unified;
            }
        } else {
            type = tp;
            define_loc = loc;
        }
    }

    if (!locked_spec && tp.specifier != types::Specifier::None) {
        if (type->specifier == types::Specifier::None) {
            type->specifier = tp.specifier;
        } else if (
            tp.specifier == types::Specifier::Var &&
            type->specifier == types::Specifier::Let
        ) {
            type->specifier = types::Specifier::Var;
        } else if (
            tp.specifier == types::Specifier::Val &&
            type->specifier != types::Specifier::Val
        ) {
            type->specifier = types::Specifier::Val;
        }
    }

    return type->type;
}

void TypeVar::lock(
    types::SpecType tp,
    Location loc,
    const Source& source,
    const types::TypePool& pool,
    ErrorHandler& err_handler
) {
    if (locked) {
        err_handler.error(
            source,
            loc,
            std::format("Internal error: Type variable already locked")
        );
        return;
    }
    if (type.has_value() && !can_convert(type->type, tp.type, pool)) {
        report_error(
            tp.type, type->type, loc, source, pool, err_handler, false
        );
    }
    type = tp;
    locked = true;
    if (tp.specifier != types::Specifier::None) {
        locked_spec = true;
    }
    if (!define_loc.has_value()) {
        define_loc = loc;
    }
}

void TypeVar::lock(
    types::TypeId tp,
    Location loc,
    const Source& source,
    const types::TypePool& pool,
    ErrorHandler& err_handler
) {
    lock(
        {.type = tp, .specifier = types::Specifier::None},
        loc,
        source,
        pool,
        err_handler
    );
}

types::TypeId TypeVar::union_tp(
    const TypeVar& other,
    Location loc,
    const Source& source,
    const types::TypePool& pool,
    ErrorHandler& err_handler
) {
    if (other.type.has_value()) {
        return add_type(*other.type, loc, source, pool, err_handler);
    }
    return type.has_value() ? type->type : types::None;
}

types::SpecType TypeVar::get() const {
    if (!type.has_value()) {
        return {.type = types::None, .specifier = types::Specifier::None};
    }
    return *type;
}

void TypeVar::report_error(
    types::TypeId from,
    types::TypeId to,
    Location loc,
    const Source& source,
    const types::TypePool& pool,
    ErrorHandler& err_handler,
    bool is_unification
) const {
    std::string hint = "";
    if (can_cast(from, to, pool)) {
        hint = std::format(
            "use 'as {}' for explicit conversion", pool.to_string(to)
        );
    }
    std::vector<Label> labels;
    if (define_loc.has_value()) {
        labels.push_back({
            .source = &source,
            .location = *define_loc,
            .message =
                std::format("type established here as {}", pool.to_string(to)),
        });
    }
    std::string message = is_unification
                              ? std::format(
                                    "Type mismatch: cannot unify {} and {}",
                                    pool.to_string(from),
                                    pool.to_string(to)
                                )
                              : std::format(
                                    "Type mismatch: cannot convert {} to {}",
                                    pool.to_string(from),
                                    pool.to_string(to)
                                );
    err_handler.error(source, loc, std::move(message), hint, std::move(labels));
}

}
