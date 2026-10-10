/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#ifndef RVNANO_PID_H
#define RVNANO_PID_H
#include <stdint.h>
/* Same integer PID + first-order virtual plant on both cores. Q16 units. */
struct pid_state { int32_t plant, integral, previous; uint32_t checksum; };
static inline int32_t bound32(int64_t x,int32_t lo,int32_t hi) {
    return x<lo?lo:x>hi?hi:(int32_t)x;
}
static inline void pid_step(struct pid_state *s,uint32_t sample) {
    int32_t reference=((sample/1000u)&1u)?32768:65536;
    int32_t error=reference-s->plant;
    s->integral=bound32((int64_t)s->integral+error,-655360,655360);
    int32_t control=bound32(((int64_t)error*98304+((int64_t)s->integral*256)+
                            ((int64_t)(error-s->previous)*16384))/65536,-131072,131072);
    s->previous=error;
    s->plant+=(control-s->plant)/32;
    s->checksum=(s->checksum^(uint32_t)s->plant)*16777619u;
}
#endif
