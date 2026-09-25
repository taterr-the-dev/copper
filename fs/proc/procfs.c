#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/kmalloc.h>
#include <kernel/proc.h>
#include <kernel/console.h>
#include <kernel/syscall.h>

extern uint64_t timer_ticks;
extern int proc_get_task_fd_path(int pid, int fd, char *buf, size_t sz);

struct pbuf { char *data; size_t size; };

static void pstr(char *b, int *o, const char *s) { while (*s) b[(*o)++] = *s++; }
static void pdec(char *b, int *o, uint64_t v) {
    char t[21]; int i = 0;
    if (!v) t[i++] = '0';
    while (v) { t[i++] = '0' + v % 10; v /= 10; }
    while (i) b[(*o)++] = t[--i];
}

static struct task *find_task(int pid) {
    if (!current) return NULL;
    struct task *t = current;
    do {
        if (t->pid == (uint32_t)pid) return t; 
        t = t->next;
    } while (t != current);
    return NULL;
}

static const char *static_names[] = {"meminfo", "uptime", "version", "cpuinfo", "tasks", "mounts"};
#define STATIC_COUNT 6

static void gen_static(int idx, char *b, int *o) {
    switch (idx) {
    case 0: pstr(b, o, "MemTotal:       16384 kB\nMemFree:        12000 kB\n"); break;
    case 1: pdec(b, o, timer_ticks / 100); pstr(b, o, ".00 0.00 0\n"); break;
    case 2: pstr(b, o, "Copper kernel 0.1.0 x86_64\n"); break;
    case 3: pstr(b, o, "processor : 0\nvendor_id : GenuineKernel\nmodel name : Copper x86_64\n"); break;
    case 4: {
        if (!current) {
            pstr(b, o, "(no tasks yet)\n");
            break;
        }
        struct task *t = current;
        struct task *s = t;
        int count = 0;
        do {
            if (count++ > 64) break;
            if (!t->next || (uint64_t)t->next < 0x100000) break;
            pdec(b, o, t->pid);
            pstr(b, o, " ");
            pstr(b, o, t->name);
            pstr(b, o, "\n");
            t = t->next;
        } while (t != s);
        break;
    }
    case 5: {
        extern int vfs_get_mount(int, char *, int, char *, int);
        char path[256], name[32];
        for (int i = 0; i < 16; i++) {
            if (vfs_get_mount(i, path, sizeof(path), name, sizeof(name)) == 0) {
                pstr(b, o, "none "); pstr(b, o, path); pstr(b, o, " "); pstr(b, o, name); pstr(b, o, " rw 0 0\n");
            }
        }
        break;
    }
    }
}

static int parse_path(const char *path, int *pid_out, int *fd_out) {
    if (strcmp(path, "/") == 0 || strcmp(path, "/proc") == 0 || strcmp(path, "proc") == 0) return 0;
    
    const char *p = path;
    if (*p == '/') p++;
    if (strncmp(p, "proc/", 5) == 0) p += 5;
    else if (strcmp(p, "proc") == 0) return 0;

    int is_self = 0;
    if (strncmp(p, "self", 4) == 0 && (p[4] == '/' || p[4] == 0)) {
        *pid_out = current ? current->pid : 0;
        p += 4;
        is_self = 1;
        if (*p == '/') p++;
    }

    if (*p == 0) return 1;

    if (!is_self) {
        int pid = 0;
        int parsed = 0;
        while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; parsed = 1; }
        if (!parsed) {
            for (int i = 0; i < STATIC_COUNT; i++) {
                if (strcmp(p, static_names[i]) == 0) {
                    *pid_out = i;
                    return 3;
                }
            }
            return -1;
        }
        *pid_out = pid;
        if (*p == 0) return 1;
        if (*p == '/') p++;
    }

    if (strncmp(p, "fd", 2) == 0) {
        p += 2;
        if (*p == 0) return 2;
        if (*p == '/') {
            p++;
            int fd = 0;
            int parsed = 0;
            while (*p >= '0' && *p <= '9') { fd = fd * 10 + (*p - '0'); p++; parsed = 1; }
            if (parsed && *p == 0) {
                *fd_out = fd;
                return 4;
            }
        }
    }

    if (strcmp(p, "status") == 0) return 5;
    if (strcmp(p, "cmdline") == 0) return 6;

    return -1;
}

static int proc_open(void *sb, const char *path, struct fs_file *f) {
    (void)sb;
    int pid = 0, fd = 0;
    int type = parse_path(path, &pid, &fd);
    
    if (type == 0 || type == 1 || type == 2 || type == 4) {
        f->mode = (type == 4) ? 0120777 : 0040555;
        f->size = 0;
        f->pos = 0;
        f->priv = NULL;
        return 0;
    }

    char *b = kmalloc(1024);
    int o = 0;
    struct pbuf *pb = kmalloc(sizeof(*pb));

    if (type == 3) {
        gen_static(pid, b, &o);
    } else if (type == 5) {
        struct task *t = find_task(pid);
        if (!t) { kfree(b); kfree(pb); return -ENOENT; }
        pstr(b, &o, "Name:\t"); pstr(b, &o, t->name); pstr(b, &o, "\n");
        pstr(b, &o, "State:\tR (running)\n");
        pstr(b, &o, "Pid:\t"); pdec(b, &o, t->pid); pstr(b, &o, "\n");
    } else if (type == 6) {
        struct task *t = find_task(pid);
        if (!t) { kfree(b); kfree(pb); return -ENOENT; }
        pstr(b, &o, t->name); pstr(b, &o, "\0");
        o++;
    } else {
        kfree(b); kfree(pb);
        return -ENOENT;
    }

    pb->data = b; pb->size = o;
    f->priv = pb; f->size = o; f->pos = 0;
    f->mode = 0100444;
    return 0;
}

static int64_t proc_read(struct fs_file *f, void *out, size_t len) {
    if (!f->priv) return 0;
    struct pbuf *pb = f->priv;
    uint8_t *o = out; size_t d = 0;
    while (d < len && f->pos < pb->size) o[d++] = pb->data[f->pos++];
    return d;
}

static int proc_close(struct fs_file *f) {
    if (f->priv) {
        struct pbuf *pb = f->priv;
        if (pb->data) kfree(pb->data);
        kfree(pb);
        f->priv = NULL;
    }
    return 0;
}

static int proc_readdir(void *sb, const char *path, int idx, struct fs_dirent *out) {
    (void)sb;
    int pid = 0, fd = 0;
    int type = parse_path(path, &pid, &fd);

    if (type == 0) {
        if (idx < STATIC_COUNT) {
            strcpy(out->name, static_names[idx]);
            out->is_dir = 0; return 0;
        }
        idx -= STATIC_COUNT;
        if (idx == 0) { strcpy(out->name, "self"); out->is_dir = 1; return 0; }
        idx--;
        
        struct task *t = current; int count = 0;
        do {
            if (count == idx) {
                char tmp[16]; int o=0; pdec(tmp, &o, t->pid); tmp[o]=0;
                strcpy(out->name, tmp);
                out->is_dir = 1; return 0;
            }
            t = t->next; count++;
        } while (t != current);
        return -1;
    }
    
    if (type == 1) {
        const char *files[] = {"status", "cmdline", "fd"};
        if (idx < 3) { strcpy(out->name, files[idx]); out->is_dir = (idx == 2); return 0; }
        return -1;
    }

    if (type == 2) {
        if (pid < 0 || pid >= 64) return -1;
        char buf[128];
        int found = 0;
        for (int i = 0; i < 64; i++) {
            if (proc_get_task_fd_path(pid, i, buf, sizeof(buf)) == 0) {
                if (found == idx) {
                    char tmp[16]; int o=0; pdec(tmp, &o, i); tmp[o]=0;
                    strcpy(out->name, tmp);
                    out->is_dir = 0;
                    out->mode = 0120777;
                    return 0;
                }
                found++;
            }
        }
        return -1;
    }

    return -1;
}

static int proc_readlink(void *sb, const char *path, char *buf, size_t bufsz) {
    (void)sb;
    int pid = 0, fd = 0;
    int type = parse_path(path, &pid, &fd);

    const char *p = path;
    if (*p == '/') p++;
    if (strncmp(p, "proc/", 5) == 0) p += 5;
    if (strcmp(p, "self") == 0) {
        int o = 0; pdec(buf, &o, current->pid); buf[o] = 0; return o;
    }

    if (type == 4) {
        char tmp[128];
        if (proc_get_task_fd_path(pid, fd, tmp, sizeof(tmp)) == 0) {
            size_t len = strlen(tmp);
            if (len >= bufsz) len = bufsz - 1;
            memcpy(buf, tmp, len); buf[len] = 0;
            return len;
        }
    }
    return -EINVAL;
}

static int proc_mount(struct blkdev *d, void **sbp) { (void)d; *sbp = 0; return 0; }

struct fs_ops proc_fs = {
    .name = "procfs",
    .mount = proc_mount,
    .open = proc_open,
    .read = proc_read,
    .close = proc_close,
    .readdir = proc_readdir,
    .readlink = proc_readlink
};
