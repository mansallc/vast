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

#include "vast/error.hpp"

#include "vast/detail/assert.hpp"

#include <caf/pec.hpp>
#include <caf/sec.hpp>

#include <sstream>
#include <string>

namespace vast {
namespace {

const char* descriptions[] = {
  "no_error",
  "unspecified",
  "no_such_file",
  "filesystem_error",
  "type_clash",
  "unsupported_operator",
  "parse_error",
  "print_error",
  "convert_error",
  "invalid_query",
  "format_error",
  "end_of_input",
  "timeout",
  "stalled",
  "version_error",
  "syntax_error",
  "lookup_error",
  "logic_error",
  "invalid_table_slice_type",
  "invalid_synopsis_type",
  "remote_node_down",
  "invalid_argument",
  "invalid_result",
  "invalid_configuration",
  "unrecognized_option",
  "invalid_subcommand",
  "missing_subcommand",
  "missing_component",
  "unimplemented",
  "silent",
  "out_of_memory",
  "system_error",
};

static_assert(ec{std::size(descriptions)} == ec::ec_count,
              "Mismatch between number of error codes and descriptions");

void render_default_ctx(std::ostringstream& oss, const caf::message& ctx) {
  size_t size = ctx.size();
  if (size > 0) {
    oss << ":";
    for (size_t i = 0; i < size; ++i)
      oss << ' ' << ctx.stringify(i);
  }
}

} // namespace

const char* to_string(ec x) {
  auto index = static_cast<size_t>(x);
  VAST_ASSERT(index < sizeof(descriptions));
  return descriptions[index];
}

std::string render(caf::error err) {
  using caf::atom_uint;
  if (!err)
    return "";
  std::ostringstream oss;
  auto category = err.category();
  if (atom_uint(category) == atom_uint("vast") && err == ec::silent)
    return "";
  oss << "!! ";
  switch (atom_uint(category)) {
    default:
      oss << to_string(category);
      render_default_ctx(oss, err.context());
      break;
    case atom_uint("vast"):
      oss << to_string(static_cast<vast::ec>(err.code()));
      render_default_ctx(oss, err.context());
      break;
    case atom_uint("parser"):
      oss << to_string(static_cast<caf::pec>(err.code()));
      render_default_ctx(oss, err.context());
      break;
    case atom_uint("system"):
      oss << to_string(static_cast<caf::sec>(err.code()));
      render_default_ctx(oss, err.context());
      break;
  }
  return oss.str();
}

} // namespace vast
