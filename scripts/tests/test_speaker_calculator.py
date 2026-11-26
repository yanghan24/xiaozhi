"""Drive the P4X calculator's input and arithmetic on the host."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

BOARD = (pathlib.Path(__file__).resolve().parents[2] / "main/boards/waveshare"
         / "esp32-p4-wifi6-touch-lcd")


@unittest.skipUnless(shutil.which("c++"), "requires a host C++ compiler")
class SpeakerCalculatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(tmp.cleanup)
        source = pathlib.Path(tmp.name) / "driver.cc"
        cls.binary = pathlib.Path(tmp.name) / "calculator"
        source.write_text(r'''
#include "speaker_calculator.h"
#include <iostream>

int main(int argc, char** argv) {
    SpeakerCalculator calculator;
    int confirmations = 0;
    for (const char* key = argv[1]; *key; ++key) {
        if (calculator.Press(*key) && calculator.Confirmed()) {
            ++confirmations;
        }
    }
    const auto result = calculator.Preview();
    std::cout << calculator.Expression() << '\n';
    switch (result.status) {
        case SpeakerCalculator::Status::Value:
            std::cout << SpeakerCalculator::Format(result.value);
            break;
        case SpeakerCalculator::Status::DivisionByZero:
            std::cout << "division by zero";
            break;
        case SpeakerCalculator::Status::OutOfRange:
            std::cout << "out of range";
            break;
    }
    std::cout << '\n' << confirmations << '\n';
}
''', encoding="utf-8")
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Werror", "-I", str(BOARD),
                        str(source), str(BOARD / "speaker_calculator.cc"),
                        "-o", str(cls.binary)], check=True)

    def check_keys(self, keys, expression, result, confirmations=0):
        actual = subprocess.check_output([self.binary, keys], text=True).splitlines()
        self.assertEqual(actual, [expression, result, str(confirmations)])

    def test_precedence_and_live_preview(self):
        self.check_keys("2+3*4/2-", "2+3*4/2-", "8")
        self.check_keys("-8/4*2+3-1=", "-8/4*2+3-1", "-2", 1)

    def test_percentage_applies_to_the_operand(self):
        self.check_keys("2+50%=", "2+50%", "2.5", 1)
        self.check_keys("200*15%%9.", "200*15%", "30")

    def test_decimal_input_and_leading_zero(self):
        self.check_keys("00.1.2+03*.5", "0.12+3*0.5", "1.62")

    def test_replacing_operator_and_erasing_operand(self):
        self.check_keys("7+*03\b2=", "7*2", "14", 1)
        self.check_keys("-2\b\b", "0", "0")
        self.check_keys("4+=", "4+", "4")

    def test_division_by_zero_is_not_committed(self):
        self.check_keys("8/0=", "8/0", "division by zero")
        self.check_keys("8/0=.5=", "8/0.5", "16", 1)

    def test_confirmation_chaining_and_fresh_input(self):
        self.check_keys("2+3==*4=", "5*4", "20", 2)
        self.check_keys("1-3=*4=", "-2*4", "-8", 2)
        self.check_keys("2+3=9", "9", "9", 1)
        self.check_keys("2+3=C", "0", "0", 1)

    def test_scientific_result_can_be_reused_or_erased(self):
        self.check_keys("1000000000000=*2=", "1e+12*2", "2e+12", 2)
        self.check_keys("1000000000000=*\b\b\b+2", "1+2", "3", 1)

    def test_input_and_numeric_ranges_are_bounded(self):
        self.check_keys("9" * 70, "9" * 64, "1e+64")
        keys = "1=" + ("*" + "1" + "0" * 49 + "=") * 7
        self.check_keys(keys, "1e+294*1" + "0" * 49, "out of range", 7)
