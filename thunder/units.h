#pragma once
// Compile-time helpers shared by the engine and the screen: log2 of a literal
// in Q16, folded by the compiler. No tables: each translation unit may
// include it without duplicating data.
#include <opxy/types.hpp>

namespace thunder {
namespace ct {
constexpr double ln2 = 0.6931471805599453;
constexpr double series(double y2, double term, int k) { return k > 81 ? 0.0 : term / k + series(y2, term * y2, k + 2); }
constexpr double ln_unit(double x) { return 2.0 * series(((x - 1) / (x + 1)) * ((x - 1) / (x + 1)), (x - 1) / (x + 1), 1); }
constexpr double ln(double x) { return x >= 2.0 ? ln2 + ln(x / 2.0) : x < 1.0 ? -ln2 + ln(x * 2.0) : ln_unit(x); }
constexpr opxy::i32 round(double v) { return v >= 0 ? static_cast<opxy::i32>(v + 0.5) : -static_cast<opxy::i32>(-v + 0.5); }
}
// log2(x) in Q16, for a positive literal.
constexpr opxy::i32 L(double x) { return ct::round(ct::ln(x) / ct::ln2 * 65536.0); }
}
