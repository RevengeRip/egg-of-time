/* EGG_SELFTEST=1: run recompiled pure game functions against C reference results, then exit.
 * Catches translation bugs (flags, carries, shifts) in isolation from the rest of the game. */
#include <stdlib.h>

#include "dispatch.h"
#include "rt.h"

#define LL_UDIV 0x02021d20u /* MSL: unsigned 64-bit division, r0:r1 / r2:r3 -> r0:r1 */
#define LL_UMOD 0x02021d2cu /* second entry point of the same routine: unsigned remainder */

static uint64_t call64(CPU *cpu, uint32_t fn, uint64_t a, uint64_t b) {
    R[0] = (uint32_t)a; R[1] = (uint32_t)(a >> 32);
    R[2] = (uint32_t)b; R[3] = (uint32_t)(b >> 32);
    RT_CALL_ADDR(fn, 0xFFFF0000u);
    return (uint64_t)R[1] << 32 | R[0];
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
}

void rt_selftest(CPU *cpu) {
    int fails = 0, runs = 0;
    for (int i = 0; i < 20000; i++) {
        uint64_t a = next(), b = next();
        switch (i % 4) { /* cover small divisors, 32-bit operands and full 64-bit operands */
        case 0: b &= 0xFFFF; break;
        case 1: a &= 0xFFFFFFFFull; b &= 0xFFFFFFFFull; break;
        case 2: b >>= (next() & 63); break;
        default: break;
        }
        if (b == 0) continue;
        runs++;
        uint64_t q = call64(cpu, LL_UDIV, a, b);
        if (q != a / b) {
            if (fails++ < 10) rt_log("selftest: udiv %016llx / %016llx = %016llx, expected %016llx",
                                     (unsigned long long)a, (unsigned long long)b, (unsigned long long)q,
                                     (unsigned long long)(a / b));
        }
        uint64_t r = call64(cpu, LL_UMOD, a, b);
        if (r != a % b) {
            if (fails++ < 10) rt_log("selftest: umod %016llx %% %016llx = %016llx, expected %016llx",
                                     (unsigned long long)a, (unsigned long long)b, (unsigned long long)r,
                                     (unsigned long long)(a % b));
        }
    }
    rt_log("selftest: %d cases, %d failures", runs, fails);
    exit(fails ? 1 : 0);
}
