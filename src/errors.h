#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "source.h"

namespace acu {

enum class Severity : std::uint8_t { Note, Warning, Error, Fatal };
enum class LabelStyle : std::uint8_t { Primary, Secondary };

struct Label {
    LabelStyle style = LabelStyle::Primary;
    const Source* source;
    Location location;
    std::string message;
};

struct Error {
    Severity severity = Severity::Error;
    std::string message;
    std::vector<Label> labels;
    std::vector<std::string> notes;
};

class ErrorHandler {
public:
    void report(Error error) {
        if (error.severity == Severity::Error ||
            error.severity == Severity::Fatal) {
            has_errors_ = true;
        }
        errors_.push_back(std::move(error));
    }

    void error(
        const Source& source,
        const Location& location,
        const std::string& message
    ) {
        report(
            {.severity = Severity::Error,
             .message = message,
             .labels = {
                 {.source = &source, .location = location, .message = message}
             }}
        );
    }

    [[nodiscard]] bool has_errors() const { return has_errors_; }
    [[nodiscard]] const std::vector<Error>& errors() const { return errors_; }

    void emit_all() const;

private:
    std::vector<Error> errors_;
    bool has_errors_ = false;
};
}
