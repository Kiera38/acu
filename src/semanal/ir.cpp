#include "ir.h"

#include "project.h"


namespace acu::ir {

const Source& UsedFunc::source(const Project& project) const {
    return project.package(package).func(func).source();
}

Location UsedFunc::location(const Project& project) const {
    return project.package(package).func(func).location();
}

std::string_view UsedFunc::name(const Project& project) const {
    return project.package(package).func(func).name();
}

bool UsedFunc::is_extern(const Project& project) const {
    return project.package(package).func(func).is_extern();
}

Param UsedFunc::param(const Project& project, ParamRef ref) const {
    return project.package(package).func(func).param(ref);
}

IndexSpan<const Param, ParamRef> UsedFunc::params(
    const Project& project
) const {
    return project.package(package).func(func).params();
}

types::SpecType UsedFunc::return_type(const Project& project) const {
    return project.package(package).func(func).return_type();
}
}