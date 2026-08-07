#include "mini_libc.h"
#include "mini_printf.h"
#include "mini_string.h"

/* Renders mode into a fixed "drwxrwxrwx"-style 10-char buffer (out
 * must have room for 11 bytes including the terminator). No printf
 * %o exists in this libc (see mini_printf.h), so this is spelled out
 * bit by bit instead of formatted. */
static void format_mode(int is_dir, unsigned long mode, char *out) {
    out[0] = is_dir ? 'd' : '-';
    const char *bits = "rwx";
    for (int group = 0; group < 3; group++) {
        unsigned long shift = (unsigned long)(2 - group) * 3;
        unsigned long triplet = (mode >> shift) & 0x7;
        for (int b = 0; b < 3; b++) {
            out[1 + group * 3 + b] = (triplet & (0x4UL >> b)) ? bits[b] : '-';
        }
    }
    out[10] = '\0';
}

static void join_path(const char *dir, const char *name, char *out, unsigned long out_size) {
    unsigned long i = 0;
    for (; dir[i] && i + 1 < out_size; i++) out[i] = dir[i];
    if (i == 0 || out[i - 1] != '/') { if (i + 1 < out_size) out[i++] = '/'; }
    unsigned long j = 0;
    while (name[j] && i + 1 < out_size) out[i++] = name[j++];
    out[i] = '\0';
}

int main(int argc, char **argv) {
    const char *path = 0;
    int long_format = 0;
    char cwd_buf[128];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) long_format = 1;
        else path = argv[i];
    }
    if (!path) {
        sys_getcwd(cwd_buf, sizeof(cwd_buf));
        path = cwd_buf;
    }

    char name[64];
    char full[196];
    char mode_str[11];
    int any = 0;

    for (int i = 0; ; i++) {
        long found = sys_readdir(path, i, name, sizeof(name));
        if (found <= 0) break;
        any = 1;

        if (!long_format) {
            printf("%s\n", name);
            continue;
        }

        join_path(path, name, full, sizeof(full));
        stat_t st;
        if (sys_stat(full, &st) < 0) {
            printf("??????????    ?    ?        ? %s\n", name);
            continue;
        }
        format_mode(st.type == VNODE_DIR_T, st.mode, mode_str);
        printf("%s %d %d %d %s\n", mode_str, (int)st.uid, (int)st.gid, (int)st.size, name);
    }

    if (!any) {
        printf("ls: %s: not a directory, empty, or doesn't exist\n", path);
    }

    return 0;
}
