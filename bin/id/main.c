#include "mini_libc.h"
#include "mini_printf.h"

/* No /etc/passwd or group-name database exists yet (that's Tier 5
 * territory, see things.md) -- so unlike real `id`, this only ever
 * has numbers to print, never names like "root" or "alice". */
int main(void) {
    long uid = sys_getuid();
    long gid = sys_getgid();
    printf("uid=%d gid=%d\n", (int)uid, (int)gid);
    return 0;
}
