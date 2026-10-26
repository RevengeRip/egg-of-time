/* ARM9 I/O registers: dispatch to the device emulation, plain storage for everything else. */
#include <stdlib.h>

#include "rt.h"
#include "single_screen.h"
#include "video.h"

#define KEYINPUT_NONE 0x03FFu

volatile uint32_t rt_keys;
volatile int rt_touch_down, rt_touch_x, rt_touch_y;

/* EGG_LOG_IO=1 logs the first read and first write of each I/O word (bring-up aid). */
static int io_log = -1;
static uint8_t io_seen[RT_IO_SIZE / 4 + 8];

static void io_trace(uint32_t addr, int write, uint32_t v) {
    if (io_log < 0) io_log = getenv("EGG_LOG_IO") != NULL;
    if (!io_log) return;
    uint32_t slot = addr >= 0x04100000u ? RT_IO_SIZE / 4 + ((addr >> 2) & 7) : (addr - 0x04000000u) / 4;
    if (slot >= sizeof io_seen || (io_seen[slot] & (1 << write))) return;
    io_seen[slot] |= (uint8_t)(1 << write);
    CPU *c = rt_cur_cpu;
    rt_log("io %s 0x%08x = 0x%08x (return to 0x%08x)", write ? "write" : "read ", addr, v,
           c && c->ret_depth ? c->ret_stack[c->ret_depth - 1] : 0);
}

/* ---- divider / square root ---- */

static void div_update(void) {
    uint32_t cnt = io_get32(0x280);
    int64_t num, den;
    memcpy(&num, &rt_io[0x290], 8);
    memcpy(&den, &rt_io[0x298], 8);
    switch (cnt & 3) {
    case 0: num = (int32_t)num; den = (int32_t)den; break;
    case 1:
    case 3: den = (int32_t)den; break;
    default: break;
    }
    int64_t q, r;
    if (den == 0) {
        q = num < 0 ? 1 : -1;
        r = num;
    } else if (num == INT64_MIN && den == -1) {
        q = INT64_MIN;
        r = 0;
    } else {
        q = num / den;
        r = num % den;
    }
    int64_t full_den;
    memcpy(&full_den, &rt_io[0x298], 8);
    io_set32(0x280, (cnt & 3) | (full_den == 0 ? 1u << 14 : 0));
    memcpy(&rt_io[0x2A0], &q, 8);
    memcpy(&rt_io[0x2A8], &r, 8);
}

static void sqrt_update(void) {
    uint32_t cnt = io_get32(0x2B0);
    uint64_t v;
    memcpy(&v, &rt_io[0x2B8], 8);
    if (!(cnt & 1)) v &= 0xFFFFFFFFu;
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    io_set32(0x2B4, (uint32_t)r);
}

/* ---- register access ---- */

uint32_t hw_read32(uint32_t addr) {
    uint32_t v;
    if (addr == 0x04100000u) {
        v = pxi_recv();
    } else if (addr == 0x04100010u) {
        v = card_read_data();
    } else if (addr - 0x04000000u >= RT_IO_SIZE) {
        v = 0;
    } else {
        uint32_t off = addr - 0x04000000u;
        switch (off) {
        case 0x004: v = (uint32_t)video_vcount() << 16 | video_dispstat(); break;
        case 0x100: case 0x104: case 0x108: case 0x10C:
            v = (io_get32(off) & 0xFFFF0000u) | timer_counter((int)(off - 0x100) / 4);
            break;
        case 0x130: v = (io_get32(off) & 0xFFFF0000u) | (KEYINPUT_NONE & ~ss_game_keys(rt_keys)); break;
        case 0x180: v = pxi_sync_read(); break;
        case 0x184: v = pxi_fifocnt_read(); break;
        case 0x214: v = irq_if(); break;
        case 0x600: v = (io_get32(off) & 0xC0008000u) | 0x06000000u; break; /* GX FIFO empty, engine idle */
        default: v = io_get32(off); break;
        }
    }
    io_trace(addr, 0, v);
    return v;
}

void hw_write32(uint32_t addr, uint32_t value, uint32_t mask) {
    io_trace(addr, 1, value & mask);
    if (addr - 0x04000000u >= RT_IO_SIZE) return; /* 0x04100000+ registers are read-only on the ARM9 */
    uint32_t off = addr - 0x04000000u;
    uint32_t old = io_get32(off);
    uint32_t v = (old & ~mask) | (value & mask);

    switch (off) {
    case 0x004: io_set32(off, (old & 0xFFFF0007u) | (v & 0xFFB8u)); return; /* DISPSTAT: status bits are read-only */
    case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC:
        io_set32(off, v);
        dma_control_written((int)(off - 0x0B8) / 12, old);
        return;
    case 0x100: case 0x104: case 0x108: case 0x10C:
        io_set32(off, v);
        if (mask & 0xFFFF0000u) timer_control_written((int)(off - 0x100) / 4, (uint16_t)(old >> 16));
        return;
    case 0x180:
        if (mask & 0xFFFFu) pxi_sync_write((uint16_t)v);
        return;
    case 0x184:
        if (mask & 0xFFFFu) pxi_fifocnt_write((uint16_t)v);
        return;
    case 0x188: pxi_send(v); return;
    case 0x1A4:
        io_set32(off, v);
        if ((v & 0x80000000u) && (mask & 0xFF000000u)) card_romctrl_written();
        return;
    case 0x240: case 0x244: case 0x248:
        io_set32(off, v);
        video_vramcnt_written();
        return;
    case 0x028: case 0x02C: case 0x038: case 0x03C:
    case 0x1028: case 0x102C: case 0x1038: case 0x103C:
        io_set32(off, v);
        video_affine_ref_written(off >= 0x1000, (off & 0x10) ? 1 : 0);
        return;
    case 0x208: case 0x210:
        io_set32(off, v);
        irq_changed();
        return;
    case 0x214: irq_ack(value & mask); return;
    case 0x280: case 0x290: case 0x294: case 0x298: case 0x29C:
        io_set32(off, v);
        div_update();
        return;
    case 0x2B0: case 0x2B8: case 0x2BC:
        io_set32(off, v);
        sqrt_update();
        return;
    case 0x600: io_set32(off, (old & ~0xC0000000u) | (v & 0xC0000000u)); return; /* GXSTAT: only IRQ mode writable */
    default: io_set32(off, v); return;
    }
}

void hw_reset(void) {
    memset(rt_io, 0, sizeof rt_io);
    io_set16(0x130, KEYINPUT_NONE);
}
