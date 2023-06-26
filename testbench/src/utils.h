#pragma once
#include <cmath>
#include <limits>
#include <iostream>

bool IsLess(double a, double b, double tolerance=std::numeric_limits<float>::epsilon()) {
    return (!(std::abs(a - b) <= tolerance) && a < b);
}