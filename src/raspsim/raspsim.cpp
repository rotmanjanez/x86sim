//
// PTLsim: Cycle Accurate x86-64 Simulator
// RASPsim application
//
// Copyright 2020-2020 Alexis Engelke <engelke@in.tum.de>
//
#include <charconv>
#include <vector>
#include <ranges>
#include <string_view>
#include <span>

#include <fstream>
#include <sstream>

#include "raspsim/globals.h"
#include "raspsim/logging.h"


#include "raspsim/ptlsim.h"
#include "raspsim/ptlsim-api.h"
#include "raspsim/ptlhwdef.h"
#include "raspsim/config.h"
#include "raspsim/stats.h"
#include "raspsim/raspsim-hwsetup.h"

static void print_hex_bytes(std::span<const byte> bytes, size_t splitat = 16) {
  for (size_t i = 0; i < bytes.size(); i++) {
    logging::eprint("{:02x}", static_cast<unsigned>(bytes[i]));
    if (((i % splitat) == (splitat - 1)) && (i != bytes.size() - 1))
      logging::eprint("\n");
    else if (i != bytes.size() - 1)
      logging::eprint(" ");
  }
  logging::eprintln("");
}

struct PTLsimConfig;
extern PTLsimConfig config;

extern ConfigurationParser<PTLsimConfig> configparser;


extern "C" void assert_fail(const char* __assertion, const char* __file, unsigned int __line, const char* __function) {
  logging::println(logging::ERROR, "Assert {} failed in {}:{} ({}) at {} cycles, {} iterations, {} user commits\n",
                   __assertion, __file, __line, __function, sim_cycle, iterations, total_user_insns_committed);

  logging::flush();

  std::exit(1);
}

// Saved and restored by asm code:
FXSAVEStruct x87state;
W16 saved_cs;
W16 saved_ss;
W16 saved_ds;
W16 saved_es;
W16 saved_fs;
W16 saved_gs;

void Raspsim::propagate_x86_exception(byte exception, W32 errorcode, Waddr virtaddr) {
  Context& ctx{Raspsim::getContext()};

  const char* msg = Raspsim::formatException(exception, errorcode, virtaddr);
  logging::println("{}", msg);
  logging::flush();
  logging::eprintln("{}", msg);
  free((void*)msg);

  logging::eprintln("End state:");
  logging::eprintln("{}", ctx);
  std::exit(1);
}

//
// SYSCALL instruction from x86-64 mode
//
void Raspsim::handle_syscall_64bit() {
  bool DEBUG = 1; //analyze_in_detail();
  //
  // Handle an x86-64 syscall:
  // (This is called from the assist_syscall ucode assist)
  //
  Context& ctx{Raspsim::getContext()};

  size_t syscallid = ctx.commitarf[REG_rax];
  W64 arg1 = ctx.commitarf[REG_rdi];
  W64 arg2 = ctx.commitarf[REG_rsi];
  W64 arg3 = ctx.commitarf[REG_rdx];
  W64 arg4 = ctx.commitarf[REG_r10];
  W64 arg5 = ctx.commitarf[REG_r8];
  W64 arg6 = ctx.commitarf[REG_r9];


  logging::println(logging::DEBUG, "handle_syscall -> (#{} {}) from {} args  ({}, {}, {}, {}, {}, {}) at iteration {}",
                   syscallid, ((syscallid < lengthofSyscallNames()) ? syscall_names_64bit[syscallid] : "???"),
                   (void*)ctx.commitarf[REG_rcx], (void*)arg1, (void*)arg2, (void*)arg3, (void*)arg4, (void*)arg5,
                   (void*)arg6, iterations);

  ctx.commitarf[REG_rax] = -ENOSYS;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_rcx];

  logging::println(logging::DEBUG, "handle_syscall: result {} ({}); returning to {}", ctx.commitarf[REG_rax],
                   (void*)ctx.commitarf[REG_rax], (void*)ctx.commitarf[REG_rip]);
  logging::flush();
}

void Raspsim::handle_syscall_32bit(int semantics) {
  Context& ctx{Raspsim::getContext()};

  bool DEBUG = 1; //analyze_in_detail();
  //
  // Handle a 32-bit syscall:
  // (This is called from the assist_syscall ucode assist)
  //
  if (semantics == SYSCALL_SEMANTICS_INT80) {
    // Our exit operation.
    requested_switch_to_native = 1;
  } else {
    // But don't clobber RAX when we want out guest to quit.
    ctx.commitarf[REG_rax] = -ENOSYS;
  }

  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

CpuidResult Raspsim::handle_cpuid(W32 func, W32 subfunc) {
  return default_cpuid(func, subfunc, Raspsim::getContext().vcpuid);
}


bool handle_config_arg(Raspsim& sim, const std::string_view line, std::vector<Waddr>& dump_pages) {
  using std::operator""sv;

  if (line.empty())
    return false;

  std::vector<std::string_view> toks;
  for (auto tok : std::views::split(line, " "sv)) {
    auto sv = std::string_view(tok.begin(), tok.end());
    if (!sv.empty())
      toks.push_back(sv);
  }
  if (toks.empty())
    return false;

  if (toks[0][0] == '#') {
    return false;
  }

  auto parse_hex = [](std::string_view str) -> std::optional<W64> {
    W64 v;
    if (str.starts_with("0x"))
      str = str.substr(2);
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), v, 16);
    if (ec == std::errc())
      return v;
    return std::nullopt;
  };

  if (toks[0][0] == 'M') { // allocate page M<addr> <prot>
    if (toks.size() != 2) {
      logging::eprintln("Error: option {} has wrong number of arguments", line);
      return true;
    }
    auto addrOpt = parse_hex(toks[0].substr(1));
    if (!addrOpt || lowbits(*addrOpt, 12)) {
      logging::eprintln("Error: invalid value {}", toks[0]);
      return true;
    }
    int prot = 0;
    if (toks[1] == "ro")
      prot = PROT_READ;
    else if (toks[1] == "rw")
      prot = PROT_READ | PROT_WRITE;
    else if (toks[1] == "rx")
      prot = PROT_READ | PROT_EXEC;
    else if (toks[1] == "rwx")
      prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    else {
      logging::eprintln("Error: invalid mem prot {}", toks[1]);
      return true;
    }
    sim.map(*addrOpt, Raspsim::getPageSize(), prot);
  } else if (toks[0][0] == 'W') { // write to mem W<addr> <hexbytes>, may not cross page boundaries
    if (toks.size() != 2) {
      logging::eprintln("Error: option {} has wrong number of arguments", line);
      return true;
    }
    auto addrOpt = parse_hex(toks[0].substr(1));
    if (!addrOpt) {
      logging::eprintln("Error: invalid value {}", toks[0]);
      return true;
    }
    byte* mapped = sim.getMappedPage(*addrOpt);
    if (!mapped) {
      logging::eprintln("Error: page not mapped {}", (void*)*addrOpt);
      return true;
    }
    Waddr addr = *addrOpt;
    Waddr arglen = toks[1].length();
    if ((arglen & 1) || arglen / 2 > 4096 - lowbits(addr, 12)) {
      logging::eprintln("Error: arg has odd size or crosses page boundary {}", (void*)addr);
      return true;
    }
    unsigned n = std::min((Waddr)(4096 - lowbits(addr, 12)), arglen / 2);
    foreach (i, n) {
      char hex_byte[3] = {toks[1][i * 2], toks[1][i * 2 + 1], 0};
      mapped[i] = strtoul(hex_byte, NULL, 16);
    }
  } else if (toks[0][0] == 'D') { // dump page D<page>
    if (toks.size() != 1) {
      logging::eprintln("Error: option {} has wrong number of arguments", line);
      return true;
    }
    auto addrOpt = parse_hex(toks[0].substr(1));
    if (!addrOpt) {
      logging::eprintln("Error: invalid value {}", toks[0]);
      return true;
    }
    dump_pages.push_back(floor(*addrOpt, PAGE_SIZE));
  } else if (toks[0] == "Fnox87") {
    sim.disableX87();
  } else if (toks[0] == "Fnosse") {
    sim.disableSSE();
  } else if (toks[0] == "Fnocache") {
    sim.enablePerfectCache();
  } else if (toks[0] == "Fstbrpred") {
    sim.enableStaticBranchPrediction();
  } else {
    if (toks.size() != 2) {
      logging::eprintln("Error: option {} has wrong number of arguments", line);
      return true;
    }
    std::string reg_str(toks[0]);
    int reg = sim.getRegisterIndex(reg_str.c_str());
    if (reg < 0) {
      logging::eprintln("Error: invalid register {}", toks[0]);
      return true;
    }
    auto valOpt = parse_hex(toks[1]);
    if (!valOpt) {
      logging::eprintln("Error: invalid value {}", toks[1]);
      return true;
    }
    sim.setRegisterValue(reg, *valOpt);
  }

  return false;
}

//
// PTLsim main: called after ptlsim_preinit() brings up boot subsystems
//
int main(int argc, char** argv) {
  configparser.setup();
  config.reset();

  int ptlsim_arg_count = 1 + configparser.parse(config, argc - 1, argv + 1);
  if (ptlsim_arg_count == 0)
    ptlsim_arg_count = argc;
  handle_config_change(config, ptlsim_arg_count - 1, argv + 1);

  Raspsim sim{};
  std::vector<Waddr> dump_pages;
  // TODO(AE): set seccomp filter before parsing arguments
  bool parse_err = false;
  for (unsigned i = ptlsim_arg_count; i < argc; i++) {
    if (argv[i][0] == '@') {
      std::ifstream is(argv[i] + 1);
      if (!is) {
        logging::eprintln("Warning: cannot open command list file '{}'", argv[i]);
        continue;
      }
      std::string line;
      for (;;) {
        if (!std::getline(is, line))
          break;
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos)
          line.erase(comment_pos);
        parse_err |= handle_config_arg(sim, line, dump_pages);
      }
    } else {
      parse_err |= handle_config_arg(sim, argv[i], dump_pages);
    }
  }
  if (parse_err) {
    logging::eprintln("Error: could not parse all arguments");
    std::exit(1);
  }

  logging::println("\n=== Switching to simulation mode at rip {} ===\n", (void*)(Waddr)sim.getRegisterValue(REG_rip));
  logging::flush();
  logging::println("Baseline state:");
  logging::println("{}", sim.getContext());

  sim.run();

  logging::eprintln("End state:");
  logging::eprintln("{}", sim.getContext());

  for (Waddr addr : dump_pages) {
    byte* mapped = sim.getMappedPage(addr);
    if (!mapped) {
      logging::eprintln("Error dumping memory: page not mapped {}", (void*)addr);
    } else {
      logging::eprintln("Dump of memory at {}:", (void*)addr);
      print_hex_bytes(std::span<const byte>(mapped, PAGE_SIZE));
    }
  }

  logging::eprint("Decoder stats:");
  foreach (i, DECODE_TYPE_COUNT) {
    logging::eprint(" {}={}", decode_type_names[i], stats.decoder.x86_decode_type[i]);
  }
  logging::eprintln("");

  logging::eprintln("\n=== Exiting after full simulation at rip {} ({} cycles, {} user commits, {} iterations) ===\n",
                    (void*)(Waddr)sim.getRegisterValue(REG_rip), sim_cycle,
                    total_user_insns_committed, iterations);

  Raspsim::stutdown();

  std::exit(0);
}
