#pragma once

/* Real Linux x86_64 open(2) flag values -- not our own numbering, same
 * reasoning as errno.h: a borrowed static binary's compiled-in O_CREAT
 * etc. are these exact bit patterns, baked in at compile time by
 * whatever libc it was built against. Matching them is what makes
 * process_open()'s flags argument mean anything to code we didn't write. */

#define O_ACCMODE 0x0003
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0040
#define O_EXCL    0x0080
#define O_TRUNC   0x0200
#define O_APPEND  0x0400
