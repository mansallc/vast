/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

/// This file contains drop-in replacements for C++20's <bit> header. Once we
/// can switch to C++20, this file has no raison d'être.

#pragma once

#include <numeric>
#include <type_traits>

namespace vast::detail {

template <class T>
constexpr int countl_zero(T x) noexcept {
  constexpr auto d = std::numeric_limits<T>::digits;
  if (x == 0)
    return d;
  constexpr auto d_ull = std::numeric_limits<unsigned long long>::digits;
  constexpr auto d_ul = std::numeric_limits<unsigned long>::digits;
  constexpr auto d_u = std::numeric_limits<unsigned>::digits;
  if constexpr (d <= d_u) {
    constexpr int diff = d_u - d;
    return __builtin_clz(x) - diff;
  } else if constexpr (d <= d_ul) {
    constexpr int diff = d_ul - d;
    return __builtin_clzl(x) - diff;
  } else if constexpr (d <= d_ull) {
    constexpr int diff = d_ull - d;
    return __builtin_clzll(x) - diff;
  } else {
    static_assert(d <= (2 * d_ull));
    unsigned long long high = x >> d_ull;
    if (high != 0) {
      constexpr int diff = (2 * d_ull) - d;
      return __builtin_clzll(high) - diff;
    }
    constexpr auto max_ull = std::numeric_limits<unsigned long long>::max();
    unsigned long long low = x & max_ull;
    return (d - d_ull) + __builtin_clzll(low);
  }
}

template <class T>
constexpr bool ispow2(T x) noexcept {
  return x && ((x & (x - 1)) == 0);
}

template <class T>
constexpr T ceil2(T x) noexcept {
  constexpr auto d = std::numeric_limits<T>::digits;
  if (x == 0 || x == 1)
    return 1;
  auto shift_exponent = d - countl_zero((T)(x - 1u));
  using promoted_type = decltype(x << 1);
  if constexpr (!std::is_same_v<promoted_type, T>) {
    const int extra_exp = sizeof(promoted_type) / sizeof(T) / 2;
    shift_exponent |= (shift_exponent & d) << extra_exp;
  }
  return (T) 1u << shift_exponent;
}

template <class T>
constexpr T floor2(T x) noexcept {
  constexpr auto digits = std::numeric_limits<T>::digits;
  if (x == 0)
    return 0;
  return (T) 1u << (digits - countl_zero((T)(x >> 1)));
}

template <class T>
constexpr T log2p1(T x) noexcept {
  return std::numeric_limits<T>::digits - countl_zero(x);
}

} // namespace vast::detail
