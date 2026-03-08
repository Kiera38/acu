#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "source.h"

namespace acu {

enum class Severity : std::uint8_t { Note, Warning, Error, Fatal };

struct Error {
    Severity severity;
    Location location;
    std::string message;
    std::string hint;
};

class ErrorHandler {
public:
    void report(
        Severity severity,
        Location location,
        std::string message,
        std::string hint = ""
    ) {
        errors_.push_back(
            {severity, location, std::move(message), std::move(hint)}
        );
        if (severity == Severity::Error || severity == Severity::Fatal) {
            has_errors_ = true;
        }
    }

    void error(Location location, std::string message, std::string hint = "") {
        report(Severity::Error, location, std::move(message), std::move(hint));
    }

    void warning(
        Location location, std::string message, std::string hint = ""
    ) {
        report(
            Severity::Warning, location, std::move(message), std::move(hint)
        );
    }

    void note(Location location, std::string message, std::string hint = "") {
        report(Severity::Note, location, std::move(message), std::move(hint));
    }

    [[nodiscard]] bool has_errors() const { return has_errors_; }
    [[nodiscard]] const std::vector<Error>& errors() const { return errors_; }

    void emit_all(const Source& source) const;

private:
    std::vector<Error> errors_;
    bool has_errors_ = false;
};

}
