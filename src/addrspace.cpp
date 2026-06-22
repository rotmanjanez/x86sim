#include "addrspace.h"

#include <cstdlib>
#include <cstring>
//
// Shadow page accessibility table format (x86-64 only):
// Top level:  1048576 bytes: 131072 64-bit pointers to chunks
//
// Leaf level: 65536 bytes per chunk: 524288 bits, one per 4 KB page
// Total: 131072 chunks x 524288 pages per chunk x 4 KB per page = 48 bits virtual address space
// Total: 17 bits       + 19 bits                + 12 bits       = 48 bits virtual address space
//

byte& AddressSpace::pageid_to_map_byte(spat_t top, Waddr pageid) {
  W64 chunkid = pageid >> log2(SPAT_PAGES_PER_CHUNK);

  if (!top[chunkid]) {
    top[chunkid] = (SPATChunk*)std::aligned_alloc(PAGE_SIZE, ceil((Waddr)SPAT_BYTES_PER_CHUNK, PAGE_SIZE));
    if (top[chunkid])
      std::memset(top[chunkid], 0, SPAT_BYTES_PER_CHUNK);
  }
  SPATChunk& chunk = *top[chunkid];
  W64 byteid = bits(pageid, 3, log2(SPAT_BYTES_PER_CHUNK));
  assert(byteid <= SPAT_BYTES_PER_CHUNK);
  return chunk[byteid];
}

void AddressSpace::make_accessible(void* p, Waddr size, spat_t top) {
  Waddr address = lowbits((Waddr)p, ADDRESS_SPACE_BITS);
  Waddr firstpage = (Waddr)address >> log2(PAGE_SIZE);
  Waddr lastpage = ((Waddr)address + size - 1) >> log2(PAGE_SIZE);
  logging::println("SPT: Making byte range {} to {} (size {}) accessible for {}", (void*)(firstpage << log2(PAGE_SIZE)),
                   (void*)(lastpage << log2(PAGE_SIZE)), size,
                   ((top == readmap)    ? "read"
                    : (top == writemap) ? "write"
                    : (top == execmap)  ? "exec"
                                        : "UNKNOWN"));
  logging::flush();
  assert(ceil((W64)address + size, PAGE_SIZE) <= ADDRESS_SPACE_SIZE);
  for (W64 i = firstpage; i <= lastpage; i++) {
    setbit(pageid_to_map_byte(top, i), lowbits(i, 3));
  }
}

void AddressSpace::make_inaccessible(void* p, Waddr size, spat_t top) {
  Waddr address = lowbits((Waddr)p, ADDRESS_SPACE_BITS);
  Waddr firstpage = (Waddr)address >> log2(PAGE_SIZE);
  Waddr lastpage = ((Waddr)address + size - 1) >> log2(PAGE_SIZE);

  logging::println("SPT: Making byte range {} to {} (size {}) inaccessible for {}",
                   (void*)(firstpage << log2(PAGE_SIZE)), (void*)(lastpage << log2(PAGE_SIZE)), size,
                   ((top == readmap)    ? "read"
                    : (top == writemap) ? "write"
                    : (top == execmap)  ? "exec"
                                        : "UNKNOWN"));
  logging::flush();

  assert(ceil((W64)address + size, PAGE_SIZE) <= ADDRESS_SPACE_SIZE);
  for (Waddr i = firstpage; i <= lastpage; i++) {
    clearbit(pageid_to_map_byte(top, i), lowbits(i, 3));
  }
}


AddressSpace::spat_t AddressSpace::allocmap() {
  constexpr Waddr bytes = SPAT_TOPLEVEL_CHUNKS * sizeof(SPATChunk*);

  spat_t top = (spat_t)std::aligned_alloc(PAGE_SIZE, ceil(bytes, PAGE_SIZE));
  if (top)
    std::memset(top, 0, bytes);
  return top;
}
void AddressSpace::freemap(AddressSpace::spat_t top) {
  if (top) {
    foreach (i, SPAT_TOPLEVEL_CHUNKS) {
      if (top[i])
        std::free(top[i]);
    }
    std::free(top);
  }
}

void AddressSpace::reset() {
  freemap(readmap);
  freemap(writemap);
  freemap(execmap);
  freemap(dirtymap);
  mapped_mem.clear();

  readmap = allocmap();
  writemap = allocmap();
  execmap = allocmap();
  dirtymap = allocmap();
}

void AddressSpace::setattr(void* start, Waddr length, int prot) {
  logging::println("setattr: region {} to {} ({} KB) has user-visible attributes {}{}{}", start,
                   (void*)((char*)start + length), length >> 10, ((prot & PROT_READ) ? 'r' : '-'),
                   ((prot & PROT_WRITE) ? 'w' : '-'), ((prot & PROT_EXEC) ? 'x' : '-'));

  if (prot & PROT_READ)
    allow_read(start, length);
  else
    disallow_read(start, length);

  if (prot & PROT_WRITE)
    allow_write(start, length);
  else
    disallow_write(start, length);

  if (prot & PROT_EXEC)
    allow_exec(start, length);
  else
    disallow_exec(start, length);
}

int AddressSpace::getattr(void* addr) {
  Waddr address = lowbits((Waddr)addr, ADDRESS_SPACE_BITS);

  Waddr page = pageid(address);

  int prot = (bit(pageid_to_map_byte(readmap, page), lowbits(page, 3)) ? PROT_READ : 0) |
             (bit(pageid_to_map_byte(writemap, page), lowbits(page, 3)) ? PROT_WRITE : 0) |
             (bit(pageid_to_map_byte(execmap, page), lowbits(page, 3)) ? PROT_EXEC : 0);

  return prot;
}
