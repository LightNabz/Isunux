#include "mini_libc.h"
#include "mini_printf.h"

/* Boot-verification for Tier 3 step 1e. Triggers a handful of
 * genuinely distinct failure REASONS and checks that each syscall
 * comes back with the exact matching errno -- not just "some negative
 * number", which a lazy blanket -1 -> -EINVAL substitution would have
 * passed just as easily. Real error handling in a borrowed static
 * binary branches on the SPECIFIC errno (e.g. "does this file already
 * exist" vs "do I lack permission" are very different code paths), so
 * this is the part of 1e that actually matters. */

static int check(const char *label, long got, long want) {
    int ok = (got == want);
    printf("%s: got %d, want %d -> %s\n", label, (int)got, (int)want, ok ? "PASS" : "FAIL");
    return ok;
}

int main(void) {
    int all_ok = 1;

    all_ok &= check("open() on a missing path",
                     sys_open("/no/such/path/at/all"), -ENOENT);

    sys_mkdir("/tmp/errnotest_dir");
    all_ok &= check("mkdir() over an existing name",
                     sys_mkdir("/tmp/errnotest_dir"), -EEXIST);

    sys_create("/tmp/errnotest_dir/child.txt");
    all_ok &= check("unlink() a non-empty directory",
                     sys_unlink("/tmp/errnotest_dir"), -ENOTEMPTY);
    sys_unlink("/tmp/errnotest_dir/child.txt");
    sys_unlink("/tmp/errnotest_dir");

    all_ok &= check("chdir() into a plain file",
                     sys_chdir("/hello.txt"), -ENOTDIR);

    all_ok &= check("read() on a never-opened fd",
                     sys_read(97, (void *)0, 1), -EBADF);
    all_ok &= check("close() on a never-opened fd",
                     sys_close(97), -EBADF);

    all_ok &= check("kill() a pid that doesn't exist",
                     sys_kill(9999, SIGTERM), -ESRCH);

    long unknown = syscall1(999, 0); /* a syscall number nothing implements at all */
    all_ok &= check("an entirely unknown syscall number",
                     unknown, -ENOSYS);

    printf(all_ok ? "PASS: every failure came back with the exact right errno\n"
                  : "FAIL: see above\n");
    return all_ok ? 0 : 1;
}
