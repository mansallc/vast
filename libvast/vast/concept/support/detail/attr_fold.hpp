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

#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <type_traits>

namespace vast {

struct unused_type;

namespace detail {

template <class Attribute>
struct attr_fold : std::decay<Attribute> {};

template <>
struct attr_fold<std::vector<char>> : std::decay<std::string> {};

template <>
struct attr_fold<unused_type> : std::decay<unused_type> {};

template <>
struct attr_fold<std::vector<unused_type>> : std::decay<unused_type> {};

template <>
struct attr_fold<std::tuple<char, char>> : std::decay<std::string> {};

template <>
struct attr_fold<std::tuple<char, std::string>> : std::decay<std::string> {};

template <>
struct attr_fold<std::tuple<std::string, char>> : std::decay<std::string> {};

template <class T>
using attr_fold_t = typename attr_fold<T>::type;

} // namespace detail
} // namespace vast
