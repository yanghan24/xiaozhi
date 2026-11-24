#pragma once

#include <string>

// Calculator input and arithmetic are independent of LVGL.
class SpeakerCalculator {
public:
    enum class Status { Value, DivisionByZero, OutOfRange };
    struct Result {
        Status status = Status::Value;
        double value = 0;
    };

    bool Press(char key);
    const std::string& Expression() const { return expression_; }
    Result Preview() const { return preview_; }
    bool Confirmed() const { return confirmed_; }
    static std::string Format(double value);

private:
    void Edit(char key);

    std::string expression_ = "0";
    Result preview_;
    bool confirmed_ = false;
};
