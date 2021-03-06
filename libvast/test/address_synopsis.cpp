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

#define SUITE address_synopsis

#include "vast/address_synopsis.hpp"

#include <caf/actor_system.hpp>
#include <caf/actor_system_config.hpp>

#include <vast/address.hpp>
#include <vast/concept/hashable/hash_append.hpp>
#include <vast/concept/hashable/xxhash.hpp>
#include <vast/concept/parseable/to.hpp>
#include <vast/concept/parseable/vast/address.hpp>
#include <vast/load.hpp>
#include <vast/save.hpp>
#include <vast/si_literals.hpp>
#include <vast/synopsis.hpp>
#include <vast/synopsis_factory.hpp>
#include <vast/test/fixtures/actor_system.hpp>
#include <vast/test/synopsis.hpp>
#include <vast/test/test.hpp>
#include <vast/type.hpp>

using namespace std::string_literals;
using namespace caf;
using namespace vast;
using namespace vast::test;
using namespace vast::si_literals;

TEST(failed construction) {
  // If there's no type attribute with Bloom filter parameters present,
  // construction fails.
  auto x = make_address_synopsis<xxhash64>(address_type{}, caf::settings{});
  CHECK_EQUAL(x, nullptr);
}

namespace {

struct fixture : fixtures::deterministic_actor_system {
  fixture() {
    factory<synopsis>::add(address_type{}, make_address_synopsis<xxhash64>);
  }
  caf::settings opts;
};

} // namespace

FIXTURE_SCOPE(address_filter_synopsis_tests, fixture)

TEST(construction via custom factory) {
  using namespace nft;
  auto to_addr_view = [](std::string_view str) {
    return make_data_view(unbox(to<address>(str)));
  };
  // Minimally sized Bloom filter to test expected collisions.
  auto t = address_type{}.attributes({{"synopsis", "bloomfilter(1,0.1)"}});
  auto x = factory<synopsis>::make(t, opts);
  REQUIRE_NOT_EQUAL(x, nullptr);
  x->add(to_addr_view("192.168.0.1"));
  auto verify = verifier{x.get()};
  verify(to_addr_view("192.168.0.1"), {N, N, N, N, N, N, T, N, N, N, N, N});
  MESSAGE("collisions");
  verify(to_addr_view("192.168.0.6"), {N, N, N, N, N, N, F, N, N, N, N, N});
  verify(to_addr_view("192.168.0.11"), {N, N, N, N, N, N, T, N, N, N, N, N});
}

TEST(serialization with custom attribute type) {
  auto t = address_type{}.attributes({{"synopsis", "bloomfilter(1000,0.1)"}});
  CHECK_ROUNDTRIP_DEREF(factory<synopsis>::make(t, opts));
}

TEST(construction based on partition size) {
  opts["max-partition-size"] = 1_Mi;
  auto ptr = factory<synopsis>::make(address_type{}, opts);
  REQUIRE_NOT_EQUAL(ptr, nullptr);
  CHECK_ROUNDTRIP_DEREF(std::move(ptr));
}

FIXTURE_SCOPE_END()
