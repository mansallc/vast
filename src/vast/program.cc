#include "vast/program.h"

#include <cstdlib>
#include <iostream>
#include <boost/exception/diagnostic_information.hpp>
#include "vast/archive.h"
#include "vast/exception.h"
#include "vast/id_tracker.h"
#include "vast/index.h"
#include "vast/ingestor.h"
#include "vast/logger.h"
#include "vast/query_client.h"
#include "vast/search.h"
#include "vast/comm/broccoli.h"
#include "vast/detail/cppa_type_info.h"
#include "vast/fs/path.h"
#include "vast/fs/operations.h"
#include "vast/meta/schema_manager.h"
#include "vast/util/profiler.h"
#include "config.h"


#ifdef USE_PERFTOOLS_CPU_PROFILER
#include <google/profiler.h>
#endif
#ifdef USE_PERFTOOLS_HEAP_PROFILER
#include <google/heap-profiler.h>
#endif

namespace vast {

/// Declaration of global (extern) variables.
logger* LOGGER;

bool program::init(std::string const& filename)
{
  try
  {
    config_.load(filename);
    do_init();
    return true;
  }
  catch (error::config const& e)
  {
    std::cerr << e.what() << std::endl;
  }
  catch (boost::program_options::unknown_option const& e)
  {
    std::cerr << e.what() << std::endl;
  }
  catch (boost::exception const& e)
  {
    std::cerr << boost::diagnostic_information(e);
  }
  catch (...)
  {
    std::cerr << "unknown exception during program initialization" << std::endl;
  }

  return false;
}

bool program::init(int argc, char *argv[])
{
  try
  {
    config_.load(argc, argv);

    if (argc < 2 || config_.check("help") || config_.check("advanced"))
    {
      config_.print(std::cerr, config_.check("advanced"));
      return false;
    }

    do_init();
    return true;
  }
  catch (error::config const& e)
  {
    std::cerr << e.what() << std::endl;
  }
  catch (boost::program_options::unknown_option const& e)
  {
    std::cerr << e.what() << ", try -h or --help" << std::endl;
  }
  catch (boost::program_options::invalid_command_line_syntax const& e)
  {
    std::cerr << "invalid command line: " << e.what() << std::endl;
  }
  catch (boost::exception const& e)
  {
    std::cerr << boost::diagnostic_information(e);
  }
  catch (...)
  {
    std::cerr << "unknown exception during program initialization" << std::endl;
  }

  return false;
}

void program::start()
{
  using namespace cppa;
  detail::cppa_announce_types();

  auto log_dir = config_.get<fs::path>("directory") / "log";
  assert(fs::exists(log_dir));

  try
  {
    if (config_.check("profile"))
    {
      auto ms = config_.get<unsigned>("profile");
      profiler_ = spawn<util::profiler>(log_dir.string(), std::chrono::seconds(ms));
      send(profiler_,
           atom("run"),
           config_.check("profile-cpu"),
           config_.check("profile-heap"));
    }

    schema_manager_ = spawn<meta::schema_manager>();
    if (config_.check("schema"))
    {
      send(schema_manager_, atom("load"), config_.get<std::string>("schema"));

      if (config_.check("print-schema"))
      {
        send(schema_manager_, atom("print"));
        receive(
            on(atom("schema"), arg_match) >> [](std::string const& schema)
            {
              std::cout << schema << std::endl;
            });

        return;
      }
    }

    if (config_.check("tracker-actor") || config_.check("all-server"))
    {
      tracker_ = spawn<id_tracker>(
          (config_.get<fs::path>("directory") / "id").string());

      LOG(verbose, core) << "publishing tracker at *:"
          << config_.get<unsigned>("tracker.port");
      publish(tracker_, config_.get<unsigned>("tracker.port"));
    }
    else
    {
      LOG(verbose, core) << "connecting to tracker at "
          << config_.get<std::string>("tracker.host") << ":"
          << config_.get<unsigned>("tracker.port");

      tracker_ = remote_actor(
          config_.get<std::string>("tracker.host"),
          config_.get<unsigned>("tracker.port"));
    }

    if (config_.check("archive-actor") || config_.check("all-server"))
    {
      archive_ = spawn<archive>(
          (config_.get<fs::path>("directory") / "archive").string(),
          config_.get<size_t>("archive.max-segments"));

      LOG(verbose, core) << "publishing archive at *:"
          << config_.get<unsigned>("archive.port");
      publish(archive_, config_.get<unsigned>("archive.port"));
    }
    else
    {
      LOG(verbose, core) << "connecting to archive at "
          << config_.get<std::string>("archive.host") << ":"
          << config_.get<unsigned>("archive.port");

      archive_ = remote_actor(
          config_.get<std::string>("archive.host"),
          config_.get<unsigned>("archive.port"));
    }

    if (config_.check("index-actor") || config_.check("all-server"))
    {
      index_ = spawn<index>(
          archive_,
          (config_.get<fs::path>("directory") / "index").string());

      LOG(verbose, core) << "publishing index at *:"
          << config_.get<unsigned>("index.port");
      publish(index_, config_.get<unsigned>("index.port"));
    }
    else
    {
      LOG(verbose, core) << "connecting to index at "
          << config_.get<std::string>("index.host") << ":"
          << config_.get<unsigned>("index.port");

      index_ = remote_actor(
          config_.get<std::string>("index.host"),
          config_.get<unsigned>("index.port"));
    }


    if (config_.check("ingestor-actor"))
    {
      comm::broccoli::init(config_.check("broccoli-messages"),
                           config_.check("broccoli-calltrace"));

      ingestor_ = spawn<ingestor>(tracker_, archive_, index_);

      send(ingestor_, atom("initialize"),
          config_.get<size_t>("ingest.max-events-per-chunk"),
          config_.get<size_t>("ingest.max-segment-size") * 1000000);

      if (config_.check("ingest.events"))
      {
        auto host = config_.get<std::string>("ingest.host");
        auto port = config_.get<unsigned>("ingest.port");
        auto events = config_.get<std::vector<std::string>>("ingest.events");
        send(ingestor_, atom("ingest"), atom("broccoli"), host, port, events);
      }

      if (config_.check("ingest.file-names"))
      {
        auto type = config_.get<std::string>("ingest.file-type");
        auto files = config_.get<std::vector<std::string>>("ingest.file-names");
        for (auto& file : files)
        {
          if (fs::exists(file))
            send(ingestor_, atom("ingest"), type, file);
          else
            LOG(error, core) << "no such file: " << file;
        }
      }
    }

    if (config_.check("search-actor") || config_.check("all-server"))
    {
      search_ = spawn<search>(archive_, index_);

      LOG(verbose, core) << "publishing search at *:"
          << config_.get<unsigned>("search.port");
      publish(search_, config_.get<unsigned>("search.port"));
    }
    else
    {
      LOG(verbose, core) << "connecting to search at "
          << config_.get<std::string>("search.host") << ":"
          << config_.get<unsigned>("search.port");

      search_ = remote_actor(
          config_.get<std::string>("search.host"),
          config_.get<unsigned>("search.port"));
    }

    if (config_.check("expression"))
    {
      auto paginate = config_.get<unsigned>("query.paginate");
      auto& expression = config_.get<std::string>("query");
      query_client_ = spawn<query_client>(search_, paginate);
      send(query_client_, atom("query"), atom("create"), expression);
    }

    await_all_others_done();
    return_ = EXIT_SUCCESS;
  }
  catch (network_error const& e)
  {
      LOG(error, core) << "network error: " << e.what();
  }
  catch (...)
  {
    LOG(fatal, core)
      << "exception details:\n"
      << boost::current_exception_diagnostic_information();
  }
}

void program::stop()
{
  using namespace cppa;

  if (terminating_)
  {
    return_ = EXIT_FAILURE;
    return;
  }

  terminating_ = true;

  auto shutdown = make_any_tuple(atom("shutdown"));

  if (config_.check("query"))
    query_client_ << shutdown;

  if (config_.check("search-actor") || config_.check("all-server"))
    search_ << shutdown;

  if (config_.check("ingestor-actor"))
    ingestor_ << shutdown;

  if (config_.check("index-actor") || config_.check("all-server"))
    index_ << shutdown;

  if (config_.check("archive-actor") || config_.check("all-server"))
    archive_ << shutdown;

  if (config_.check("tracker-actor") || config_.check("all-server"))
    tracker_ << shutdown;

  schema_manager_ << shutdown;

  if (config_.check("profile"))
    profiler_ << shutdown;

  return_ = EXIT_SUCCESS;
}

int program::end()
{
  switch (return_)
  {
    case EXIT_SUCCESS:
      LOG(info, core) << "vast terminated cleanly";
      break;

    case EXIT_FAILURE:
      LOG(info, core) << "vast terminated with errors";
      break;

    default:
      assert(! "invalid return code");
  }

  return return_;
}

void program::do_init()
{
  auto vast_dir = config_.get<fs::path>("directory");
  if (! fs::exists(vast_dir))
    fs::mkdir(vast_dir);

  auto log_dir = vast_dir / "log";
  if (! fs::exists(log_dir))
      fs::mkdir(log_dir);

  LOGGER = new logger(
      static_cast<logger::level>(config_.get<int>("console-verbosity")),
      static_cast<logger::level>(config_.get<int>("logfile-verbosity")),
      log_dir / "vast.log");

  LOG(verbose, core) << " _   _____   __________";
  LOG(verbose, core) << "| | / / _ | / __/_  __/";
  LOG(verbose, core) << "| |/ / __ |_\\ \\  / / ";
  LOG(verbose, core) << "|___/_/ |_/___/ /_/  " << VAST_VERSION;
  LOG(verbose, core) << "";
}

} // namespace vast
