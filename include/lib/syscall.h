#pragma once

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unios/ipc.h>
#include <unios/sync.h>

// --- 进程管理 (Process Management) ---
int get_ticks(void);
int get_pid(void);
int get_ppid(void);
int fork(void);
int execve(const char *path, char *const *argv, char *const *envp);
int wait(int *wstatus);
void exit(int exit_code);
void yield(void);
void sleep(int n);
int killerabbit(int pid);

// --- 内存管理 (Memory Management) ---
void *malloc(int size);
void free(void *ptr);

// --- 文件系统 (File System) ---
int open(const char *path, int flags);
int close(int fd);
int read(int fd, void *buf, int count);
int write(int fd, const void *buf, int count);
int lseek(int fd, int offset, int whence);
int unlink(const char *path);
int create(const char *path);
int delete(const char *path);
int opendir(const char *path);
int createdir(const char *path);
int deletedir(const char *path);

// --- 环境变量 (Environment) ---
bool putenv(char *const *envp);
char *const *getenv(void);

// --- 内核对象 (Kernel Objects) ---
handle_t krnlobj_lookup(int user_id);
handle_t krnlobj_create(int user_id);
void krnlobj_destroy(handle_t handle);
void krnlobj_lock(int user_id);
void krnlobj_unlock(int user_id);

// --- 窗口 (Window) ---
int get_root_window_handle();
int open_window(int x, int y, int w, int h, const char* title, uint32_t bg_color);
bool close_window(int handle);
bool refresh_window(int handle);
bool refresh_all_window();
bool set_window_surface_buffer(int handle, void **win_surface_buffer);

// --- 绘图 (Graphics) ---
bool fill_rect(int x, int y, int w, int h, uint32_t color);

// --- 进程间通信 (IPC) ---
int sendrec(int function, int src_dest, message_t* m);
