#pragma once
#include <stdint.h>

/* One-way trip into ring 3. Never returns. */
void enter_userspace(uint64_t user_rip, uint64_t user_rsp);
