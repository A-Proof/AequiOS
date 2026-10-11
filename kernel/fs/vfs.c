/* LumenFS v0 — read-only in-memory filesystem over the initramfs (ustar).
 * Read/write support for real block devices is a roadmap item. */
#include <kernel.h>

struct tar_hdr {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

#define MAX_FILES 64

struct vfile {
    char name[256];
    const void *data;
    size_t size;
};

static struct vfile files[MAX_FILES];
static int nfiles;

extern volatile struct limine_module_request module_request;

static u64 octal(const char *s, int n) {
    u64 v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        v = v * 8 + (s[i] - '0');
    return v;
}

static void vfs_add(const char *name, const void *data, size_t size) {
    if (nfiles >= MAX_FILES) return;
    /* normalize: strip leading "./" and "/" */
    while (*name == '.' || *name == '/') name++;
    strncpy(files[nfiles].name, name, sizeof(files[nfiles].name) - 1);
    files[nfiles].name[sizeof(files[nfiles].name) - 1] = 0;
    /* strip trailing '/' */
    size_t l = strlen(files[nfiles].name);
    while (l && files[nfiles].name[l - 1] == '/') files[nfiles].name[--l] = 0;
    if (!l) return;
    files[nfiles].data = data;
    files[nfiles].size = size;
    nfiles++;
}

int vfs_init(void) {
    struct limine_module_response *mr = module_request.response;
    if (!mr || mr->module_count == 0) {
        early_printk("vfs: no modules loaded\n");
        return -1;
    }
    struct limine_file *mod = mr->modules[0];
    early_printk("vfs: initramfs '%s' %llu bytes\n", mod->path,
                 (unsigned long long)mod->size);

    const u8 *p = mod->address;
    const u8 *end = p + mod->size;
    while (p + 512 <= end) {
        const struct tar_hdr *h = (const struct tar_hdr *)p;
        if (h->name[0] == 0) break;
        u64 size = octal(h->size, sizeof(h->size));
        if (h->typeflag == '0' || h->typeflag == 0) {
            char full[256];
            if (h->prefix[0])
                snprintf(full, sizeof(full), "%s/%s", h->prefix, h->name);
            else
                strncpy(full, h->name, sizeof(full) - 1);
            vfs_add(full, p + 512, size);
        }
        p += 512 + ALIGN_UP(size, 512);
    }
    early_printk("vfs: %d files\n", nfiles);
    return 0;
}

static int find(const char *path) {
    while (*path == '.' || *path == '/') path++;
    for (int i = 0; i < nfiles; i++)
        if (strcmp(files[i].name, path) == 0) return i;
    return -1;
}

int vfs_open(const char *path) {
    int i = find(path);
    if (i < 0) return -1;
    return i;                      /* vfs id == index; per-task fds wrap this */
}

int vfs_read(int fd, void *buf, size_t n) {
    if (fd < 0 || fd >= nfiles) return -1;
    if (n > files[fd].size) n = files[fd].size;
    memcpy(buf, files[fd].data, n);
    return (int)n;
}

/* read at offset (task layer tracks offsets) */
int vfs_pread(int fd, void *buf, size_t n, u64 off) {
    if (fd < 0 || fd >= nfiles || off >= files[fd].size) return 0;
    if (n > files[fd].size - off) n = files[fd].size - off;
    memcpy(buf, (const u8 *)files[fd].data + off, n);
    return (int)n;
}

long vfs_size(int fd) {
    if (fd < 0 || fd >= nfiles) return -1;
    return (long)files[fd].size;
}

int vfs_close(int fd) { (void)fd; return 0; }

void vfs_list(void (*cb)(const char *name, size_t size)) {
    for (int i = 0; i < nfiles; i++) cb(files[i].name, files[i].size);
}
