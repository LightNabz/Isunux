#include "mini_libc.h"
#include "mini_printf.h"

/* Interactive, not automated -- unlike jobtest/signaltest/pipetest,
 * raw mode and echo-off both need a REAL keystroke stream to mean
 * anything; there's no way to synthesize a keyboard IRQ from
 * userland to script this. Same reason `spin` is interactive instead
 * of a scripted PASS/FAIL case. Run this and type along. */
int main(void) {
    printf("=== raw mode demo ===\n");
    printf("Type 5 characters -- each should appear the instant you press it,\n");
    printf("with no need to press Enter (that's the difference from normal typing).\n");

    sys_tty_set_raw(1);
    for (int i = 0; i < 5; i++) {
        char c;
        sys_read(0, &c, 1);
        printf(" [got 0x%x]", (int)c);
    }
    sys_tty_set_raw(0);
    printf("\n\n");

    printf("=== echo-off demo (password-style prompt) ===\n");
    printf("Type something and press Enter -- nothing should appear as you type,\n");
    printf("but backspace still works, and it'll be printed back after Enter to\n");
    printf("prove it was actually captured.\n");
    printf("password: ");

    sys_tty_set_echo(0);
    char line[128];
    long n = sys_read(0, line, sizeof(line) - 1);
    sys_tty_set_echo(1);

    if (n > 0 && line[n - 1] == '\n') n--;
    line[n] = '\0';
    printf("\n(what was actually captured: \"%s\")\n\n", line);

    printf("=== back to normal ===\n");
    printf("Canonical mode + echo should be exactly like every other program now.\n");

    return 0;
}
