// -*- c++ -*-
//
// PTLsim: Cycle Accurate x86-64 Simulator
// Simulator Structures
//
// Copyright 2003-2008 Matt T. Yourst <yourst@yourst.com>
//

#ifndef _PTLSIM_H_
#define _PTLSIM_H_

#include <format>
#include <limits>
#include <string>
#include <string_view>

#include "globals.h"
#include "ptlsim-api.h"
#include "ptlhwdef.h"
#include "x86sim/logging.h"

namespace x86sim {

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

struct MachineImpl {
  W64 sim_cycle = 0;
  W64 unhalted_cycle_count = 0;
  W64 iterations = 0;
  W64 total_uops_executed = 0;
  W64 total_uops_committed = 0;
  W64 total_user_insns_committed = 0;
  W64 total_basic_blocks_committed = 0;

  explicit MachineImpl(const PTLsimConfig&) {}
  virtual ~MachineImpl() = default;
  virtual std::string_view name() const { return "unkown"; }
  virtual int run();
  virtual void update_stats(PTLsimStats& stats) { return; }
  virtual void flush_tlb(Context& ctx) { return; }
  virtual void flush_tlb_virt(Context& ctx, Waddr virtaddr) { return; }
};

struct TransOpBuffer {
  TransOp uops[MAX_TRANSOP_BUFFER_SIZE];
  UopImpl synthops[MAX_TRANSOP_BUFFER_SIZE];
  int index;
  int count;

  bool get(TransOp& uop, UopImpl& synthop) {
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


void backup_and_reopen_logfile();
void shutdown_subsystems();

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
UopImpl get_synthcode_for_uop(int op, int size, bool setflags, int cond, int extshift, bool except, bool internal);
UopImpl get_synthcode_for_cond_branch(int opcode, int cond, int size, bool except);
void synth_uops_for_bb(BasicBlock& bb);
struct PTLsimStats;

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

extern bool logenable;
void force_logging_enabled();

} // namespace x86sim

namespace std {

template<>
struct formatter<x86sim::PTLsimConfig> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const x86sim::PTLsimConfig& config, format_context& ctx) const {
    auto out = std::format_to(ctx.out(), "Active parameters:\n");

    auto bool_text = [](bool value) { return value ? "enabled" : "disabled"; };
    auto format_w64 = [](x86sim::W64 value) {
      if (value == 0)
        return std::string{"0"};
      if (value == std::numeric_limits<x86sim::W64s>::max())
        return std::string{"infinity"};
      if ((value % 1000000000LL) == 0)
        return std::format("{} G", value / 1000000000LL);
      if ((value % 1000000LL) == 0)
        return std::format("{} M", value / 1000000LL);
      return std::format("{}", value);
    };

    auto field = [&out]<typename T>(std::string_view name, const T& value) {
      out = std::format_to(out, "  -{:<24} {}\n", name, value);
    };

    field("core", config.core_name);
    field("quiet", bool_text(config.quiet));
    field("logfile", config.log_filename);
    field("loglevel", format_w64(config.loglevel));
    field("startlog", format_w64(config.start_log_at_iteration));
    field("startlogrip", format_w64(config.start_log_at_rip));
    field("consolelog", bool_text(config.log_on_console));
    field("bootlog", bool_text(config.log_ptlsim_boot));
    field("logbufsize", format_w64(config.log_buffer_size));
    field("mm-logfile", config.mm_logfile);
    field("mm-logbuf-size", format_w64(config.mm_log_buffer_size));
    field("mm-log-inline", bool_text(config.enable_inline_mm_logging));
    field("mm-validate", bool_text(config.enable_mm_validate));
    field("ringbuf", bool_text(config.event_log_enabled));
    field("ringbuf-size", format_w64(config.event_log_ring_buffer_size));
    field("flush-events", bool_text(config.flush_event_log_every_cycle));
    field("ringbuf-trigger-rip", format_w64(config.log_backwards_from_trigger_rip));
    field("dump-state-now", bool_text(config.dump_state_now));
    field("abort-at-end", bool_text(config.abort_at_end));
    field("startrip", format_w64(config.start_at_rip));
    field("excludeld", bool_text(config.include_dyn_linker));
    field("trigger", bool_text(config.trigger_mode));
    field("pause-at-startup", format_w64(config.pause_at_startup));
    field("stopinsns", format_w64(config.stop_at_user_insns));
    field("stopiter", format_w64(config.stop_at_iteration));
    field("stoprip", format_w64(config.stop_at_rip));
    field("perfect-cache", bool_text(config.perfect_cache));
    field("static-branchpred", bool_text(config.static_branchpred));
    field("bbdump", config.bbcache_dump_filename);
    field("seq", format_w64(config.sequential_mode_insns));
    field("exitend", bool_text(config.exit_after_fullsim));

    return out;
  }
};

template<>
struct formatter<x86sim::PTLsimBanner> {
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  auto format(const x86sim::PTLsimBanner& banner, format_context& ctx) const {
    auto out = std::format_to(ctx.out(), "//  \n");
    out = std::format_to(out, "//  PTLsim: Cycle Accurate x86-64 Simulator\n");
    out = std::format_to(out, "//  Copyright 1999-2007 Matt T. Yourst <yourst@yourst.com>\n");
    out = std::format_to(out, "// \n");
    out = std::format_to(out, "//  Built {} on {} using {}\n", x86sim::build_info::timestamp,
                         x86sim::build_info::hostname, x86sim::build_info::compiler);
    out = std::format_to(out, "//  Revision {}\n", x86sim::build_info::git_revision);
    out = std::format_to(out, "//  Running on {}\n", x86sim::host_platform_name());
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
