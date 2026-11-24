#include "speaker_calculator.h"

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if !defined(ESP_PLATFORM) || CONFIG_XIAOZHI_SPEAKER_ACCOUNT

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {
constexpr size_t kMaxExpressionLength = 64;

bool IsOperator(char key) { return key == '+' || key == '-' || key == '*' || key == '/'; }

bool IsDigit(char key) { return key >= '0' && key <= '9'; }

size_t OperandStart(std::string_view expression) {
    for (size_t i = expression.size(); i > 0; --i) {
        // Scientific notation can only come from chaining a formatted result.
        if (IsOperator(expression[i - 1]) && (i < 2 || expression[i - 2] != 'e')) {
            return i;
        }
    }
    return 0;
}

SpeakerCalculator::Result Evaluate(const char* expression) {
    using Status = SpeakerCalculator::Status;
    double sum = 0;
    double term = 0;
    char op = '+';
    const char* next = expression;
    while (*next) {
        char* end;
        double number = std::strtod(next, &end);
        if (end == next) {  // A pending leading sign has no value yet.
            break;
        }
        if (!std::isfinite(number)) {
            return {Status::OutOfRange};
        }
        if (*end == '%') {
            number /= 100;
            ++end;
        }
        // Keep multiplication/division in the current term before adding it to the sum.
        switch (op) {
            case '+':
                sum += term;
                term = number;
                break;
            case '-':
                sum += term;
                term = -number;
                break;
            case '*':
                term *= number;
                break;
            case '/':
                if (number == 0) {
                    return {Status::DivisionByZero};
                }
                term /= number;
                break;
        }
        if (!std::isfinite(sum) || !std::isfinite(term)) {
            return {Status::OutOfRange};
        }
        op = *end;
        if (!IsOperator(op)) {
            break;
        }
        next = end + 1;
    }
    const double value = sum + term;
    return std::isfinite(value) ? SpeakerCalculator::Result{Status::Value, value}
                                : SpeakerCalculator::Result{Status::OutOfRange};
}
}  // namespace

std::string SpeakerCalculator::Format(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.12g", value == 0 ? 0.0 : value);
    return text;
}

bool SpeakerCalculator::Press(char key) {
    if (key == '=') {
        if (confirmed_ || IsOperator(expression_.back()) || preview_.status != Status::Value) {
            return false;
        }
        confirmed_ = true;
        return true;
    }

    const auto before = expression_;
    const bool was_confirmed = confirmed_;
    if (confirmed_) {
        if (IsOperator(key) || key == '%') {
            expression_ = Format(preview_.value);
        } else if (IsDigit(key) || key == '.') {
            expression_ = "0";
        } else if (key != 'C' && key != '\b') {
            return false;
        }
        confirmed_ = false;
    }

    Edit(key);
    if (was_confirmed || expression_ != before) {
        preview_ = Evaluate(expression_.c_str());
        return true;
    }
    return false;
}

void SpeakerCalculator::Edit(char key) {
    if (key == 'C') {
        expression_ = "0";
        return;
    }
    if (key == '\b') {
        expression_.pop_back();
        // Don't leave an incomplete exponent when editing a chained result.
        if (expression_.size() >= 2 && expression_[expression_.size() - 2] == 'e') {
            expression_.resize(expression_.size() - 2);
        }
        if (expression_.empty()) {
            expression_ = "0";
        }
        return;
    }

    const char last = expression_.back();
    const auto operand = std::string_view(expression_).substr(OperandStart(expression_));
    const bool can_append = expression_.size() < kMaxExpressionLength;
    if (IsOperator(key)) {
        const bool is_sign = key == '-' || key == '+';
        if (expression_ == "0" && is_sign) {
            expression_.back() = key;
        } else if (IsOperator(last)) {
            if (expression_.size() > 1 || is_sign) {
                expression_.back() = key;
            }
        } else if (can_append) {
            expression_ += key;
        }
    } else if (IsDigit(key)) {
        if (last != '%' && operand.find('e') == std::string_view::npos) {
            if (operand == "0") {
                expression_.back() = key;
            } else if (can_append) {
                expression_ += key;
            }
        }
    } else if (key == '.') {
        if (last != '%' && operand.find_first_of(".e") == std::string_view::npos) {
            if (operand.empty() && expression_.size() + 2 <= kMaxExpressionLength) {
                expression_ += "0.";
            } else if (!operand.empty() && can_append) {
                expression_ += key;
            }
        }
    } else if (key == '%' && !IsOperator(last) && last != '%' && can_append) {
        expression_ += key;
    }
}

#endif
