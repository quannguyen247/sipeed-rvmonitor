/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#include <inttypes.h>
#include <stdio.h>
#include "pid.h"
int main(void) {
    struct pid_state state={0,0,0,2166136261u};
    for(unsigned i=0;i<5000;i++) {
        pid_step(&state,i);
        if(i==999 || i==4999)printf("%u %"PRIu32"\n",i+1,state.checksum);
    }
    return 0;
}
