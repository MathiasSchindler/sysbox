#pragma once

// Minimal types (avoid libc headers)
typedef unsigned long sb_usize;
typedef unsigned long sb_u64;
typedef long sb_i64;
typedef unsigned int sb_u32;
typedef int sb_i32;
typedef unsigned short sb_u16;
typedef unsigned char sb_u8;

#define SB_NORETURN __attribute__((noreturn))
#define SB_INLINE static inline __attribute__((always_inline))

// Linux x86_64 syscall numbers (subset)
#define SB_SYS_nanosleep 35
#define SB_SYS_read 0
#define SB_SYS_write 1
#define SB_SYS_fstat 5
#define SB_SYS_lseek 8
#define SB_SYS_close 3
#define SB_SYS_getuid 102
#define SB_SYS_getgid 104
#define SB_SYS_getgroups 115
#define SB_SYS_openat 257
#define SB_SYS_mkdirat 258
#define SB_SYS_fchownat 260
#define SB_SYS_newfstatat 262
#define SB_SYS_unlinkat 263
#define SB_SYS_renameat 264
#define SB_SYS_linkat 265
#define SB_SYS_symlinkat 266
#define SB_SYS_readlinkat 267
#define SB_SYS_fchmodat 268
#define SB_SYS_faccessat 269
#define SB_SYS_getcwd 79
#define SB_SYS_getdents64 217
#define SB_SYS_clock_gettime 228
#define SB_SYS_kill 62
#define SB_SYS_uname 63
#define SB_SYS_sched_getaffinity 204
#define SB_SYS_execve 59
#define SB_SYS_vfork 58
#define SB_SYS_wait4 61
#define SB_SYS_pipe2 293
#define SB_SYS_dup2 33
#define SB_SYS_chdir 80
#define SB_SYS_mount 165
#define SB_SYS_statfs 137
#define SB_SYS_rt_sigaction 13
#define SB_SYS_utimensat 280
#define SB_SYS_exit 60
#define SB_SYS_exit_group 231

// openat flags/values (minimal subset)
#define SB_AT_FDCWD (-100)
#define SB_O_RDONLY 0
#define SB_O_WRONLY 1
#define SB_O_RDWR 2
#define SB_O_CREAT 0100
#define SB_O_APPEND 02000
#define SB_O_TRUNC 01000
#define SB_O_NOFOLLOW 0400000
#define SB_O_CLOEXEC 02000000
#define SB_O_DIRECTORY 0200000

// unlinkat flags
#define SB_AT_REMOVEDIR 0x200

// *at flags
#define SB_AT_SYMLINK_NOFOLLOW 0x100
// linkat flags
#define SB_AT_SYMLINK_FOLLOW 0x400

// faccessat flags
#define SB_AT_EACCESS 0x200

// access(2) mode bits
#define SB_F_OK 0
#define SB_X_OK 1
#define SB_W_OK 2
#define SB_R_OK 4

// Minimal errno values (Linux)
#define SB_ENOENT 2
#define SB_EPERM 1
#define SB_EINTR 4
#define SB_EEXIST 17
#define SB_EPIPE 32
#define SB_EXDEV 18
#define SB_ENOTDIR 20
#define SB_EISDIR 21
#define SB_EINVAL 22
#define SB_ESPIPE 29
#define SB_ELOOP 40
#define SB_ENOTEMPTY 39

// lseek whence
#define SB_SEEK_SET 0
#define SB_SEEK_CUR 1
#define SB_SEEK_END 2

// Signals (minimal)
#define SB_SIGPIPE 13

// clock_gettime clocks
#define SB_CLOCK_REALTIME 0
#define SB_CLOCK_MONOTONIC 1

// Kernel ABI timespec (x86_64)
struct sb_timespec {
	sb_i64 tv_sec;
	sb_i64 tv_nsec;
};

// uname(2)
struct sb_utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

// statfs(2) (Linux kernel ABI)
struct sb_statfs {
	sb_i64 f_type;
	sb_i64 f_bsize;
	sb_u64 f_blocks;
	sb_u64 f_bfree;
	sb_u64 f_bavail;
	sb_u64 f_files;
	sb_u64 f_ffree;
	sb_i64 f_fsid[2];
	sb_i64 f_namelen;
	sb_i64 f_frsize;
	sb_i64 f_flags;
	sb_i64 f_spare[4];
};

// rt_sigaction(2) (minimal; only used to ignore SIGPIPE)
struct sb_sigaction {
	void (*sa_handler)(int);
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	unsigned long sa_mask[1];
};

// File type bits (st_mode)
#define SB_S_IFMT 0170000
#define SB_S_IFREG 0100000
#define SB_S_IFDIR 0040000
#define SB_S_IFLNK 0120000

// getdents64(2) entry (Linux kernel ABI)
struct sb_linux_dirent64 {
	sb_u64 d_ino;
	sb_i64 d_off;
	sb_u16 d_reclen;
	sb_u8 d_type;
	char d_name[];
} __attribute__((packed));

// Generic syscall helpers (Linux x86_64)
SB_INLINE sb_i64 sb_syscall0(sb_i64 n) {
	sb_i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
	return ret;
}

SB_INLINE sb_i64 sb_syscall1(sb_i64 n, sb_i64 a1) {
	sb_i64 ret;
	__asm__ volatile("syscall"
		: "=a"(ret)
		: "a"(n), "D"(a1)
		: "rcx", "r11", "memory");
	return ret;
}

SB_INLINE sb_i64 sb_syscall2(sb_i64 n, sb_i64 a1, sb_i64 a2) {
	sb_i64 ret;
	__asm__ volatile("syscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2)
		: "rcx", "r11", "memory");
	return ret;
}

SB_INLINE sb_i64 sb_syscall3(sb_i64 n, sb_i64 a1, sb_i64 a2, sb_i64 a3) {
	sb_i64 ret;
	__asm__ volatile("syscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3)
		: "rcx", "r11", "memory");
	return ret;
}

SB_INLINE sb_i64 sb_syscall4(sb_i64 n, sb_i64 a1, sb_i64 a2, sb_i64 a3, sb_i64 a4) {
	sb_i64 ret;
	register sb_i64 r10 __asm__("r10") = a4;
	__asm__ volatile("syscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
		: "rcx", "r11", "memory");
	return ret;
}

SB_INLINE sb_i64 sb_syscall5(sb_i64 n, sb_i64 a1, sb_i64 a2, sb_i64 a3, sb_i64 a4, sb_i64 a5) {
	sb_i64 ret;
	register sb_i64 r10 __asm__("r10") = a4;
	register sb_i64 r8 __asm__("r8") = a5;
	__asm__ volatile("syscall"
		: "=a"(ret)
		: "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
		: "rcx", "r11", "memory");
	return ret;
}

// Common syscall wrappers
SB_INLINE sb_i64 sb_sys_read(sb_i32 fd, void *buf, sb_usize len) {
	return sb_syscall3(SB_SYS_read, (sb_i64)fd, (sb_i64)buf, (sb_i64)len);
}

SB_INLINE sb_i64 sb_sys_write(sb_i32 fd, const void *buf, sb_usize len) {
	return sb_syscall3(SB_SYS_write, (sb_i64)fd, (sb_i64)buf, (sb_i64)len);
}

SB_INLINE sb_i64 sb_sys_getuid(void) {
	return sb_syscall0(SB_SYS_getuid);
}

SB_INLINE sb_i64 sb_sys_getgid(void) {
	return sb_syscall0(SB_SYS_getgid);
}

SB_INLINE sb_i64 sb_sys_getgroups(sb_i32 size, sb_u32 *list) {
	return sb_syscall2(SB_SYS_getgroups, (sb_i64)size, (sb_i64)list);
}

// Linux x86_64 struct stat (kernel ABI). Only st_mode is used by sysbox currently.
struct sb_stat {
	sb_u64 st_dev;
	sb_u64 st_ino;
	sb_u64 st_nlink;
	sb_u32 st_mode;
	sb_u32 st_uid;
	sb_u32 st_gid;
	sb_u32 __pad0;
	sb_u64 st_rdev;
	sb_i64 st_size;
	sb_i64 st_blksize;
	sb_i64 st_blocks;
	sb_u64 st_atime;
	sb_u64 st_atime_nsec;
	sb_u64 st_mtime;
	sb_u64 st_mtime_nsec;
	sb_u64 st_ctime;
	sb_u64 st_ctime_nsec;
	sb_i64 __unused[3];
};

SB_INLINE sb_i64 sb_sys_fstat(sb_i32 fd, struct sb_stat *st) {
	return sb_syscall2(SB_SYS_fstat, (sb_i64)fd, (sb_i64)st);
}

SB_INLINE sb_i64 sb_sys_lseek(sb_i32 fd, sb_i64 offset, sb_i32 whence) {
	return sb_syscall3(SB_SYS_lseek, (sb_i64)fd, (sb_i64)offset, (sb_i64)whence);
}

SB_INLINE sb_i64 sb_sys_newfstatat(sb_i32 dirfd, const char *path, struct sb_stat *st, sb_i32 flags) {
	return sb_syscall4(SB_SYS_newfstatat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)st, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_fchmodat(sb_i32 dirfd, const char *path, sb_u32 mode, sb_i32 flags) {
	return sb_syscall4(SB_SYS_fchmodat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)mode, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_faccessat(sb_i32 dirfd, const char *path, sb_i32 mode, sb_i32 flags) {
	return sb_syscall4(SB_SYS_faccessat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)mode, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_fchownat(sb_i32 dirfd, const char *path, sb_u32 uid, sb_u32 gid, sb_i32 flags) {
	return sb_syscall5(SB_SYS_fchownat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)uid, (sb_i64)gid, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_close(sb_i32 fd) {
	return sb_syscall1(SB_SYS_close, (sb_i64)fd);
}

SB_INLINE sb_i64 sb_sys_openat(sb_i32 dirfd, const char *path, sb_i32 flags, sb_u32 mode) {
	return sb_syscall4(SB_SYS_openat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)flags, (sb_i64)mode);
}

SB_INLINE sb_i64 sb_sys_mkdirat(sb_i32 dirfd, const char *path, sb_u32 mode) {
	return sb_syscall3(SB_SYS_mkdirat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)mode);
}

SB_INLINE sb_i64 sb_sys_unlinkat(sb_i32 dirfd, const char *path, sb_i32 flags) {
	return sb_syscall3(SB_SYS_unlinkat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_renameat(sb_i32 olddirfd, const char *oldpath, sb_i32 newdirfd, const char *newpath) {
	return sb_syscall4(SB_SYS_renameat, (sb_i64)olddirfd, (sb_i64)oldpath, (sb_i64)newdirfd, (sb_i64)newpath);
}

SB_INLINE sb_i64 sb_sys_getcwd(char *buf, sb_usize size) {
	return sb_syscall2(SB_SYS_getcwd, (sb_i64)buf, (sb_i64)size);
}

SB_INLINE sb_i64 sb_sys_getdents64(sb_i32 fd, void *dirp, sb_u32 count) {
	return sb_syscall3(SB_SYS_getdents64, (sb_i64)fd, (sb_i64)dirp, (sb_i64)count);
}

SB_INLINE sb_i64 sb_sys_linkat(sb_i32 olddirfd, const char *oldpath, sb_i32 newdirfd, const char *newpath, sb_i32 flags) {
	return sb_syscall5(SB_SYS_linkat, (sb_i64)olddirfd, (sb_i64)oldpath, (sb_i64)newdirfd, (sb_i64)newpath, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_symlinkat(const char *target, sb_i32 newdirfd, const char *linkpath) {
	return sb_syscall3(SB_SYS_symlinkat, (sb_i64)target, (sb_i64)newdirfd, (sb_i64)linkpath);
}

SB_INLINE sb_i64 sb_sys_readlinkat(sb_i32 dirfd, const char *path, char *buf, sb_usize bufsz) {
	return sb_syscall4(SB_SYS_readlinkat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)buf, (sb_i64)bufsz);
}

SB_INLINE sb_i64 sb_sys_clock_gettime(sb_i32 clockid, struct sb_timespec *tp) {
	return sb_syscall2(SB_SYS_clock_gettime, (sb_i64)clockid, (sb_i64)tp);
}

SB_INLINE sb_i64 sb_sys_nanosleep(const struct sb_timespec *req, struct sb_timespec *rem) {
	return sb_syscall2(SB_SYS_nanosleep, (sb_i64)req, (sb_i64)rem);
}

SB_INLINE sb_i64 sb_sys_uname(struct sb_utsname *buf) {
	return sb_syscall1(SB_SYS_uname, (sb_i64)buf);
}

SB_INLINE sb_i64 sb_sys_statfs(const char *path, struct sb_statfs *buf) {
	return sb_syscall2(SB_SYS_statfs, (sb_i64)path, (sb_i64)buf);
}

SB_INLINE sb_i64 sb_sys_rt_sigaction(sb_i32 signum, const struct sb_sigaction *act, struct sb_sigaction *oldact, sb_usize sigsetsize) {
	return sb_syscall4(SB_SYS_rt_sigaction, (sb_i64)signum, (sb_i64)act, (sb_i64)oldact, (sb_i64)sigsetsize);
}

SB_INLINE sb_i64 sb_sys_kill(sb_i32 pid, sb_i32 sig) {
	return sb_syscall2(SB_SYS_kill, (sb_i64)pid, (sb_i64)sig);
}

SB_INLINE sb_i64 sb_sys_utimensat(sb_i32 dirfd, const char *path, const struct sb_timespec times[2], sb_i32 flags) {
	return sb_syscall4(SB_SYS_utimensat, (sb_i64)dirfd, (sb_i64)path, (sb_i64)times, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_execve(const char *pathname, char *const argv[], char *const envp[]) {
	return sb_syscall3(SB_SYS_execve, (sb_i64)pathname, (sb_i64)argv, (sb_i64)envp);
}

SB_INLINE sb_i64 sb_sys_pipe2(sb_i32 pipefd[2], sb_i32 flags) {
	return sb_syscall2(SB_SYS_pipe2, (sb_i64)pipefd, (sb_i64)flags);
}

SB_INLINE sb_i64 sb_sys_dup2(sb_i32 oldfd, sb_i32 newfd) {
	return sb_syscall2(SB_SYS_dup2, (sb_i64)oldfd, (sb_i64)newfd);
}

SB_INLINE sb_i64 sb_sys_mount(const char *source, const char *target, const char *filesystemtype, sb_u64 mountflags, const void *data) {
	return sb_syscall5(SB_SYS_mount, (sb_i64)source, (sb_i64)target, (sb_i64)filesystemtype, (sb_i64)mountflags, (sb_i64)data);
}

SB_INLINE sb_i64 sb_sys_chdir(const char *path) {
	return sb_syscall1(SB_SYS_chdir, (sb_i64)path);
}

SB_INLINE sb_i64 sb_sys_sched_getaffinity(sb_i32 pid, sb_usize cpusetsize, void *mask) {
	return sb_syscall3(SB_SYS_sched_getaffinity, (sb_i64)pid, (sb_i64)cpusetsize, (sb_i64)mask);
}

SB_INLINE sb_i64 sb_sys_vfork(void) {
	return sb_syscall0(SB_SYS_vfork);
}

SB_INLINE sb_i64 sb_sys_wait4(sb_i32 pid, sb_i32 *wstatus, sb_i32 options, void *rusage) {
	return sb_syscall4(SB_SYS_wait4, (sb_i64)pid, (sb_i64)wstatus, (sb_i64)options, (sb_i64)rusage);
}

SB_NORETURN void sb_exit(sb_i32 code);

// Tiny helpers
sb_usize sb_strlen(const char *s);
int sb_streq(const char *a, const char *b);
int sb_starts_with_n(const char *s, const char *pre, sb_usize n);
int sb_has_slash(const char *s);
int sb_is_dot_or_dotdot(const char *name);
const char *sb_getenv_kv(char **envp, const char *key_eq);
int sb_parse_u64_dec(const char *s, sb_u64 *out);
int sb_parse_u32_dec(const char *s, sb_u32 *out);
int sb_parse_u32_octal(const char *s, sb_u32 *out);
int sb_parse_i64_dec(const char *s, sb_i64 *out);
int sb_parse_uid_gid(const char *s, sb_u32 *out_uid, sb_u32 *out_gid);
sb_i64 sb_write_all(sb_i32 fd, const void *buf, sb_usize len);
sb_i64 sb_write_str(sb_i32 fd, const char *s);
void sb_write_hex_u64(sb_i32 fd, sb_u64 v);
sb_i64 sb_write_u64_dec(sb_i32 fd, sb_u64 v);
sb_i64 sb_write_i64_dec(sb_i32 fd, sb_i64 v);

// Common UX helpers
SB_NORETURN void sb_die_usage(const char *argv0, const char *usage);
SB_NORETURN void sb_die_errno(const char *argv0, const char *ctx, sb_i64 err_neg);
