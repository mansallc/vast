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

#include "vast/fbs/utils.hpp"

#include "vast/chunk.hpp"
#include "vast/error.hpp"

#include <optional>
#include <string_view>

namespace vast::fbs {

chunk_ptr release(flatbuffers::FlatBufferBuilder& builder) {
  size_t offset;
  size_t size;
  auto ptr = builder.ReleaseRaw(size, offset);
  auto deleter = [=]() { flatbuffers::DefaultAllocator::dealloc(ptr, size); };
  return chunk::make(size - offset, ptr + offset, deleter);
}

flatbuffers::Verifier make_verifier(span<const byte> xs) {
  auto data = reinterpret_cast<const uint8_t*>(xs.data());
  return flatbuffers::Verifier{data, xs.size()};
}

span<const byte> as_bytes(const flatbuffers::FlatBufferBuilder& builder) {
  auto data = reinterpret_cast<const byte*>(builder.GetBufferPointer());
  auto size = builder.GetSize();
  return {data, size};
}

} // namespace vast::fbs
