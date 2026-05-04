#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "source.h"

namespace acu {

enum class Severity : std::uint8_t { Note, Warning, Error, Fatal };

struct Label {
    const Source* source;
    Location location;
    std::string message;
};

struct Error {
    Severity severity;
    const Source* source;
    Location location;
    std::string message;
    std::string hint;
    std::vector<Label> labels;
};

class ErrorHandler {
public:
    void report(
        Severity severity,
        const Source& source,
        Location location,
        std::string message,
        std::string hint = "",
        std::vector<Label> labels = {}
    ) {
        errors_.push_back({
            .severity = severity,
            .source = &source,
            .location = location,
            .message = std::move(message),
            .hint = std::move(hint),
            .labels = std::move(labels),
        });
        if (severity == Severity::Error || severity == Severity::Fatal) {
            has_errors_ = true;
        }
    }

    void error(
        const Source& source,
        Location location,
        std::string message,
        std::string hint = "",
        std::vector<Label> labels = {}
    ) {
        report(
            Severity::Error,
            source,
            location,
            std::move(message),
            std::move(hint),
            std::move(labels)
        );
    }

    void warning(
        const Source& source,
        Location location,
        std::string message,
        std::string hint = "",
        std::vector<Label> labels = {}
    ) {
        report(
            Severity::Warning,
            source,
            location,
            std::move(message),
            std::move(hint),
            std::move(labels)
        );
    }

    void note(
        const Source& source,
        Location location,
        std::string message,
        std::string hint = "",
        std::vector<Label> labels = {}
    ) {
        report(
            Severity::Note,
            source,
            location,
            std::move(message),
            std::move(hint),
            std::move(labels)
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
