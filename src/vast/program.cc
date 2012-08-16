#include "vast/program.h"

#include <cstdlib>
#include <iostream>
#include "vast/archive.h"
#include "vast/exception.h"
#include "vast/id_tracker.h"
#include "vast/index.h"
#include "vast/ingestor.h"
#include "vast/logger.h"
#include "vast/query_client.h"
#include "vast/search.h"
#include "vast/system_monitor.h"
#include "vast/comm/broccoli.h"
#include "vast/detail/cppa_type_info.h"
#include "vast/fs/path.h"
#include "vast/fs/operations.h"
#include "vast/meta/schema_manager.h"
#include "vast/util/profiler.h"
#include "config.h"

namespace vast {

using namespace cppa;

program::program(configuration const& config)
  : config_(config)
{
  detail::cppa_announce_types();
}

bool program::run()
{
  if (! start())
    return false;

  auto mon = spawn<system_monitor>(self);
  self->monitor(mon);

  bool done = false;
  do_receive(
      on(atom("system"), atom("keystroke"), arg_match) >> [](char key)
      {
        DBG(core) << "received keystroke: " << key;
      },
      on(atom("DOWN"), arg_match) >> [&done](uint32_t reason)
      {
        done = true;
      }).until(gref(done) == true);

  stop();
  await_all_others_done();

  return true;
}

bool program::start()
{
  LOG(verbose, core) << " _   _____   __________";
  LOG(verbose, core) << "| | / / _ | / __/_  __/";
  LOG(verbose, core) << "| |/ / __ |_\\ \\  / / ";
  LOG(verbose, core) << "|___/_/ |_/___/ /_/  " << VAST_VERSION;
  LOG(verbose, core) << "";

  auto vast_dir = config_.get<fs::path>("directory");
  if (! fs::exists(vast_dir))
    fs::mkdir(vast_dir);

  auto log_dir = config_.get<fs::path>("log.directory");
  if (! fs::exists(log_dir))
      fs::mkdir(log_dir);

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

    // TODO: uncomment once brought back into the game.
    //schema_manager_ = spawn<meta::schema_manager>();
    //if (config_.check("schema"))
    //{
    //  send(schema_manager_, atom("load"), config_.get<std::string>("schema"));

    //  if (config_.check("print-schema"))
    //  {
    //    send(schema_manager_, atom("print"));
    //    receive(
    //        on(atom("schema"), arg_match) >> [](std::string const& schema)
    //        {
    //          std::cout << schema << std::endl;
    //        },
    //        after(std::chrono::seconds(1)) >> [=]
    //        {
    //          LOG(error, meta)
    //            << "schema manager did not answer after one second";
    //        });

    //    return false;
    //  }
    //}

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

      LOG(verbose, core) << "connected to tracker actor @" << tracker_->id();
    }

    if (config_.check("archive-actor") || config_.check("all-server"))
    {
      archive_ = spawn<archive>(
          (config_.get<fs::path>("directory") / "archive").string(),
          config_.get<size_t>("archive.max-segments"));
      send(archive_, atom("load"));

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

      LOG(verbose, core) << "connected to archive actor @" << archive_->id();
    }

    if (config_.check("index-actor") || config_.check("all-server"))
    {
      index_ = spawn<index>(
          archive_,
          (config_.get<fs::path>("directory") / "index").string());
      send(index_, atom("load"));

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

      LOG(verbose, core) << "connected to index actor @" << index_->id();
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

      send(ingestor_, atom("extract"));
    }

    if (config_.check("search-actor") || config_.check("all-server"))
    {
      search_ = spawn<search>(archive_, index_);

      LOG(verbose, core) << "publishing search at *:"
          << config_.get<unsigned>("search.port");

      publish(search_, config_.get<unsigned>("search.port"));
    }
    else if (config_.check("expression"))
    {
      LOG(verbose, core) << "connecting to search at "
          << config_.get<std::string>("search.host") << ":"
          << config_.get<unsigned>("search.port");

      search_ = remote_actor(
          config_.get<std::string>("search.host"),
          config_.get<unsigned>("search.port"));

      LOG(verbose, core) << "connected to search actor @" << search_->id();

      auto paginate = config_.get<unsigned>("query.paginate");
      auto& expression = config_.get<std::string>("expression");
      query_client_ = spawn<query_client>(search_, expression, paginate);
      send(query_client_, atom("start"));
    }

    return true;
  }
  catch (network_error const& e)
  {
      LOG(error, core) << "network error: " << e.what();
  }

  return false;
}

void program::stop()
{
  auto shutdown = make_any_tuple(atom("shutdown"));

  if (config_.check("expression"))
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

  // TODO: uncomment once brought back into the game.
  //schema_manager_ << shutdown;

  if (config_.check("profile"))
    profiler_ << shutdown;
}

} // namespace vast
