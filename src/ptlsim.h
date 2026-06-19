// -*- c++ -*-
//
// PTLsim: Cycle Accurate x86-64 Simulator
// Simulator Structures
//
// Copyright 2003-2008 Matt T. Yourst <yourst@yourst.com>
//

#ifndef _PTLSIM_H_
#define _PTLSIM_H_

#include <string_view>

#include "globals.h"
#include "mm.h"
#include "ptlsim-api.h"
#include "ptlhwdef.h"
#include "config.h"
#include "logging.h"

extern W64 sim_cycle;
extern W64 unhalted_cycle_count;
extern W64 total_uops_committed;
extern W64 total_user_insns_committed;

void user_process_terminated(int rc);

// Compile-time host platform name; replaces runtime uname(), which is not
// available on all targets (e.g. wasm)
constexpr const char* host_platform_name() {
#if defined(__EMSCRIPTEN__)
  return "emscripten";
#elif defined(__wasi__)
  return "wasi";
#elif defined(__APPLE__)
  return "darwin";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

#ifndef BUILDHOST
#define BUILDHOST unknown
#endif
#ifndef GIT_REVISION
#define GIT_REVISION unknown
#endif

// Compile-time build provenance; single source of truth for the startup
// banner and PTLsimStats
namespace build_info {
inline constexpr std::string_view timestamp = __DATE__ " " __TIME__;
inline constexpr std::string_view hostname = stringify(BUILDHOST);
inline constexpr std::string_view git_revision = stringify(GIT_REVISION);
inline constexpr std::string_view compiler =
#if defined(__clang__)
    "clang-" stringify(__clang_major__) "." stringify(__clang_minor__) "." stringify(__clang_patchlevel__);
#elif defined(__GNUC__)
    "gcc-" stringify(__GNUC__) "." stringify(__GNUC_MINOR__) "." stringify(__GNUC_PATCHLEVEL__);
#else
    "unknown";
#endif
} // namespace build_info

static const int MAX_TRANSOP_BUFFER_SIZE = 4;

struct PTLsimConfig;
struct PTLsimStats;

struct PTLsimMachine {
  bool initialized;
  PTLsimMachine() { initialized = 0; }
  virtual bool init(PTLsimConfig& config);
  virtual int run(PTLsimConfig& config);
  virtual void reset();
  virtual void update_stats(PTLsimStats& stats);
  virtual void flush_tlb(Context& ctx);
  virtual void flush_tlb_virt(Context& ctx, Waddr virtaddr);
  static void addmachine(std::string&& name, PTLsimMachine* machine);
  static PTLsimMachine* getmachine(const std::string& name);
  static PTLsimMachine* getcurrent();
};

struct TransOpBuffer {
  TransOp uops[MAX_TRANSOP_BUFFER_SIZE];
  uopimpl_func_t synthops[MAX_TRANSOP_BUFFER_SIZE];
  int index;
  int count;

  bool get(TransOp& uop, uopimpl_func_t& synthop) {
    if (!count)
      return false;
    uop = uops[index];
    synthop = synthops[index];
    index++;
    if (index >= count) {
      count = 0;
      index = 0;
    }
    return true;
  }

  void reset() {
    index = 0;
    count = 0;
  }

  int put() { return count++; }

  bool empty() const { return (count == 0); }

  TransOpBuffer() { reset(); }
};

void split_unaligned(const TransOp& transop, TransOpBuffer& buf);

bool handle_config_change(PTLsimConfig& config, int argc = 0, char** argv = null);
void print_sysinfo();
void backup_and_reopen_logfile();
void shutdown_subsystems();

bool simulate(const std::string& machinename);
int inject_events();
bool check_for_async_sim_break();
void update_progress();

struct PTLsimBanner {
  int argc = 0;
  char** argv = null;
};

extern "C" void switch_to_sim();

//
// uop implementations
//

struct AddrPair {
  byte* start;
  byte* end;
};

void init_uops();
void shutdown_uops();
uopimpl_func_t get_synthcode_for_uop(int op, int size, bool setflags, int cond, int extshift, bool except,
                                     bool internal);
uopimpl_func_t get_synthcode_for_cond_branch(int opcode, int cond, int size, bool except);
void synth_uops_for_bb(BasicBlock& bb);
struct PTLsimStats;

extern W64 user_insn_commits;
extern W64 iterations;
extern W64 total_uops_executed;
extern W64 total_uops_committed;
extern W64 total_user_insns_committed;
extern W64 total_basic_blocks_committed;

#define INVALIDRIP 0xffffffffffffffffULL

//
// Configuration Options:
//
struct PTLsimConfig {

  std::string core_name;

  // Logging
  bool quiet;
  std::string log_filename;
  W64 loglevel;
  W64 start_log_at_iteration;
  W64 start_log_at_rip;
  bool log_on_console;
  bool log_ptlsim_boot;
  W64 log_buffer_size;
  std::string mm_logfile;
  W64 mm_log_buffer_size;
  bool enable_inline_mm_logging;
  bool enable_mm_validate;

  // Event Logging
  bool event_log_enabled;
  W64 event_log_ring_buffer_size;
  bool flush_event_log_every_cycle;
  W64 log_backwards_from_trigger_rip;
  bool dump_state_now;
  bool abort_at_end;

  // Starting Point
  W64 start_at_rip;
  bool include_dyn_linker;
  bool trigger_mode;
  W64 pause_at_startup;

  // Stopping Point
  W64 stop_at_user_insns;
  W64 stop_at_iteration;
  W64 stop_at_rip;

  // Out of order core features
  bool perfect_cache;
  bool static_branchpred;

  // Other info
  std::string bbcache_dump_filename;

  // Simulation Mode
  W64 sequential_mode_insns;
  bool exit_after_fullsim;
  void reset();
};

extern PTLsimConfig config;

extern ConfigurationParser<PTLsimConfig> configparser;

extern bool logenable;
void force_logging_enabled();

namespace std {
template<>
struct formatter<PTLsimMachine> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const PTLsimMachine& machine, format_context& ctx) const {
    return std::format_to(ctx.out(), "PTLsimMachine{{initialized={}}}", machine.initialized);
  }
};

template<>
struct formatter<PTLsimConfig> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const PTLsimConfig& config, format_context& ctx) const {
    return std::format_to(ctx.out(), "{}", configparser.options.format_to_string_config(&config));
  }
};

template<>
struct formatter<PTLsimBanner> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const PTLsimBanner& banner, format_context& ctx) const {
    auto out = std::format_to(ctx.out(), "//  \n");
    out = std::format_to(out, "//  PTLsim: Cycle Accurate x86-64 Simulator\n");
    out = std::format_to(out, "//  Copyright 1999-2007 Matt T. Yourst <yourst@yourst.com>\n");
    out = std::format_to(out, "// \n");
    out = std::format_to(out, "//  Built {} on {} using {}\n", build_info::timestamp, build_info::hostname,
                         build_info::compiler);
    out = std::format_to(out, "//  Revision {}\n", build_info::git_revision);
    out = std::format_to(out, "//  Running on {}\n", host_platform_name());
    out = std::format_to(out, "//  \n");

    out = std::format_to(out, "//  Arguments:\n");
    for (int i = 0; i < banner.argc; i++) {
      const char* arg = (banner.argv && banner.argv[i]) ? banner.argv[i] : "";
      out = std::format_to(out, "{} \n", arg);
    }

    return std::format_to(out, "\n");
  }
};
} // namespace std

#endif // _PTLSIM_H_
