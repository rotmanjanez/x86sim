#include <addrspace.h>
//
// Shadow page accessibility table format (x86-64 only):
// Top level:  1048576 bytes: 131072 64-bit pointers to chunks
//
// Leaf level: 65536 bytes per chunk: 524288 bits, one per 4 KB page
// Total: 131072 chunks x 524288 pages per chunk x 4 KB per page = 48 bits virtual address space
// Total: 17 bits       + 19 bits                + 12 bits       = 48 bits virtual address space
//
// In 32-bit version, SPAT is a flat 131072-byte bit vector.
//

byte& AddressSpace::pageid_to_map_byte(spat_t top, Waddr pageid) {
#ifdef PTLSIM_AMD64
  W64 chunkid = pageid >> log2(SPAT_PAGES_PER_CHUNK);

  if (!top[chunkid]) {
    top[chunkid] = (SPATChunk*)ptl_mm_alloc_private_pages(SPAT_BYTES_PER_CHUNK);
  }
  SPATChunk& chunk = *top[chunkid];
  W64 byteid = bits(pageid, 3, log2(SPAT_BYTES_PER_CHUNK));
  assert(byteid <= SPAT_BYTES_PER_CHUNK);
  return chunk[byteid];
#else
  return top[pageid >> 3];
#endif
}

void AddressSpace::make_accessible(void* p, Waddr size, spat_t top) {
  Waddr address = lowbits((Waddr)p, ADDRESS_SPACE_BITS);
  Waddr firstpage = (Waddr)address >> log2(PAGE_SIZE);
  Waddr lastpage = ((Waddr)address + size - 1) >> log2(PAGE_SIZE);
  if (logable(1)) {
    logfile << "SPT: Making byte range ", (void*)(firstpage << log2(PAGE_SIZE)), " to ",
        (void*)(lastpage << log2(PAGE_SIZE)), " (size ", size, ") accessible for ",
        ((top == readmap)    ? "read"
         : (top == writemap) ? "write"
         : (top == execmap)  ? "exec"
                             : "UNKNOWN"),
        endl, flush;
  }
  assert(ceil((W64)address + size, PAGE_SIZE) <= ADDRESS_SPACE_SIZE);
  for (W64 i = firstpage; i <= lastpage; i++) {
    setbit(pageid_to_map_byte(top, i), lowbits(i, 3));
  }
}

void AddressSpace::make_inaccessible(void* p, Waddr size, spat_t top) {
  Waddr address = lowbits((Waddr)p, ADDRESS_SPACE_BITS);
  Waddr firstpage = (Waddr)address >> log2(PAGE_SIZE);
  Waddr lastpage = ((Waddr)address + size - 1) >> log2(PAGE_SIZE);
  if (logable(1)) {
    logfile << "SPT: Making byte range ", (void*)(firstpage << log2(PAGE_SIZE)), " to ",
        (void*)(lastpage << log2(PAGE_SIZE)), " (size ", size, ") inaccessible for ",
        ((top == readmap)    ? "read"
         : (top == writemap) ? "write"
         : (top == execmap)  ? "exec"
                             : "UNKNOWN"),
        endl, flush;
  }
  assert(ceil((W64)address + size, PAGE_SIZE) <= ADDRESS_SPACE_SIZE);
  for (Waddr i = firstpage; i <= lastpage; i++) {
    clearbit(pageid_to_map_byte(top, i), lowbits(i, 3));
  }
}


AddressSpace::spat_t AddressSpace::allocmap() {
#ifdef PTLSIM_AMD64
  return (spat_t)ptl_mm_alloc_private_pages(SPAT_TOPLEVEL_CHUNKS * sizeof(SPATChunk*));
#else
  return (spat_t)ptl_mm_alloc_private_pages(SPAT_BYTES);
#endif
}
void AddressSpace::freemap(AddressSpace::spat_t top) {
#ifdef PTLSIM_AMD64
  if (top) {
    foreach (i, SPAT_TOPLEVEL_CHUNKS) {
      if (top[i])
        ptl_mm_free_private_pages(top[i], SPAT_BYTES_PER_CHUNK);
    }
    ptl_mm_free_private_pages(top, SPAT_TOPLEVEL_CHUNKS * sizeof(SPATChunk*));
  }
#else
  if (top) {
    ptl_mm_free_private_pages(top, SPAT_BYTES);
  }
#endif
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
  //
  // Check first if it's been assigned a non-stdin (> 0) filehandle,
  // since this may get called from ptlsim_preinit_entry before streams
  // have been set up.
  //
  if (logfile.filehandle() > 0) {
    logfile << "setattr: region ", start, " to ", (void*)((char*)start + length), " (", length >> 10,
        " KB) has user-visible attributes ", ((prot & PROT_READ) ? 'r' : '-'), ((prot & PROT_WRITE) ? 'w' : '-'),
        ((prot & PROT_EXEC) ? 'x' : '-'), endl;
  }

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
