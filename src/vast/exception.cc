#include "vast/exception.h"

#include <sstream>

namespace vast {

exception::exception(char const* msg)
  : msg_(msg)
{
}

exception::exception(std::string const& msg)
  : msg_(msg)
{
}

char const* exception::what() const noexcept
{
  return msg_.data();
}

namespace error {

out_of_range::out_of_range(char const* msg)
 : exception(msg)
{
}

bad_type::bad_type(char const* msg, value_type type)
{
  std::ostringstream oss;
  oss << "value: " << msg << " (type '" << type << "')";
  msg_ = oss.str();
}

bad_type::bad_type(char const* msg, value_type type1, value_type type2)
{
  std::ostringstream oss;
  oss << "value: " << msg
    << " (types '" << type1 << "' and '" << type2 << "')";
  msg_ = oss.str();
}

bad_value::bad_value(std::string const& msg, value_type type)
{
  std::ostringstream oss;
  oss << "bad value: " << msg << " (type '" << type << "')";
  msg_ = oss.str();
}

serialization::serialization(char const* msg)
  : exception(msg)
{
}

io::io(char const* msg)
  : exception(msg)
{
}

logic::logic(char const* msg)
  : exception(msg)
{
}

fs::fs(char const* msg)
  : exception(msg)
{
}

fs::fs(char const* msg, std::string const& filename)
{
  std::ostringstream oss;
  oss << "file " << filename << ": " << msg;
  msg_ = oss.str();
}

network::network(char const* msg)
  : exception(msg)
{
}

#ifdef VAST_HAVE_BROCCOLI
broccoli::broccoli(char const* msg)
  : network(msg)
{
}
#endif

config::config(char const* msg)
  : exception(msg)
{
}

config::config(char const* msg, char shortcut)
{
  std::ostringstream oss;
  oss << msg << " (-" << shortcut << ')';
  msg_ = oss.str();
}

config::config(char const* msg, std::string option)
{
  std::ostringstream oss;
  oss << msg << " (" << option << ')';
  msg_ = oss.str();
}

config::config(char const* msg, std::string option1, std::string option2)
{
  std::ostringstream oss;
  oss << msg << " (" << option1 << " and " << option2 << ')';
  msg_ = oss.str();
}

ingest::ingest(char const* msg)
  : exception(msg)
{
}

ingest::ingest(std::string const& msg)
  : exception(msg)
{
}

parse::parse(char const* msg)
  : ingest(msg)
{
}

parse::parse(char const* msg, size_t line)
  : ingest(msg)
{
  std::ostringstream oss;
  oss << "line " << line << ": " << msg;
  msg_ = oss.str();
}

segment::segment(char const* msg)
  : exception(msg)
{
}

query::query(char const* msg)
  : exception(msg)
{
}

query::query(char const* msg, std::string const& expr)
{
  std::ostringstream oss;
  oss << msg << "'" << expr << "'";
  msg_ = oss.str();
}

schema::schema(char const* msg)
  : exception(msg)
{
}

index::index(char const* msg)
  : exception(msg)
{
}

} // namespace error
} // namespace vast
