/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RTT_CONTROL_SHARED_H
#define RTT_CONTROL_SHARED_H
#include <stdint.h>
#include <stddef.h>
#define RTT_CONTROL_MAGIC 0x58545452U
#define RTT_CONTROL_ABI 1U
#define RTT_SHARED_SIZE 16384U
#define RTT_GRANT_FIRST 8U
#define RTT_GRANT_COUNT 4U
struct rtt_control_input {
    uint32_t sequence;
    uint32_t command;
    int32_t target_x, target_y;
    uint32_t external_mode;
    uint32_t reserved;
    uint64_t sent_counter;
} __attribute__((aligned(64)));
struct rtt_control_output {
    uint32_t sequence, input_sequence;
    int32_t pan, tilt;
    uint64_t control_count, counter, compute_us, max_compute_us, max_late_us;
    uint32_t deadline_misses, fp_failures;
} __attribute__((aligned(64)));
struct rtt_control_shared {
    uint32_t magic, abi, size, grant_first;
    uint64_t counter_frequency;
    uint8_t reserved[40];
    struct rtt_control_input input;
    struct rtt_control_output output;
} __attribute__((aligned(RTT_SHARED_SIZE)));
_Static_assert(offsetof(struct rtt_control_shared, input) == 64, "input ABI");
_Static_assert(offsetof(struct rtt_control_shared, output) == 128, "output ABI");
_Static_assert(sizeof(struct rtt_control_shared) == RTT_SHARED_SIZE, "shared ABI");
#endif
