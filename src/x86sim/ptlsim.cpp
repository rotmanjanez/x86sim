//
// PTLsim: Cycle Accurate x86-64 Simulator
// Shared Functions and Structures
//
// Copyright 2000-2008 Matt T. Yourst <yourst@yourst.com>
//

#include <string>
#include <unordered_map>
#include <fcntl.h>
#include "globals.h"
#include "ptlsim.h"
#include "x86sim/logging.h"
#include "stats.h"

namespace x86sim {

#ifndef CONFIG_ONLY
//
// Global variables
//
PTLsimConfig config;
PTLsimStats stats;

bool logenable = 0;
#endif

int MachineImp::run() {
  return 0;
}

void PTLsimConfig::reset() {
  quiet = 0;
  core_name = "ooo";
  log_filename = "ptlsim.log";
  start_log_at_iteration = 0;
  start_log_at_rip = INVALIDRIP;
  log_on_console = 0;
  log_ptlsim_boot = 0;
  log_buffer_size = 524288;
  mm_logfile.clear();
  mm_log_buffer_size = 16384;
  enable_inline_mm_logging = 0;
  enable_mm_validate = 0;

  event_log_enabled = 0;
  event_log_ring_buffer_size = 32768;
  flush_event_log_every_cycle = 0;
  log_backwards_from_trigger_rip = INVALIDRIP;
  dump_state_now = 0;
  abort_at_end = 0;

  stop_at_user_insns = std::numeric_limits<W64s>::max();
  stop_at_iteration = std::numeric_limits<W64s>::max();
  stop_at_rip = INVALIDRIP;

  perfect_cache = 0;
  static_branchpred = 0;

  bbcache_dump_filename.clear();
}

#ifndef CONFIG_ONLY


std::string current_log_filename;
std::string current_bbcache_dump_filename;

void backup_and_reopen_logfile() {
  if (!config.log_filename.empty()) {
    // Close existing log
    logging::flush();

    // Backup old log file
    std::string oldname = std::format("{}.backup", config.log_filename);
    std::remove(oldname.c_str());
    std::rename(config.log_filename.c_str(), oldname.c_str());

    // Open new log file
    logging::set_file_sink(config.log_filename.c_str());
  }
}

void force_logging_enabled() {
  logenable = 1;
  config.start_log_at_iteration = 0;
  config.loglevel = static_cast<int>(logging::Level::VERBOSE); // Maximum verbosity
  logging::set_level(logging::VERBOSE);
  config.flush_event_log_every_cycle = 1;
}

bool handle_config_change(PTLsimConfig& config, int argc, char** argv) {
  static bool first_time = true;

  if (!config.log_filename.empty() && (config.log_filename != current_log_filename)) {
    // Can also use "-logfile /dev/fd/1" to send to stdout (or /dev/fd/2 for stderr):
    backup_and_reopen_logfile();
    current_log_filename = config.log_filename;
  }

  // Set log level in the new logging system
  logging::set_level(static_cast<int>(config.loglevel));

  if ((config.loglevel > 0) & (config.start_log_at_rip == INVALIDRIP) & (config.start_log_at_iteration == std::numeric_limits<W64s>::max())) {
    config.start_log_at_iteration = 0;
  }

  // Force printing every cycle if loglevel <= TRACE (5):
  // (Note: new system has inverted semantics - lower numbers = more verbose)
  if (config.loglevel <= static_cast<int>(logging::Level::TRACE)) {
    config.event_log_enabled = 1;
    config.flush_event_log_every_cycle = 1;
  }

  //
  // Fix up parameter defaults:
  //
  if (config.start_log_at_rip != INVALIDRIP) {
    config.start_log_at_iteration = std::numeric_limits<W64s>::max();
    logenable = 0;
  } else if (config.start_log_at_iteration != std::numeric_limits<W64s>::max()) {
    config.start_log_at_rip = INVALIDRIP;
    logenable = 0;
  }

  logenable = 1;

  if (!config.bbcache_dump_filename.empty() && (config.bbcache_dump_filename != current_bbcache_dump_filename)) {
    // Can also use "-logfile /dev/fd/1" to send to stdout (or /dev/fd/2 for stderr):
    if (bbcache_dump_file)
      std::fclose(bbcache_dump_file);
    bbcache_dump_file = std::fopen(config.bbcache_dump_filename.c_str(), "wb");
    if (!bbcache_dump_file) {
      logging::println(logging::WARNING, "Cannot open bb dump file '{}'", config.bbcache_dump_filename);
    }
    current_bbcache_dump_filename = config.bbcache_dump_filename;
  }

  config.start_log_at_rip = signext64(config.start_log_at_rip, 48);
  config.log_backwards_from_trigger_rip = signext64(config.log_backwards_from_trigger_rip, 48);
  config.start_at_rip = signext64(config.start_at_rip, 48);
  config.stop_at_rip = signext64(config.stop_at_rip, 48);

  if (first_time) {
    if (!config.quiet) {
      logging::eprint("{}", PTLsimBanner{argc, argv});
    }
    logging::println(logging::INFO, "{}", config);
    first_time = false;
  }

  return true;
}

extern void shutdown_uops();

void shutdown_subsystems() {
  //
  // Let the subsystems close any special files or buffers
  // they may have open:
  //
  shutdown_uops();
  shutdown_decode();
}

#endif // CONFIG_ONLY

} // namespace x86sim
