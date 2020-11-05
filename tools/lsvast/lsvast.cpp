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

#include "vast/chunk.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/uuid.hpp"
#include "vast/directory.hpp"
#include "vast/fbs/index.hpp"
#include "vast/fbs/partition.hpp"
#include "vast/fbs/segment.hpp"
#include "vast/fbs/utils.hpp"
#include "vast/ids.hpp"
#include "vast/io/read.hpp"
#include "vast/path.hpp"
#include "vast/uuid.hpp"

#include <caf/binary_deserializer.hpp>

#include <flatbuffers/flatbuffers.h>

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using std::cerr;
using std::cout;
using std::endl;

using namespace std::string_literals;

enum class Kind {
  Unknown,
  DatabaseDir,
  Partition,
  Index,
  Segment,
};

enum class output_verbosity {
  normal,
  verbose,
};

struct formatting_options {
  // TODO: Add `format` enum to be able to select json output.
  output_verbosity verbosity;
  bool human_readable_numbers; // print e.g. "2TiB" instead of '2199023255552"'
  bool print_bytesizes;
};

typedef void (*printer)(vast::path, const formatting_options&, int);

void print_unknown(vast::path, const formatting_options&, int);
void print_vast_db(vast::path, const formatting_options&, int);
void print_partition(vast::path, const formatting_options&, int);
void print_index(vast::path, const formatting_options&, int);
void print_segment(vast::path, const formatting_options&, int);

static const std::map<Kind, printer> printers = {
  {Kind::Unknown, print_unknown}, {Kind::DatabaseDir, print_vast_db},
  {Kind::Index, print_index},     {Kind::Partition, print_partition},
  {Kind::Segment, print_segment},
};

Kind classify(vast::path path) {
  if (path.is_directory() && path.basename() == "vast.db")
    return Kind::DatabaseDir;
  if (!path.is_regular_file())
    return Kind::Unknown;
  auto bytes = vast::io::read(path);
  if (!bytes)
    return Kind::Unknown;
  auto buf = bytes->data();
  if (vast::fbs::IndexBufferHasIdentifier(buf))
    return Kind::Index;
  if (vast::fbs::PartitionBufferHasIdentifier(buf))
    return Kind::Partition;
  if (vast::fbs::SegmentBufferHasIdentifier(buf))
    return Kind::Segment;
  return Kind::Unknown;
}

template <typename T>
struct flatbuffer_deleter {
  // Plumbing for a move-only type.
  flatbuffer_deleter() = default;
  flatbuffer_deleter(const flatbuffer_deleter&) = delete;
  flatbuffer_deleter(flatbuffer_deleter&&) = default;

  flatbuffer_deleter(std::vector<vast::byte>&& c) : chunk_(std::move(c)) {
  }

  void operator()(const T*) {
    // nop (the destructor of `chunk_` already releases the memory)
  }

  std::vector<vast::byte> chunk_;
};

// Get contents of the specified file as versioned flatbuffer, or nullptr in
// case of a read error/version mismatch.
// The unique_pointer is used to have a pointer with the correct flatbuffer
// type, that will still delete the underlying vector from `io::read`
// automatically upon destruction.
template <typename T>
std::unique_ptr<const T, flatbuffer_deleter<T>>
read_flatbuffer_file(vast::path path) {
  using result_t = std::unique_ptr<const T, flatbuffer_deleter<T>>;
  auto result
    = result_t(static_cast<const T*>(nullptr), flatbuffer_deleter<T>{});
  auto maybe_bytes = vast::io::read(path);
  if (!maybe_bytes)
    return result;
  auto bytes = std::move(*maybe_bytes);
  const auto* ptr = flatbuffers::GetRoot<T>(bytes.data());
  return result_t(ptr, flatbuffer_deleter<T>(std::move(bytes)));
}

std::ostream& operator<<(std::ostream& out, const vast::fbs::uuid::v0* uuid) {
  if (!uuid || !uuid->data())
    return out << "(null)";
  for (size_t i = 0; i < uuid->data()->size(); ++i)
    out << std::hex << +uuid->data()->Get(i);
  return out;
}

void print_unknown(vast::path path, const formatting_options&, int indent) {
  std::string spaces(indent, ' ');
  std::cout << spaces << "(unknown " << path.str() << ")\n";
}

std::string print_bytesize(size_t bytes, const formatting_options& formatting) {
  const char* suffixes[] = {
    "", "KiB", "MiB", "GiB", "TiB", "EiB",
  };
  std::stringstream ss;
  if (!formatting.human_readable_numbers) {
    ss << bytes;
  } else {
    size_t idx = 0;
    while (bytes > 1024 && idx < std::size(suffixes)) {
      ++idx;
      bytes /= 1024;
    }
    ss << bytes << std::setprecision(1) << suffixes[idx];
  }
  return std::move(ss).str();
}

void print_vast_db(vast::path vast_db, const formatting_options& formatting,
                   int indent) {
  // TODO: We should have some versioning for the layout
  // of the vast.db directory itself, so we can still read
  // older versions.
  auto index_dir = vast_db / "index";
  std::string spaces(indent, ' ');
  std::cout << spaces << index_dir.str() << "/\n";
  print_index(index_dir / "index.bin", formatting, indent + 2);
  for (auto file : vast::directory{index_dir}) {
    auto stem = file.basename(true).str();
    if (stem == "index")
      continue;
    print_partition(file, formatting, indent + 2);
  }
  auto segments_dir = vast_db / "archive" / "segments";
  std::cout << spaces << segments_dir.str() << "/\n";
  for (auto file : vast::directory{segments_dir}) {
    print_segment(file, formatting, indent + 2);
  }
}

void print_partition_v0(const vast::fbs::partition::v0* partition,
                        const formatting_options& formatting, int indent) {
  if (!partition) {
    std::cout << "(null)\n";
    return;
  }
  std::string spaces(indent, ' ');
  std::cout << spaces << "Paasdfrtition\n";
  vast::uuid id;
  if (partition->uuid())
    unpack(*partition->uuid(), id);
  spaces += "  ";
  std::cout << spaces << "uuid: " << to_string(id) << "\n";
  std::cout << spaces << "offset: " << partition->offset() << "\n";
  std::cout << spaces << "events: " << partition->events() << "\n";
  spaces += "  ";
  for (auto type_ids : *partition->type_ids()) {
    auto name = type_ids->name()->c_str();
    auto ids_bytes = type_ids->ids();
    std::cout << spaces << name << ": ";
    vast::ids restored_ids;
    caf::binary_deserializer bds(
      nullptr, reinterpret_cast<const char*>(ids_bytes->data()),
      ids_bytes->size());
    if (auto error = bds(restored_ids))
      std::cout << "(error: " << caf::to_string(error) << ")";
    else
      std::cout << rank(restored_ids);
    if (formatting.print_bytesizes)
      std::cout << "(" << print_bytesize(ids_bytes->size(), formatting) << ")";
    std::cout << "\n";
  }
  std::cout << spaces.substr(2) << "Meta Index\n";
  if (auto partition_synopsis = partition->partition_synopsis()) {
    auto s = partition->partition_synopsis();
    for (auto column_synopsis : *s->synopses()) {
      if (auto opaque = column_synopsis->opaque_synopsis()) {
        std::cout << spaces << "opaque";
        if (formatting.print_bytesizes)
          std::cout << " ("
                    << print_bytesize(opaque->data()->size(), formatting)
                    << ")";
        std::cout << '\n';
      } else if (auto bs = column_synopsis->bool_synopsis()) {
        // ...
      }
    }
  }
  std::cout << spaces.substr(2) << "Column Indices\n";
  for (auto index : *partition->indexes()) {
    auto name = index->qualified_field_name();
    auto sz = index->index()->data()->size();
    std::cout << spaces << name->c_str();
    if (formatting.print_bytesizes)
      std::cout << " (" << print_bytesize(sz, formatting) << ")";
    std::cout << "\n";
  }
  // TODO: print combined_layout and indexes
}

void print_partition(vast::path path, const formatting_options& formatting,
                     int indent) {
  auto partition = read_flatbuffer_file<vast::fbs::Partition>(path);
  if (!partition) {
    std::cout << "(error reading partition file " << path.str() << ")\n";
  }
  switch (partition->partition_type()) {
    case vast::fbs::partition::Partition::v0:
      print_partition_v0(partition->partition_as_v0(), formatting, indent);
      break;
    default:
      std::cout << "(unknown partition version)\n";
  }
}

void print_index_v0(const vast::fbs::index::v0* index,
                    const formatting_options& formatting, int indent) {
  if (!index) {
    std::cout << "(null)\n";
    return;
  }
  std::string spaces(indent, ' ');
  std::cout << spaces << "Index\n";
  spaces += "  ";
  std::cout << spaces << "layouts:\n";
  spaces += "  ";
  if (!index->stats()) {
    std::cout << "(null)\n";
  } else {
    for (auto stat : *index->stats())
      std::cout << spaces << stat->name()->c_str() << ": " << stat->count()
                << std::endl;
  }
  spaces.resize(spaces.size() - 2);
  std::cout << spaces << "partitions: ";
  if (!index->partitions()) {
    std::cout << "(null)\n";
  } else {
    std::cout << '[';
    for (auto uuid : *index->partitions())
      std::cout << uuid << ", ";
    std::cout << "]\n";
  }
}

void print_index(vast::path path, const formatting_options& formatting,
                 int indent) {
  auto index = read_flatbuffer_file<vast::fbs::Index>(path);
  if (!index) {
    std::cout << "(error reading index file " << path.str() << ")\n";
  }
  switch (index->index_type()) {
    case vast::fbs::index::Index::v0:
      print_index_v0(index->index_as_v0(), formatting, indent);
      break;
    default:
      std::cout << "(unknown partition version)\n";
  }
}

void print_segment_v0(const vast::fbs::segment::v0* segment,
                      const formatting_options& formatting, int indent = 0) {
  vast::uuid id;
  if (segment->uuid())
    unpack(*segment->uuid(), id);
  std::string spaces(indent, ' ');
  std::cout << spaces << "Segment\n";
  spaces += "  ";
  std::cout << spaces << "uuid: " << to_string(id) << "\n";
  std::cout << spaces << "events: " << segment->events() << "\n";
}

void print_segment(vast::path path, const formatting_options& formatting,
                   int indent = 0) {
  auto segment = read_flatbuffer_file<vast::fbs::Segment>(path);
  if (!segment) {
    std::cout << "(error reading segment file " << path.str() << ")\n";
  }
  switch (segment->segment_type()) {
    case vast::fbs::segment::Segment::v0:
      print_segment_v0(segment->segment_as_v0(), formatting, indent);
      break;
    default:
      std::cout << "(unknown partition version)\n";
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: ./lsvast <path/to/vast.db>\n";
    return 1;
  }
  std::string raw_path{argv[1]};
  if (raw_path.back() == '/')
    raw_path.resize(raw_path.size() - 1);
  auto path = vast::path{raw_path};
  auto kind = classify(path);
  // TODO: Add command line options to force a specific kind.
  if (kind == Kind::Unknown) {
    std::cerr << "Could not determine type of " << argv[1] << std::endl;
    return 1;
  }
  struct formatting_options format {};
  format.human_readable_numbers = true;
  format.print_bytesizes = true;
  auto printer = printers.at(kind);
  printer(path, format, 0);
  return 0;
}
