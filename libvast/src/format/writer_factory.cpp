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

#include "vast/format/writer_factory.hpp"

#include "vast/config.hpp"
#include "vast/detail/make_io_stream.hpp"
#include "vast/format/ascii.hpp"
#include "vast/format/csv.hpp"
#include "vast/format/json.hpp"
#include "vast/format/null.hpp"
#include "vast/format/writer.hpp"
#include "vast/format/zeek.hpp"

#if VAST_HAVE_PCAP
#  include "vast/format/pcap.hpp"
#endif

#if VAST_HAVE_ARROW
#  include "vast/format/arrow.hpp"
#endif

namespace vast {

template <class Writer>
caf::expected<std::unique_ptr<format::writer>>
make_writer(const caf::settings& options) {
  using namespace std::string_literals;
  using defaults = typename Writer::defaults;
  using ostream_ptr = std::unique_ptr<std::ostream>;
  if constexpr (std::is_constructible_v<Writer, ostream_ptr>) {
    auto out = detail::make_output_stream<defaults>(options);
    if (!out)
      return out.error();
    return std::make_unique<Writer>(std::move(*out));
#if VAST_HAVE_PCAP
  } else if constexpr (std::is_same_v<Writer, format::pcap::writer>) {
    auto output
      = get_or(options, defaults::category + ".write"s, defaults::write);
    auto flush = get_or(options, defaults::category + ".flush-interval"s,
                        defaults::flush_interval);
    return std::make_unique<Writer>(output, flush);
#endif
  } else {
    return std::make_unique<Writer>();
  }
}

void factory_traits<format::writer>::initialize() {
  using namespace format;
  using fac = factory<writer>;
  fac::add("ascii", make_writer<ascii::writer>);
  fac::add("csv", make_writer<csv::writer>);
  fac::add("json", make_writer<format::json::writer>);
  fac::add("null", make_writer<null::writer>);
  fac::add("zeek", make_writer<zeek::writer>);
#if VAST_HAVE_PCAP
  fac::add("pcap", make_writer<pcap::writer>);
#endif
#if VAST_HAVE_ARROW
  fac::add("arrow", make_writer<arrow::writer>);
#endif
}

} // namespace vast
