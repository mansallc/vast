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

#include "vast/system/sink_command.hpp"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <caf/scoped_actor.hpp>

#include "vast/logger.hpp"

#include "vast/system/signal_monitor.hpp"

using namespace std::chrono_literals;
using namespace caf;

namespace vast::system {

int sink_command::run_impl(caf::actor_system& sys,
                                  const caf::config_value_map& options,
                                  argument_iterator begin,
                                  argument_iterator end) {
  // Get a convenient and blocking way to interact with actors.
  scoped_actor self{sys};
  // Get VAST node.
  auto node_opt = spawn_or_connect_to_node(self, options);
  if (!node_opt)
    return EXIT_FAILURE;
  auto node = std::move(*node_opt);
  /// Spawn an actor that takes care of CTRL+C and friends.
  auto sig_mon = self->spawn<detached>(system::signal_monitor, 750ms, self);
  auto guard = caf::detail::make_scope_guard([&] {
    self->send_exit(sig_mon, exit_reason::user_shutdown);
  });
  // Spawn a sink.
  VAST_DEBUG(this, "spawns sink with parameters:", deep_to_string(options));
  auto snk_opt = make_sink(self, options, begin, end);
  if (!snk_opt) {
    std::cerr << "unable to spawn sink: " << sys.render(snk_opt.error())
              << std::endl;
    return EXIT_FAILURE;
  }
  auto snk = std::move(*snk_opt);
  // Spawn exporter at the node.
  actor exp;
  // TODO: we need to also include arguments in CLI format from the export
  //       command; we really should forward `options` to the node actor
  //       instead to clean this up
  auto args = caf::message_builder{begin, end}.move_to_message();
  args = make_message("exporter") + args;
  if (get_or<bool>(options, "continuous", false))
    args += make_message("--continuous");
  if (get_or<bool>(options, "historical", false))
    args += make_message("--historical");
  if (get_or<bool>(options, "unified", false))
    args += make_message("--unified");
  auto max_events = get_or<uint64_t>(options, "events", 0u);
  args += make_message("-e", std::to_string(max_events));
  VAST_DEBUG(this, "spawns exporter with parameters:", to_string(args));
  self->request(node, infinite, "spawn", args).receive(
    [&](const actor& a) {
      exp = a;
    },
    [&](const error& e) {
      VAST_IGNORE_UNUSED(e);
      VAST_ERROR(this, "failed to spawn exporter:", self->system().render(e));
    }
  );
  if (!exp) {
    self->send_exit(snk, exit_reason::user_shutdown);
    return 1;
  }
  // Start the exporter.
  self->send(exp, system::sink_atom::value, snk);
  self->send(exp, system::run_atom::value);
  self->monitor(snk);
  self->monitor(exp);
  auto rc = 0;
  auto stop = false;
  self->do_receive(
    [&](const down_msg& msg) {
      if (msg.source == node)  {
        VAST_DEBUG(this, "received DOWN from node");
        self->send_exit(snk, exit_reason::user_shutdown);
        self->send_exit(exp, exit_reason::user_shutdown);
        rc = 1;
      } else if (msg.source == exp) {
        VAST_DEBUG(this, "received DOWN from exporter");
        self->send_exit(snk, exit_reason::user_shutdown);
      } else if (msg.source == snk) {
        VAST_DEBUG(this, "received DOWN from sink");
        self->send_exit(exp, exit_reason::user_shutdown);
        rc = 1;
      } else {
        VAST_ASSERT(!"received DOWN from inexplicable actor");
      }
      stop = true;
    },
    [&](system::signal_atom, int signal) {
      VAST_DEBUG(this, "got " << ::strsignal(signal));
      if (signal == SIGINT || signal == SIGTERM) {
        self->send_exit(exp, exit_reason::user_shutdown);
        self->send_exit(snk, exit_reason::user_shutdown);
      }
    }
  ).until([&] { return stop; });
  cleanup(node);
  return rc;
}

} // namespace vast::system