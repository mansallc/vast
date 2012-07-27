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

config::config(char const* msg, char const* option)
{
  std::ostringstream oss;
  oss << msg << " (--" << option << ')';
  msg_ = oss.str();
}

config::config(char const* msg, char const* opt1, char const* opt2)
{
  std::ostringstream oss;
  oss << msg << " (--" << opt1 << " and --" << opt2 << ')';
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

segment::segment(char const* msg)
  : exception(msg)
{
}

query::query(char const* msg)
  : exception(msg)
{
}

syntax::syntax(char const* msg, std::string const& q)
{
  std::ostringstream oss;
  oss << "syntax error: " << msg << " (query: " << q << ')';
  msg_ = oss.str();
}

semantic::semantic(char const* msg, std::string const& q)
{
  std::ostringstream oss;
  oss << "semantic error: " << msg << " (query: " << q << ')';
  msg_ = oss.str();
}

} // namespace error
} // namespace vast
