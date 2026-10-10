/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
/* Pure rendering/math probe. monitor_render.h is extracted from current source
 * by the Python test; no sampling, device mapping, driver or self-test runs. */
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include "shared.h"
static int loaded_here;
#include "monitor_render.h"
int main(int argc,char **argv) {
    struct snapshot s={0};
    s.temperature_mC=59149;s.memory_total_kB=234452;s.memory_used_kB=75620;
    s.clock_hz[0]=1050000000;s.clock_hz[1]=786432000;s.clock_hz[2]=700000000;
    s.c1_valid=1;s.c1_state=RV_RUN;s.npu_valid=1;s.npu_active=1;
    if(argc==2 && !strcmp(argv[1],"json")) {
        json_sample(stdout,&s,100,3.7,2.1,0.5,1000);
        s.c1_state=RV_DONE;s.npu_active=0;
        json_sample(stdout,&s,0,0,0,0.5,1000);
        s.c1_valid=s.npu_valid=0;
        json_sample(stdout,&s,-1,-1,-1,0.5,1000);
    }else if(argc==2 && !strcmp(argv[1],"percent")) {
        printf("%.1f %.1f %.1f %.1f %.1f\n",
          percent(70,20,200,100,1,1),percent(70,20,200,100,1,0),
          percent(70,20,100,100,1,0),percent(10,20,200,100,1,0),
          percent(70,20,200,100,0,0));
    }else {
        display(&s,100,3.7,2.1,1,1000,1);
        s.c1_state=RV_DONE;s.npu_active=0;
        display(&s,0,0,0,1,1000,0);
        s.c1_valid=s.npu_valid=0;s.temperature_mC=-1;s.clock_hz[0]=0;
        display(&s,-1,-1,-1,1,1000,0);
    }
    return 0;
}
