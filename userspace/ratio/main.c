/* Ratio - the AequiOS shell. A calm serial-console REPL. */
#include <aequios.h>

static char line[160];

static void banner(void) {
    puts("");
    puts("  +------------------------------------------------+");
    puts("  |  R a t i o   -   A e q u i O S   s h e l l      |");
    puts("  +------------------------------------------------+");
    puts("  type 'help' for commands");
}

static void prompt(void) {
    puts("aequi> ");
}

static void readline(void) {
    int i = 0;
    for (;;) {
        char c;
        if (sys_read(0, &c, 1) <= 0) continue;
        if (c == '\r') c = '\n';
        if (c == '\n') { putc('\n'); line[i] = 0; return; }
        if (c == 0x7f || c == '\b') {
            if (i) { i--; puts("\b \b"); }
            continue;
        }
        if (i < (int)sizeof(line) - 1) { line[i++] = c; putc(c); }
    }
}

static int starts(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

static void skip_spaces(char **p) {
    while (**p == ' ') (*p)++;
}

static void cmd_help(void) {
    puts("commands:");
    puts("  help           - this text");
    puts("  uname          - kernel identity");
    puts("  meminfo        - free physical memory (KiB)");
    puts("  ls             - list initramfs files");
    puts("  cat <path>     - print a file");
    puts("  run <path>     - spawn a program, wait, print exit code");
    puts("  demo           - run the Linux-compat demo binary");
    puts("  reboot         - restart the machine");
}

static void cmd_cat(const char *path) {
    s64 fd = sys_open(path);
    if (fd < 0) { puts("cat: not found"); return; }
    char buf[256];
    for (;;) {
        s64 n = sys_read((int)fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(1, buf, n);
    }
    sys_close((int)fd);
}

static void run_and_report(const char *path) {
    uprintf("[ratio] spawn %s\n", path);
    s64 code = sys_spawn(path);
    if (code < 0) { uprintf("[ratio] spawn failed (%ld)\n", code); return; }
    uprintf("[ratio] %s exited with code %ld\n", path, code);
}

int main(void) {
    banner();

    /* auto-run the Linux compat demo once so `vmtest` can verify
     * end-to-end without any input */
    run_and_report("/bin/linux-hello");

    for (;;) {
        prompt();
        readline();
        char *p = line;
        skip_spaces(&p);
        if (!*p) continue;

        if (ustrcmp(p, "help") == 0)         cmd_help();
        else if (ustrcmp(p, "uname") == 0) {
            char b[128];
            sys_uname(b);
            puts(b);
        }
        else if (ustrcmp(p, "meminfo") == 0)
            uprintf("free memory: %ld KiB\n", sys_meminfo());
        else if (ustrcmp(p, "ls") == 0)      sys_ls();
        else if (ustrcmp(p, "demo") == 0)    run_and_report("/bin/linux-hello");
        else if (ustrcmp(p, "reboot") == 0)  sys_reboot();
        else if (starts(p, "cat "))          cmd_cat(p + 4);
        else if (starts(p, "run "))          run_and_report(p + 4);
        else uprintf("unknown command '%s' - try 'help'\n", p);
    }
    return 0;
}
