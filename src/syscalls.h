// -*- c++ -*-
//
// System Calls
//
// Copyright 2004-2008 Matt T. Yourst <yourst@yourst.com>
//
// This program is free software; it is licensed under the
// GNU General Public License, Version 2.
//

#ifndef __SYSCALLS_H__
#define __SYSCALLS_H__

extern "C" {
int sys_open(const char* pathname, int flags, int mode);
int sys_close(int fd);
ssize_t sys_read(int fd, void* buf, size_t count);
ssize_t sys_write(int fd, const void* buf, size_t count);
ssize_t sys_fdatasync(int fd);
W64 sys_seek(int fd, W64 offset, unsigned int origin);
int sys_unlink(const char* pathname);
int sys_rename(const char* oldpath, const char* newpath);

void* sys_mmap(void* start, size_t length, int prot, int flags, int fd, W64 offset);
int sys_munmap(void* start, size_t length);
void* sys_mremap(void* old_address, size_t old_size, size_t new_size, unsigned long flags);
int sys_mprotect(void* addr, size_t len, int prot);
int sys_madvise(void* addr, size_t len, int action);
int sys_mlock(const void* addr, size_t len);
int sys_munlock(const void* addr, size_t len);
int sys_mlockall(int flags);
int sys_munlockall(void);

pid_t sys_fork();
int sys_execve(const char* filename, const char** argv, const char** envp);

pid_t sys_gettid();
pid_t sys_getppid();
pid_t sys_getpid();
void sys_exit(int code);
void* sys_brk(void* newbrk);
int sys_readlink(const char* path, char* buf, size_t bufsiz);
W64 sys_nanosleep(W64 nsec);

struct utsname;
int sys_uname(struct utsname* buf);

void* malloc(size_t size) __attribute__((__malloc__));
void free(void* ptr);
char* getenv(const char* name);
int sys_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);

int sys_gettimeofday(struct timeval* tv, struct timezone* tz);
time_t sys_time(time_t* t);
pid_t sys_wait4(pid_t pid, int* status, int options, struct rusage* rusage);

typedef void (*kernel_sighandler_t)(int signo, siginfo_t* si, void* context);

// From glibc sysdeps/unix/sysv/linux/kernel_sigaction.h for kernels >= 2.2.x:
struct kernel_sigaction {
  kernel_sighandler_t k_sa_handler;
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  sigset_t sa_mask;
};

long sys_rt_sigaction(int sig, const struct kernel_sigaction* act, struct kernel_sigaction* oldact, size_t sigsetsize);
int sys_getrlimit(int resource, struct rlimit* rlim);
#ifdef PTLSIM_AMD64
W64 sys_arch_prctl(int code, void* addr);
W64 sys_ptrace(int request, pid_t pid, W64 addr, W64 data);
#else
int sys_get_thread_area(struct user_desc* u_info);
W32 sys_ptrace(int request, pid_t pid, W32 addr, W32 data);
#endif
};

#endif
