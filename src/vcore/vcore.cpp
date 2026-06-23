#include "vcore/vcore.hpp"

#include "addrspace.h"
#include "ooocore.h"
#include "ptlhwdef.h"
#include "ptlsim.h"
#include "ptlsim-api.h"
#include "seqcore.h"
#include "vcore/logging.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace vcore {
namespace {

thread_local CPU* running_cpu = nullptr;

struct InternalX86Exception {
  X86Exception exception;
};

[[nodiscard]] CPU& active_cpu() noexcept {
  assert(running_cpu);
  return *running_cpu;
}

[[nodiscard]] Context& active_context() noexcept {
  return detail::context(active_cpu());
}

[[nodiscard]] AddressSpace& active_address_space() noexcept {
  return detail::address_space(active_cpu());
}

[[nodiscard]] Stats current_stats() noexcept {
  return {.cycles = sim_cycle, .instructions = total_user_insns_committed};
}

[[nodiscard]] SyscallKind to_syscall_kind(int semantics) {
  switch (semantics) {
  case SYSCALL_SEMANTICS_INT80:
    return SyscallKind::int80;
  case SYSCALL_SEMANTICS_SYSENTER:
    return SyscallKind::sysenter;
  default:
    throw std::runtime_error("unknown syscall semantics");
  }
}

void advance_default_syscall_rip_if_unchanged(CPU& cpu, SyscallKind kind, W64 old_rip) noexcept {
  if (cpu[Register::rip] != old_rip)
    return;

  Context& context = detail::context(cpu);
  switch (kind) {
  case SyscallKind::int80:
  case SyscallKind::sysenter:
    cpu[Register::rip] = context.commitarf[REG_nextrip];
    break;
  case SyscallKind::syscall64:
    cpu[Register::rip] = context.commitarf[REG_rcx];
    break;
  }
}

} // namespace

bool requested_switch_to_native = false;

int current_vcpuid() {
  return 0;
}

bool asp_check_exec(void* addr) {
  AddressSpace& asp = active_address_space();
  return asp.fastcheck(addr, asp.execmap);
}

bool smc_isdirty(Waddr mfn) {
  return active_address_space().isdirty(mfn);
}

void smc_setdirty(Waddr mfn) {
  active_address_space().setdirty(mfn);
}

void smc_cleardirty(Waddr mfn) {
  active_address_space().cleardirty(mfn);
}

bool check_for_async_sim_break() {
  return iterations >= config.stop_at_iteration || total_user_insns_committed >= config.stop_at_user_insns;
}

int inject_events() {
  return 0;
}

Context& contextof(int) {
  return active_context();
}

W64 loadphys(Waddr addr) {
  return *reinterpret_cast<W64*>(addr);
}

W64 storemask(Waddr addr, W64 data, byte bytemask) {
  W64& mem = *reinterpret_cast<W64*>(addr);
  mem = mux64(expand_8bit_to_64bit_lut[bytemask], mem, data);
  return data;
}

int Context::copy_from_user(void* target, Waddr addr, int bytes, PageFaultErrorCode& pfec, Waddr& faultaddr,
                            bool forexec, Level1PTE& ptelo, Level1PTE& ptehi) {
  AddressSpace& asp = active_address_space();
  logging::println("VMEM: Read from user {} ({})", reinterpret_cast<void*>(addr), bytes);
  logging::flush();

  int n = 0;
  pfec = 0;
  ptelo = 0;
  ptehi = 0;

  bool readable = asp.fastcheck(reinterpret_cast<byte*>(addr), asp.readmap);
  bool executable = true;
  if likely (forexec)
    executable = asp.fastcheck(reinterpret_cast<byte*>(addr), asp.execmap);

  if unlikely ((!readable) | (forexec & !executable)) {
    faultaddr = addr;
    pfec.p = readable;
    pfec.nx = (forexec & (!executable));
    pfec.us = 1;
    return n;
  }

  n = std::min(static_cast<Waddr>(4096 - lowbits(addr, 12)), static_cast<Waddr>(bytes));

  void* mapped_addr = asp.page_virt_to_mapped(addr);
  assert(mapped_addr);
  logging::println("VMEM: Read {} = {}[{:016x}]", mapped_addr, *reinterpret_cast<W8*>(mapped_addr),
                   *reinterpret_cast<W8*>(mapped_addr));
  logging::flush();
  std::memcpy(target, mapped_addr, n);

  if likely (n == bytes)
    return n;

  readable = asp.fastcheck(reinterpret_cast<byte*>(addr + n), asp.readmap);
  executable = true;
  if likely (forexec)
    executable = asp.fastcheck(reinterpret_cast<byte*>(addr + n), asp.execmap);

  if unlikely ((!readable) | (forexec & !executable)) {
    faultaddr = addr + n;
    pfec.p = readable;
    pfec.nx = (forexec & (!executable));
    pfec.us = 1;
    return n;
  }

  std::memcpy(reinterpret_cast<byte*>(target) + n, asp.page_virt_to_mapped(addr + n), bytes - n);
  return bytes;
}

int Context::copy_to_user(Waddr target, void* source, int bytes, PageFaultErrorCode& pfec, Waddr& faultaddr) {
  AddressSpace& asp = active_address_space();
  logging::println("VMEM: Write to user {} ({})", reinterpret_cast<void*>(target), bytes);
  logging::flush();

  pfec = 0;
  bool writable = asp.fastcheck(reinterpret_cast<byte*>(target), asp.writemap);
  if unlikely (!writable) {
    faultaddr = target;
    pfec.p = asp.fastcheck(reinterpret_cast<byte*>(target), asp.readmap);
    pfec.rw = 1;
    return 0;
  }

  byte* targetlo = reinterpret_cast<byte*>(asp.page_virt_to_mapped(target));
  int nlo = std::min(static_cast<Waddr>(4096 - lowbits(target, 12)), static_cast<Waddr>(bytes));

  smc_setdirty(target >> 12);

  if likely (nlo == bytes) {
    std::memcpy(targetlo, source, nlo);
    return bytes;
  }

  writable = asp.fastcheck(reinterpret_cast<byte*>(target + nlo), asp.writemap);
  if unlikely (!writable) {
    faultaddr = target + nlo;
    pfec.p = asp.fastcheck(reinterpret_cast<byte*>(target + nlo), asp.readmap);
    pfec.rw = 1;
    pfec.us = 1;
    return nlo;
  }

  std::memcpy(asp.page_virt_to_mapped(target + nlo), reinterpret_cast<byte*>(source) + nlo, bytes - nlo);
  std::memcpy(targetlo, source, nlo);

  smc_setdirty((target + nlo) >> 12);
  return bytes;
}

Waddr Context::check_and_translate(Waddr virtaddr, int sizeshift, bool store, bool internal, int& exception,
                                   PageFaultErrorCode& pfec, PTEUpdate& pteupdate, Level1PTE& pteused) {
  AddressSpace& asp = active_address_space();
  exception = 0;
  pteupdate = 0;
  pteused = 0;
  pfec = 0;

  if unlikely (lowbits(virtaddr, sizeshift)) {
    exception = EXCEPTION_UnalignedAccess;
    return INVALID_PHYSADDR;
  }

  if unlikely (internal)
    return virtaddr;

  AddressSpace::spat_t top = store ? asp.writemap : asp.readmap;
  if unlikely (!asp.fastcheck(virtaddr, top)) {
    exception = store ? EXCEPTION_PageFaultOnWrite : EXCEPTION_PageFaultOnRead;
    pfec.p = asp.fastcheck(virtaddr, asp.readmap);
    pfec.rw = store;
    pfec.us = 1;
    return 0;
  }

  return reinterpret_cast<Waddr>(asp.page_virt_to_mapped(floor(signext64(virtaddr, 48), 8)));
}

int Context::write_segreg(unsigned int, W16) {
  return EXCEPTION_x86_gp_fault;
}

void Context::update_shadow_segment_descriptors() {
  W64 limit = use64 ? 0xffffffffffffffffULL : 0xffffffffULL;

  for (SegmentDescriptorCache* seg_cache : {&seg[SEGID_CS], &seg[SEGID_SS], &seg[SEGID_DS], &seg[SEGID_ES],
                                            &seg[SEGID_FS], &seg[SEGID_GS]}) {
    seg_cache->present = 1;
    seg_cache->base = 0;
    seg_cache->limit = limit;
  }

  virt_addr_mask = limit;
}

RIPVirtPhys& RIPVirtPhys::update(Context& ctx, int bytes) {
  use64 = ctx.use64;
  kernel = 0;
  df = ((ctx.internal_eflags & FLAG_DF) != 0);
  padlo = 0;
  padhi = 0;
  mfnlo = rip >> 12;
  mfnhi = (rip + (bytes - 1)) >> 12;
  return *this;
}

void assist_ptlcall(Context& ctx) {
  requested_switch_to_native = 1;
  ctx.commitarf[REG_rip] = ctx.commitarf[REG_nextrip];
}

CPU::CPU(HostCallbacks& callbacks, Options options)
    : callbacks_(callbacks), options_(std::move(options)), context_(std::make_unique<Context>()),
      address_space_(std::make_unique<AddressSpace>()) {
  config.reset();
  init_uops();

  config.log_filename = options_.log_file.string();
  logging::set_file_sink(config.log_filename.c_str());

  switch (options_.core) {
  case CoreModel::sequential:
    config.core_name = "seq";
    machine_ = std::make_unique<SequentialMachine>(config);
    break;
  case CoreModel::out_of_order:
    config.core_name = "ooo";
    machine_ = std::make_unique<OutOfOrderModel::OutOfOrderMachine>(config);
    break;
  }

  context_->no_sse = !options_.sse;
  context_->no_x87 = !options_.x87;
  config.perfect_cache = options_.perfect_cache;
  config.static_branchpred = options_.static_branch_prediction;
}

std::expected<void, MemoryError> CPU::map(address_t start, std::uint64_t size, Protection prot) noexcept {
  if (size == 0)
    return std::unexpected(MemoryError::zero_size);

  try {
    address_space_->map(start, size, prot);
  } catch (const std::bad_alloc&) {
    return std::unexpected(MemoryError::out_of_memory);
  } catch (...) {
    return std::unexpected(MemoryError::mapping_failed);
  }
  return {};
}

void CPU::unmap(address_t start, std::uint64_t size) noexcept {
  if (size == 0)
    return;
  address_space_->unmap(start, size);
}

std::expected<void, MemoryError> CPU::write_memory(address_t start, std::span<const std::byte> bytes) noexcept {
  std::uint64_t processed = 0;
  while (processed < bytes.size()) {
    address_t addr = start + processed;
    auto* dst = static_cast<std::byte*>(address_space_->page_virt_to_mapped(addr));
    if (!dst)
      return std::unexpected(MemoryError::unmapped_address);

    auto chunk = std::min<std::uint64_t>(bytes.size() - processed, kPageSize - (addr & (kPageSize - 1)));
    std::memcpy(dst, bytes.data() + processed, chunk);
    processed += chunk;
  }
  return {};
}

std::expected<std::span<const std::byte>, MemoryError> CPU::read_page(address_t page_aligned_address) const noexcept {
  static std::array<std::byte, kPageSize> zero_page{};

  if ((page_aligned_address & (kPageSize - 1)) != 0)
    return std::unexpected(MemoryError::unaligned_address);

  auto* src = static_cast<const std::byte*>(address_space_->page_virt_to_mapped(page_aligned_address));
  if (!src)
    src = zero_page.data();

  return std::span<const std::byte>(src, kPageSize);
}

RegisterRef CPU::operator[](Register reg) noexcept {
  return RegisterRef(*this, reg);
}

word_t CPU::operator[](Register reg) const noexcept {
  return context_->commitarf[static_cast<int>(reg)];
}

XmmRegisterRef CPU::operator[](XmmRegister reg) noexcept {
  return XmmRegisterRef(*this, reg);
}

XmmValue CPU::operator[](XmmRegister reg) const noexcept {
  const int lo = static_cast<int>(reg);
  return {.lo = context_->commitarf[lo], .hi = context_->commitarf[lo + 1]};
}

RunResult CPU::run(RunOptions options) {
  pending_stop_.reset();

  const auto stop_at = options.instruction_limit ? total_user_insns_committed + *options.instruction_limit
                                                 : std::numeric_limits<W64>::max();
  config.stop_at_user_insns = stop_at;


    simulate(*machine_);
    // todo: exception
    return {
        .reason = StopReason::x86_exception,
        .stats = stats(),
        .x86_exception = exception.exception,
        .message = exception.exception.message,
    };

  if (options.instruction_limit && stats().instructions >= stop_at)
    return {.reason = StopReason::instruction_limit, .stats = stats(), .x86_exception = std::nullopt, .message = {}};

  if (pending_stop_) {
    pending_stop_->stats = stats();
    return *pending_stop_;
  }

  return {.reason = StopReason::guest_exit, .stats = stats(), .x86_exception = std::nullopt, .message = {}};
}

Stats CPU::stats() const noexcept {
  return current_stats();
}

RegisterRef& RegisterRef::operator=(word_t value) noexcept {
  detail::context(*cpu_).commitarf[static_cast<int>(reg_)] = value;
  return *this;
}

RegisterRef::operator word_t() const noexcept {
  return detail::context(*cpu_).commitarf[static_cast<int>(reg_)];
}

XmmRegisterRef& XmmRegisterRef::operator=(XmmValue value) noexcept {
  const int lo = static_cast<int>(reg_);
  Context& context = detail::context(*cpu_);
  context.commitarf[lo] = value.lo;
  context.commitarf[lo + 1] = value.hi;
  return *this;
}

XmmRegisterRef::operator XmmValue() const noexcept {
  const int lo = static_cast<int>(reg_);
  Context& context = detail::context(*cpu_);
  return {.lo = context.commitarf[lo], .hi = context.commitarf[lo + 1]};
}

namespace detail {

Context& context(CPU& cpu) noexcept {
  return *cpu.context_;
}

const Context& context(const CPU& cpu) noexcept {
  return *cpu.context_;
}

AddressSpace& address_space(CPU& cpu) noexcept {
  return *cpu.address_space_;
}

const AddressSpace& address_space(const CPU& cpu) noexcept {
  return *cpu.address_space_;
}

SyscallResult dispatch_syscall(CPU& cpu, SyscallKind kind) noexcept {
  return cpu.callbacks_.syscall(cpu, kind);
}

CpuidResult dispatch_cpuid(CPU& cpu, CpuidRequest request) noexcept {
  return cpu.callbacks_.cpuid(cpu, request);
}

void set_pending_stop(CPU& cpu, RunResult result) {
  cpu.pending_stop_ = std::move(result);
}

std::string format_cpu(const CPU& cpu) {
  return std::format("{}", context(cpu));
}

} // namespace detail

[[noreturn]] void throw_x86_exception(Context& context, byte exception, W32 errorcode, Waddr virtaddr) {
  X86Exception result{
      .vector = exception,
      .error_code = errorcode,
      .virtual_address = virtaddr,
      .rip = context.commitarf[REG_rip],
      .message = std::format("Exception {} code={} addr={} @ rip {}", exception, errorcode,
                             reinterpret_cast<void*>(static_cast<uintptr_t>(virtaddr)),
                             reinterpret_cast<void*>(static_cast<uintptr_t>(context.commitarf[REG_rip]))),
      .context = std::format("{}", context),
  };

  throw InternalX86Exception{std::move(result)};
}

void dispatch_syscall_64bit() {
  CPU& cpu = active_cpu();
  Context& context = detail::context(cpu);
  const W64 old_rip = context.commitarf[REG_rip];
  SyscallResult result = detail::dispatch_syscall(cpu, SyscallKind::syscall64);
  advance_default_syscall_rip_if_unchanged(cpu, SyscallKind::syscall64, old_rip);

  if (!result.continue_execution) {
    detail::set_pending_stop(
        cpu, RunResult{.reason = result.reason, .stats = current_stats(), .x86_exception = std::nullopt, .message = result.message});
    requested_switch_to_native = 1;
  }
}

void dispatch_syscall_32bit(int semantics) {
  CPU& cpu = active_cpu();
  Context& context = detail::context(cpu);
  const SyscallKind kind = to_syscall_kind(semantics);
  const W64 old_rip = context.commitarf[REG_rip];
  SyscallResult result = detail::dispatch_syscall(cpu, kind);
  advance_default_syscall_rip_if_unchanged(cpu, kind, old_rip);

  if (!result.continue_execution) {
    detail::set_pending_stop(
        cpu, RunResult{.reason = result.reason, .stats = current_stats(), .x86_exception = std::nullopt, .message = result.message});
    requested_switch_to_native = 1;
  }
}

CpuidResult dispatch_cpuid(W32 func, W32 subfunc) {
  CPU& cpu = active_cpu();
  vcore::CpuidResult result = detail::dispatch_cpuid(cpu, {.function = func, .subfunction = subfunc});
  return {.eax = result.eax, .ebx = result.ebx, .ecx = result.ecx, .edx = result.edx};
}

} // namespace vcore

namespace vcore {

void Context::propagate_x86_exception(byte exception, W32 errorcode, Waddr virtaddr) {
  throw_x86_exception(*this, exception, errorcode, virtaddr);
}

void handle_syscall_64bit() {
  dispatch_syscall_64bit();
}

void handle_syscall_32bit(int semantics) {
  dispatch_syscall_32bit(semantics);
}

CpuidResult handle_cpuid(W32 func, W32 subfunc) {
  return dispatch_cpuid(func, subfunc);
}

} // namespace vcore
