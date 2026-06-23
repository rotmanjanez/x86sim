#ifndef RASPSIM_RASPSIM_HPP
#define RASPSIM_RASPSIM_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace x86sim {

class AddressSpace;
struct Context;
struct MachineImpl;

using address_t = std::uint64_t;
using word_t = std::uint64_t;

enum class Protection : std::uint8_t { none = 0, read = 1, write = 2, execute = 4 };
enum class CoreModel { out_of_order, sequential };
enum class Register {
  rax = 0,
  rcx = 1,
  rdx = 2,
  rbx = 3,
  rsp = 4,
  rbp = 5,
  rsi = 6,
  rdi = 7,
  r8 = 8,
  r9 = 9,
  r10 = 10,
  r11 = 11,
  r12 = 12,
  r13 = 13,
  r14 = 14,
  r15 = 15,
  rip = 56,
  flags = 57
};
enum class XmmRegister {
  xmm0 = 16,
  xmm1 = 18,
  xmm2 = 20,
  xmm3 = 22,
  xmm4 = 24,
  xmm5 = 26,
  xmm6 = 28,
  xmm7 = 30,
  xmm8 = 32,
  xmm9 = 34,
  xmm10 = 36,
  xmm11 = 38,
  xmm12 = 40,
  xmm13 = 42,
  xmm14 = 44,
  xmm15 = 46
};
enum class SyscallKind { int80, syscall64, sysenter };
enum class StopReason { guest_exit, instruction_limit, x86_exception, host_request, unsupported_syscall };
enum class MemoryError { unaligned_address, zero_size, unmapped_address, out_of_memory, mapping_failed };

[[nodiscard]] constexpr Protection operator|(Protection lhs, Protection rhs) noexcept {
  return static_cast<Protection>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr Protection operator&(Protection lhs, Protection rhs) noexcept {
  return static_cast<Protection>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool has_protection(Protection value, Protection flag) noexcept {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) == static_cast<std::uint8_t>(flag);
}

struct XmmValue {
  word_t lo = 0;
  word_t hi = 0;
};

struct CpuidRequest {
  std::uint32_t function = 0;
  std::uint32_t subfunction = 0;
};

struct CpuidResult {
  std::uint32_t eax = 0;
  std::uint32_t ebx = 0;
  std::uint32_t ecx = 0;
  std::uint32_t edx = 0;
};

struct SyscallResult {
  StopReason reason = StopReason::host_request;
  bool continue_execution = false;
  std::string message;
};

class Machine;

class HostCallbacks {
public:
  virtual ~HostCallbacks() = default;
  virtual SyscallResult syscall(Machine&, Context&, SyscallKind) noexcept = 0;
  virtual CpuidResult cpuid(Machine&, Context&, CpuidRequest) noexcept = 0;
};

struct Options {
  CoreModel core = CoreModel::out_of_order;
  bool sse = true;
  bool x87 = true;
  bool perfect_cache = false;
  bool static_branch_prediction = false;
  std::filesystem::path log_file{};
};

struct RunOptions {
  std::optional<std::uint64_t> instruction_limit = std::nullopt;
};

struct Stats {
  std::uint64_t cycles = 0;
  std::uint64_t instructions = 0;
};

struct X86Exception {
  std::uint8_t vector = 0;
  std::uint32_t error_code = 0;
  address_t virtual_address = 0;
  address_t rip = 0;
  std::string message;
  std::string context;
};

struct RunResult {
  StopReason reason;
  Stats stats;
  std::optional<X86Exception> x86_exception;
  std::string message;
};

class RegisterRef {
public:
  RegisterRef& operator=(word_t value) noexcept;
  [[nodiscard]] operator word_t() const noexcept;

private:
  friend struct Context;
  constexpr RegisterRef(Context& context, Register reg) noexcept : context_(&context), reg_(reg) {}

  Context* context_;
  Register reg_;
};

class XmmRegisterRef {
public:
  XmmRegisterRef& operator=(XmmValue value) noexcept;
  [[nodiscard]] operator XmmValue() const noexcept;

private:
  friend struct Context;
  constexpr XmmRegisterRef(Context& context, XmmRegister reg) noexcept : context_(&context), reg_(reg) {}

  Context* context_;
  XmmRegister reg_;
};

class Machine {
public:
  static constexpr std::uint64_t kPageSize = 4096;

  explicit Machine(HostCallbacks&, Options = {});
  ~Machine();

  [[nodiscard]] std::expected<void, MemoryError> map(address_t start, std::uint64_t size, Protection) noexcept;
  void unmap(address_t start, std::uint64_t size) noexcept;
  [[nodiscard]] std::expected<void, MemoryError> write_memory(address_t start, std::span<const std::byte>) noexcept;

  [[nodiscard]] std::expected<std::span<const std::byte>, MemoryError> read_page(address_t page_aligned_address) const noexcept;

  [[nodiscard]] RunResult run(Context&, RunOptions = {});

  [[nodiscard]] const AddressSpace& address_space() const noexcept;
  [[nodiscard]] SyscallResult dispatch_syscall(Context&, SyscallKind) noexcept;
  [[nodiscard]] CpuidResult dispatch_cpuid(Context&, CpuidRequest) noexcept;
  void set_pending_stop(RunResult);

private:
  void prepare_context(Context&) noexcept;
  void reset_core(Context&);

  HostCallbacks& callbacks_;
  Options options_;
  std::unique_ptr<AddressSpace> address_space_;
  std::unique_ptr<MachineImpl> machine_;
  std::optional<RunResult> pending_stop_;
};

namespace detail {
[[nodiscard]] std::string format_cpu(const Machine&);
} // namespace detail

} // namespace x86sim

template<>
struct std::formatter<x86sim::Protection> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::Protection protection, FormatContext& ctx) const {
    const auto bits = static_cast<unsigned>(protection);
    if ((bits & ~0x7u) != 0)
      return std::format_to(ctx.out(), "unknown(0x{:02x})", bits);
    if (protection == x86sim::Protection::none)
      return std::format_to(ctx.out(), "none");

    auto out = ctx.out();
    if (x86sim::has_protection(protection, x86sim::Protection::read))
      out = std::format_to(out, "r");
    if (x86sim::has_protection(protection, x86sim::Protection::write))
      out = std::format_to(out, "w");
    if (x86sim::has_protection(protection, x86sim::Protection::execute))
      out = std::format_to(out, "x");
    return out;
  }
};

template<>
struct std::formatter<x86sim::CoreModel> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::CoreModel model, FormatContext& ctx) const {
    using enum x86sim::CoreModel;
    switch (model) {
    case out_of_order:
      return std::format_to(ctx.out(), "out_of_order");
    case sequential:
      return std::format_to(ctx.out(), "sequential");
    }
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(model));
  }
};

template<>
struct std::formatter<x86sim::Register> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::Register reg, FormatContext& ctx) const {
    using enum x86sim::Register;
    switch (reg) {
    case rax:
      return std::format_to(ctx.out(), "rax");
    case rcx:
      return std::format_to(ctx.out(), "rcx");
    case rdx:
      return std::format_to(ctx.out(), "rdx");
    case rbx:
      return std::format_to(ctx.out(), "rbx");
    case rsp:
      return std::format_to(ctx.out(), "rsp");
    case rbp:
      return std::format_to(ctx.out(), "rbp");
    case rsi:
      return std::format_to(ctx.out(), "rsi");
    case rdi:
      return std::format_to(ctx.out(), "rdi");
    case r8:
      return std::format_to(ctx.out(), "r8");
    case r9:
      return std::format_to(ctx.out(), "r9");
    case r10:
      return std::format_to(ctx.out(), "r10");
    case r11:
      return std::format_to(ctx.out(), "r11");
    case r12:
      return std::format_to(ctx.out(), "r12");
    case r13:
      return std::format_to(ctx.out(), "r13");
    case r14:
      return std::format_to(ctx.out(), "r14");
    case r15:
      return std::format_to(ctx.out(), "r15");
    case rip:
      return std::format_to(ctx.out(), "rip");
    case flags:
      return std::format_to(ctx.out(), "flags");
    }
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(reg));
  }
};

template<>
struct std::formatter<x86sim::XmmRegister> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::XmmRegister reg, FormatContext& ctx) const {
    const int index = (static_cast<int>(reg) - static_cast<int>(x86sim::XmmRegister::xmm0)) / 2;
    if (index >= 0 && index <= 15 && static_cast<int>(reg) == static_cast<int>(x86sim::XmmRegister::xmm0) + index * 2)
      return std::format_to(ctx.out(), "xmm{}", index);
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(reg));
  }
};

template<>
struct std::formatter<x86sim::SyscallKind> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::SyscallKind kind, FormatContext& ctx) const {
    using enum x86sim::SyscallKind;
    switch (kind) {
    case int80:
      return std::format_to(ctx.out(), "int80");
    case syscall64:
      return std::format_to(ctx.out(), "syscall64");
    case sysenter:
      return std::format_to(ctx.out(), "sysenter");
    }
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(kind));
  }
};

template<>
struct std::formatter<x86sim::StopReason> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::StopReason reason, FormatContext& ctx) const {
    using enum x86sim::StopReason;
    switch (reason) {
    case guest_exit:
      return std::format_to(ctx.out(), "guest_exit");
    case instruction_limit:
      return std::format_to(ctx.out(), "instruction_limit");
    case x86_exception:
      return std::format_to(ctx.out(), "x86_exception");
    case host_request:
      return std::format_to(ctx.out(), "host_request");
    case unsupported_syscall:
      return std::format_to(ctx.out(), "unsupported_syscall");
    }
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(reason));
  }
};

template<>
struct std::formatter<x86sim::MemoryError> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(x86sim::MemoryError error, FormatContext& ctx) const {
    using enum x86sim::MemoryError;
    switch (error) {
    case unaligned_address:
      return std::format_to(ctx.out(), "address is not page-aligned");
    case zero_size:
      return std::format_to(ctx.out(), "range has zero size");
    case unmapped_address:
      return std::format_to(ctx.out(), "address is not mapped");
    case out_of_memory:
      return std::format_to(ctx.out(), "out of memory");
    case mapping_failed:
      return std::format_to(ctx.out(), "mapping failed");
    }
    return std::format_to(ctx.out(), "unknown({})", static_cast<int>(error));
  }
};

template<>
struct std::formatter<x86sim::XmmValue> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::XmmValue& value, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "0x{:016x}{:016x}", value.hi, value.lo);
  }
};

template<>
struct std::formatter<x86sim::CpuidRequest> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::CpuidRequest& request, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "function=0x{:08x}, subfunction=0x{:08x}", request.function, request.subfunction);
  }
};

template<>
struct std::formatter<x86sim::CpuidResult> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::CpuidResult& result, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "eax=0x{:08x}, ebx=0x{:08x}, ecx=0x{:08x}, edx=0x{:08x}", result.eax,
                          result.ebx, result.ecx, result.edx);
  }
};

template<>
struct std::formatter<x86sim::SyscallResult> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::SyscallResult& result, FormatContext& ctx) const {
    auto out = std::format_to(ctx.out(), "reason={}, continue_execution={}", result.reason,
                              result.continue_execution);
    if (!result.message.empty())
      out = std::format_to(out, ", message={}", result.message);
    return out;
  }
};

template<>
struct std::formatter<x86sim::Options> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::Options& options, FormatContext& ctx) const {
    return std::format_to(ctx.out(),
                          "core={}, sse={}, x87={}, perfect_cache={}, static_branch_prediction={}, log_file={}",
                          options.core, options.sse, options.x87, options.perfect_cache,
                          options.static_branch_prediction, options.log_file.string());
  }
};

template<>
struct std::formatter<x86sim::RunOptions> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::RunOptions& options, FormatContext& ctx) const {
    if (options.instruction_limit)
      return std::format_to(ctx.out(), "instruction_limit={}", *options.instruction_limit);
    return std::format_to(ctx.out(), "instruction_limit=none");
  }
};

template<>
struct std::formatter<x86sim::Stats> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::Stats& stats, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{} cycles, {} instructions", stats.cycles, stats.instructions);
  }
};

template<>
struct std::formatter<x86sim::X86Exception> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::X86Exception& exception, FormatContext& ctx) const {
    auto out = ctx.out();
    if (!exception.message.empty()) {
      out = std::format_to(out, "{}", exception.message);
    } else {
      out = std::format_to(out, "Exception {} code={} addr=0x{:x} @ rip 0x{:x}",
                           static_cast<unsigned>(exception.vector), exception.error_code,
                           exception.virtual_address, exception.rip);
    }
    if (!exception.context.empty())
      out = std::format_to(out, "\n{}", exception.context);
    return out;
  }
};

template<>
struct std::formatter<x86sim::RunResult> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::RunResult& result, FormatContext& ctx) const {
    auto out = std::format_to(ctx.out(), "{} after {}", result.reason, result.stats);
    if (result.x86_exception)
      return std::format_to(out, "\n{}", *result.x86_exception);
    if (!result.message.empty())
      out = std::format_to(out, ": {}", result.message);
    return out;
  }
};

template<>
struct std::formatter<x86sim::RegisterRef> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::RegisterRef& reg, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "0x{:016x}", static_cast<x86sim::word_t>(reg));
  }
};

template<>
struct std::formatter<x86sim::XmmRegisterRef> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::XmmRegisterRef& reg, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", static_cast<x86sim::XmmValue>(reg));
  }
};

template<>
struct std::formatter<x86sim::Machine> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template<typename FormatContext>
  auto format(const x86sim::Machine& cpu, FormatContext& ctx) const {
    return std::format_to(ctx.out(), "{}", x86sim::detail::format_cpu(cpu));
  }
};

#endif
