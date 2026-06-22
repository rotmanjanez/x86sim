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
#include "logging.h"
#include "stats.h"

#ifndef CONFIG_ONLY
//
// Global variables
//
PTLsimConfig config;
ConfigurationParser<PTLsimConfig> configparser;
PTLsimStats stats;

bool logenable = 0;
W64 sim_cycle = 0;
W64 unhalted_cycle_count = 0;
W64 iterations = 0;
W64 total_uops_executed = 0;
W64 total_uops_committed = 0;
W64 total_user_insns_committed = 0;
W64 total_basic_blocks_committed = 0;
#endif

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

  stop_at_user_insns = infinity;
  stop_at_iteration = infinity;
  stop_at_rip = INVALIDRIP;

  perfect_cache = 0;
  static_branchpred = 0;

  bbcache_dump_filename.clear();
}

template<>
void ConfigurationParser<PTLsimConfig>::setup() {
  section("Simulation Control");

  add(core_name, "core", "Run using specified core (-core <corename>)");

  section("General Logging Control");
  add(quiet, "quiet", "Do not print PTLsim system information banner");
  add(log_filename, "logfile", "Log filename (use /dev/fd/1 for stdout, /dev/fd/2 for stderr)");
  add(loglevel, "loglevel", "Log level (0 to 99)");
  add(start_log_at_iteration, "startlog", "Start logging after iteration <startlog>");
  add(start_log_at_rip, "startlogrip", "Start logging after first translation of basic block starting at rip");
  add(log_on_console, "consolelog", "Replicate log file messages to console");
  add(log_ptlsim_boot, "bootlog", "Log PTLsim early boot and injection process (for debugging)");
  add(log_buffer_size, "logbufsize", "Size of PTLsim logfile buffer (not related to -ringbuf)");
  add(dump_state_now, "dump-state-now", "Dump the event log ring buffer and internal state of the active core");
  add(abort_at_end, "abort-at-end", "Abort current simulation after next command (don't wait for next x86 boundary)");
  add(mm_logfile, "mm-logfile", "Log PTLsim memory manager requests (alloc, free) to this file (use with ptlmmlog)");
  add(mm_log_buffer_size, "mm-logbuf-size", "Size of PTLsim memory manager log buffer (in events, not bytes)");
  add(enable_inline_mm_logging, "mm-log-inline", "Print every memory manager request in the main log file");
  add(enable_mm_validate, "mm-validate", "Validate every memory manager request against internal structures (slow)");

  section("Event Ring Buffer Logging Control");
  add(event_log_enabled, "ringbuf", "Log all core events to the ring buffer for backwards-in-time debugging");
  add(event_log_ring_buffer_size, "ringbuf-size", "Core event log ring buffer size: only save last <ringbuf> entries");
  add(flush_event_log_every_cycle, "flush-events", "Flush event log ring buffer to logfile after every cycle");
  add(log_backwards_from_trigger_rip, "ringbuf-trigger-rip",
      "Print event ring buffer when first uop in this rip is committed");

  // Userspace only
  section("Start Point");
  add(start_at_rip, "startrip", "Start at rip <startrip>");
  add(include_dyn_linker, "excludeld", "Exclude dynamic linker execution");
  add(trigger_mode, "trigger", "Trigger mode: wait for user process to do simcall before entering PTL mode");
  add(pause_at_startup, "pause-at-startup", "Pause for N seconds after starting up (to allow debugger to attach)");

  section("Trace Stop Point");
  add(stop_at_user_insns, "stopinsns", "Stop after executing <stopinsns> user instructions");
  add(stop_at_iteration, "stopiter", "Stop after <stop> iterations (does not apply to cycle-accurate cores)");
  add(stop_at_rip, "stoprip", "Stop before rip <stoprip> is translated for the first time");

  section("Out of Order Core (ooocore)");
  add(perfect_cache, "perfect-cache", "Perfect cache performance: all loads and stores hit in L1");

  section("Miscellaneous");
  add(bbcache_dump_filename, "bbdump", "Basic block cache dump filename");
  add(sequential_mode_insns, "seq", "Run in sequential mode for <seq> instructions before switching to out of order");
  add(exit_after_fullsim, "exitend", "Kill the thread after full simulation completes rather than going native");
};

#ifndef CONFIG_ONLY

void print_usage(int argc, char** argv) {
  logging::eprintln("Syntax: ptlsim <executable> <arguments...>");
  logging::eprintln("All other options come from file /home/<username>/.ptlsim/path/to/executable");
  logging::eprintln("");
  logging::eprint("{}", configparser.options.format_to_string_usage(&config));
}

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

void print_sysinfo();

bool handle_config_change(PTLsimConfig& config, int argc, char** argv) {
  static bool first_time = true;

  if (!config.log_filename.empty() && (config.log_filename != current_log_filename)) {
    // Can also use "-logfile /dev/fd/1" to send to stdout (or /dev/fd/2 for stderr):
    backup_and_reopen_logfile();
    current_log_filename = config.log_filename;
  }

  // Set log level in the new logging system
  logging::set_level(static_cast<int>(config.loglevel));

  if ((config.loglevel > 0) & (config.start_log_at_rip == INVALIDRIP) & (config.start_log_at_iteration == infinity)) {
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
    config.start_log_at_iteration = infinity;
    logenable = 0;
  } else if (config.start_log_at_iteration != infinity) {
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

#ifdef PTLSIM_AMD64
  config.start_log_at_rip = signext64(config.start_log_at_rip, 48);
  config.log_backwards_from_trigger_rip = signext64(config.log_backwards_from_trigger_rip, 48);
  config.start_at_rip = signext64(config.start_at_rip, 48);
  config.stop_at_rip = signext64(config.stop_at_rip, 48);
#endif

  if (first_time) {
    if (!config.quiet) {
      logging::eprint("{}", PTLsimBanner{argc, argv});
      print_sysinfo();
    }
    logging::println(logging::INFO, "{}", config);
    first_time = false;
  }

  return true;
}


// Make sure the vtable gets compiled:
PTLsimMachine dummymachine;

std::string_view PTLsimMachine::name() const {
  return "unknown";
}
bool PTLsimMachine::init(PTLsimConfig& config) {
  return false;
}
int PTLsimMachine::run(PTLsimConfig& config) {
  return 0;
}
void PTLsimMachine::reset() {
  bbcache.reset();
  initialized = 0;
}
void PTLsimMachine::update_stats(PTLsimStats& stats) {
  return;
}
void PTLsimMachine::flush_tlb(Context& ctx) {
  return;
}
void PTLsimMachine::flush_tlb_virt(Context& ctx, Waddr virtaddr) {
  return;
}

void simulateInitializedMachine(PTLsimMachine& machine) {
  machine.run(config);
  machine.update_stats(stats);
}

bool ensureMachineInitialized(PTLsimMachine& m) {
  if (!m.initialized) {
    logging::println("Initializing core '{}'", m.name());
    if (!m.init(config)) {
      return 1;
    }
    m.initialized = 1;
  }
  return 0;
}

bool simulate(PTLsimMachine& machine) {
  if (ensureMachineInitialized(machine)) {
    logging::println(logging::ERROR, "Cannot initialize core model; check its configuration!");
    return 0;
  }

  logging::println(logging::INFO, "Switching to simulation core '{}'...", machine.name());
  logging::flush();
  logging::eprintln("Switching to simulation core '{}'...", machine.name());
  logging::println(logging::INFO, "Stopping after {} commits", config.stop_at_user_insns);
  logging::flush();

  simulateInitializedMachine(machine);

  logging::println(logging::INFO, "Stopped after {} cycles, {} instructions", sim_cycle, total_user_insns_committed);
  logging::flush();

  return 0;
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
