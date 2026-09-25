#ifndef _KERNEL_BINFMT_H
#define _KERNEL_BINFMT_H
#include <kernel/types.h>
#include <kernel/fs.h>
struct exec_info {
    char interpreter[128];
    char arg[64];
    uint64_t entry;
    int is_script;
};
struct binfmt {
    const char *name;
    int (*probe)(struct fs_file *);
    int (*load)(struct fs_file *, struct exec_info *);
};
void binfmt_register(struct binfmt *);
void binfmt_init(void);
int exec_load(const char *path, struct exec_info *ei);
#endif
