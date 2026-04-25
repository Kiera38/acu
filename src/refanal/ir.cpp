#include "ir.h"

#include "project.h"

namespace acu::refanal::ir {
std::string_view UsedFunc::name(const Project& project) const {
    return project.module(module).func(func).name();
}
const Source& UsedFunc::source(const Project& project) const {
    return project.module(module).func(func).source();
}
std::string UsedFunc::mangle_name(const Project& project) const {
    return project.module(module).func(func).mangle_name();
}
Location UsedFunc::location(const Project& project) const {
    return project.module(module).func(func).location();
}
bool UsedFunc::is_extern(const Project& project) const {
    return project.module(module).func(func).is_extern();
}
const Local& UsedFunc::param(const Project& project, LocalRef ref) const {
    return project.module(module).func(func).param(ref);
}
IndexSpan<const Local, LocalRef> UsedFunc::params(
    const Project& project
) const {
    return project.module(module).func(func).params();
}
types::SpecType UsedFunc::return_type(const Project& project) const {
    return project.module(module).func(func).return_type();
}
}