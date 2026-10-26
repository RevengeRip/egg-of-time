/* Inter-processor communication (IPCSYNC / IPC FIFO) with a high-level emulated ARM7.
 *
 * NitroSDK's PXI layer sends 32-bit FIFO words: bits 0-4 tag (subsystem), bit 5 error, bits 6-31 data.
 * The ARM7 side answers the requests the game makes; arm7_receive() is that side. */
#include <stdlib.h>
#include <time.h>

#include "sound.h"

#define FIFO_DEPTH 16

static const char *const tag_names[32] = {
    "EX", "USER_0", "USER_1", "SYSTEM", "NVRAM", "RTC", "TOUCHPANEL", "SOUND", "PM", "MIC", "WM", "FS",
    "OS", "CTRDG", "CARD", "WVR", "CTRDG_Ex", "CTRDG_PHI",
};

static uint16_t sync_out9;    /* ARM9 -> ARM7 sync value (bits 8-11 of IPCSYNC) and IRQ enable (bit 14) */
static uint8_t sync_out7;     /* ARM7 -> ARM9 sync value */
static uint16_t fifocnt;      /* writable bits of IPCFIFOCNT */
static uint32_t recv_q[FIFO_DEPTH], recv_head, recv_count, recv_last;
static int log_pxi = -1;

/* Bitmask of PXI tags the ARM7 has a receive callback for (PXI_IsCallbackReady(tag, PXI_PROC_ARM7)). */
#define ARM7_CALLBACK_READY 0x027FFF8Cu
#define TAG(n) (1u << (n))
#define ARM7_TAGS (TAG(3) | TAG(4) | TAG(5) | TAG(6) | TAG(7) | TAG(8) | TAG(9) | TAG(10) | TAG(11) | TAG(12) | \
                   TAG(13) | TAG(14))

/* ---- IPCSYNC ---- */

uint16_t pxi_sync_read(void) { return (uint16_t)(sync_out7 | (sync_out9 & 0x4F00u)); }

void pxi_sync_write(uint16_t v) {
    sync_out9 = v & 0x4F00u;
    uint8_t echoed = (v >> 8) & 0xF;
    if (echoed == sync_out7 && sync_out7 > 0) sync_out7--;
}

/* ---- FIFO ---- */

static void recv_irq_check(void) {
    if ((fifocnt & 0x8400u) == 0x8400u && recv_count) irq_raise(IRQ_IPC_RECV);
}

/* ARM7 -> ARM9 */
static void arm7_send(uint32_t v) {
    if (recv_count == FIFO_DEPTH) {
        rt_log("pxi: receive FIFO overflow, dropping 0x%08x", v);
        return;
    }
    recv_q[(recv_head + recv_count++) % FIFO_DEPTH] = v;
    recv_irq_check();
}

void pxi_arm7_send(uint32_t v) { arm7_send(v); }

void pxi_update(uint64_t now) { snd_update(now); }

uint16_t pxi_fifocnt_read(void) {
    uint16_t v = fifocnt & 0xC404u;
    v |= 1;                       /* send FIFO: the ARM7 consumes words immediately */
    if (recv_count == 0) v |= 1u << 8;
    if (recv_count == FIFO_DEPTH) v |= 1u << 9;
    return v;
}

void pxi_fifocnt_write(uint16_t v) {
    uint16_t old = fifocnt;
    fifocnt = (fifocnt & 0x4000u) | (v & 0x8404u);
    if (v & 0x4000u) fifocnt &= ~0x4000u; /* error acknowledge */
    if ((v & 0x8004u) == 0x8004u && (old & 0x8004u) != 0x8004u) irq_raise(IRQ_IPC_SEND_EMPTY);
    recv_irq_check();
}

uint32_t pxi_recv(void) {
    if (!(fifocnt & 0x8000u)) return recv_q[recv_head];
    if (recv_count == 0) {
        fifocnt |= 0x4000u;
        return recv_last;
    }
    recv_last = recv_q[recv_head];
    recv_head = (recv_head + 1) % FIFO_DEPTH;
    recv_count--;
    return recv_last;
}

/* ---- ARM7 side ---- */

enum { TAG_RTC = 5, TAG_TOUCHPANEL = 6, TAG_SOUND = 7, TAG_CARD = 11 };

/* RTC: requests carry the command in bits 8-14; the ARM7 stores BCD date/time in shared memory and
 * replies (command << 8) | result. */
#define RTC_DATE 0x027FFDE8u /* year, month, day, weekday */
#define RTC_TIME 0x027FFDECu /* hour (bit 6: PM), minute, second */

static uint32_t bcd(int v) { return (uint32_t)(v / 10 * 16 + v % 10); }

static void rtc_request(uint32_t data) {
    uint32_t cmd = (data >> 8) & 0x7F;
    if (cmd >= 0x10 && cmd <= 0x12) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        rt_w32(RTC_DATE, bcd(tm->tm_year % 100) | bcd(tm->tm_mon + 1) << 8 | bcd(tm->tm_mday) << 16 |
                             (uint32_t)tm->tm_wday << 24);
        rt_w32(RTC_TIME, (bcd(tm->tm_hour) | (tm->tm_hour >= 12 ? 0x40u : 0)) | bcd(tm->tm_min) << 8 |
                             bcd(tm->tm_sec) << 16);
    }
    /* Other commands (reset, hour format, status, alarms, writes) succeed without effect. */
    arm7_send(TAG_RTC | (cmd << 8) << 6);
}

/* CARD library (tag 11): CARDi_Request sends a request type with the flag bit set; CARD_REQ_INIT is
 * followed by the address of the shared CARDiCommandArg block. The ARM7 writes cmd->result (word 0,
 * 0 = success) and answers with the flag bit set, which wakes the requesting thread.
 * Backup memory requests are served by card.c. */
#define CARD_REQ_INIT 0
static uint32_t card_cmd;
static int card_expect_cmd_ptr;

static void card_request(uint32_t data) {
    if (card_expect_cmd_ptr) {
        card_expect_cmd_ptr = 0;
        card_cmd = data;
    } else if (data == CARD_REQ_INIT) {
        card_expect_cmd_ptr = 1;
        return;
    } else if (card_cmd) {
        rt_w32(card_cmd, card_backup_request(data, card_cmd));
        arm7_send(TAG_CARD | 0x20);
        return;
    }
    if (card_cmd) rt_w32(card_cmd, 0);
    arm7_send(TAG_CARD | 0x20);
}

/* Touch panel: command in bits 8-14 (0 = sample once). The raw sample goes to shared memory as
 * x (bits 0-11), y (12-23), touched (24), validity (25-26); the reply is (1 << 24) | command << 8 | result.
 * Raw values are 16 ADC units per pixel, matching the calibration boot.c writes to the user settings. */
#define TP_RAW 0x027FFFAAu

static void tp_request(uint32_t data) {
    uint32_t cmd = (data >> 8) & 0x7F;
    if (cmd == 0) {
        uint32_t raw = 0;
        if (rt_touch_down) raw = (uint32_t)rt_touch_x * 16 | (uint32_t)rt_touch_y * 16 << 12 | 1u << 24;
        rt_w16(TP_RAW, (uint16_t)raw);
        rt_w16(TP_RAW + 2, (uint16_t)(raw >> 16));
    }
    arm7_send(TAG_TOUCHPANEL | ((1u << 24) | cmd << 8) << 6);
}

static void arm7_receive(uint32_t v) {
    uint32_t tag = v & 0x1F, data = v >> 6;
    if (log_pxi < 0) log_pxi = getenv("EGG_LOG_PXI") != NULL;
    if (log_pxi) {
        rt_log("pxi: ARM9 -> ARM7 tag %u (%s) data 0x%07x%s", tag, tag_names[tag] ? tag_names[tag] : "?", data,
               v & 0x20 ? " [error]" : "");
    }
    switch (tag) {
    case TAG_RTC: rtc_request(data); break;
    case TAG_TOUCHPANEL: tp_request(data); break;
    case TAG_CARD:
        if (v & 0x20) card_request(data);
        break;
    case TAG_SOUND:
        if (data) snd_command_list(data);
        break;
    default:
        break;
    }
}

void pxi_send(uint32_t v) {
    if (!(fifocnt & 0x8000u)) return;
    arm7_receive(v);
}

void pxi_reset(void) {
    rt_w32(ARM7_CALLBACK_READY, ARM7_TAGS);
    /* The ARM7 counts its sync value down as the ARM9 echoes it (PXI_Init handshake). */
    sync_out7 = 7;
    sync_out9 = 0;
    fifocnt = 0;
    recv_head = recv_count = recv_last = 0;
    card_cmd = 0;
    card_expect_cmd_ptr = 0;
    snd_reset();
}

void pxi_state(RtStateIO *s) {
    RT_STATE(s, sync_out9);
    RT_STATE(s, sync_out7);
    RT_STATE(s, fifocnt);
    RT_STATE(s, recv_q);
    RT_STATE(s, recv_head);
    RT_STATE(s, recv_count);
    RT_STATE(s, recv_last);
    RT_STATE(s, card_cmd);
    RT_STATE(s, card_expect_cmd_ptr);
}
