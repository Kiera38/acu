#pragma once

#include <optional>

#include "errors.h"
#include "semanal/types.h"

namespace acu::semanal {

bool can_convert(
    types::TypeId from, types::TypeId to, const types::TypePool& pool
);

bool can_cast(
    types::TypeId from, types::TypeId to, const types::TypePool& pool
);

std::optional<types::TypeId> unify(
    types::TypeId type1, types::TypeId type2, const types::TypePool& pool
);

struct TypeVar {
    std::optional<types::SpecType> type;
    std::optional<Location> define_loc;
    bool locked = false;
    bool locked_spec = false;

    types::TypeId add_type(
        types::SpecType tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    );

    void lock(
        types::SpecType tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    );

    void lock(
        types::TypeId tp,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    );

    types::TypeId union_tp(
        const TypeVar& other,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler
    );

    [[nodiscard]] bool defined() const { return type.has_value(); }

    [[nodiscard]] types::SpecType get() const;

private:
    void report_error(
        types::TypeId from,
        types::TypeId to,
        Location loc,
        const Source& source,
        const types::TypePool& pool,
        ErrorHandler& err_handler,
        bool is_unification
    ) const;
};

}
