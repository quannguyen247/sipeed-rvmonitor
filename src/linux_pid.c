/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "pid.h"
static uint64_t ns(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
int main(int argc,char **argv) {
    if(argc!=4) { fprintf(stderr,"COUNT FILE other|fifo\n");return 2; }
    unsigned n=(unsigned)strtoul(argv[1],NULL,10);if(!n || n>10000)return 2;
    struct {uint64_t release,wake,finish;} *a=calloc(n,sizeof(*a));if(!a)return 1;
    memset(a,0,n*sizeof(*a));FILE *f=fopen(argv[2],"w");if(!f)return 1;
    if(mlockall(MCL_CURRENT|MCL_FUTURE)) {perror("mlockall");return 1;}
    if(!strcmp(argv[3],"fifo")) {struct sched_param p={.sched_priority=20};if(syscall(SYS_sched_setscheduler,0,SCHED_FIFO,&p)) {perror("fifo syscall");return 1;} }
    struct pid_state pid={0,0,0,2166136261u};uint64_t release=ns()+100000000;
    for(unsigned i=0;i<n;i++,release+=1000000) {
        struct timespec t={(time_t)(release/1000000000),(long)(release%1000000000)};int e;
        while((e=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL))==EINTR){}
        if(e) {errno=e;perror("sleep");return 1;}
        uint64_t wake=ns();pid_step(&pid,i);uint64_t finish=ns();a[i]=(typeof(*a)){release,wake,finish};
    }
    struct sched_param p={0};if(syscall(SYS_sched_setscheduler,0,SCHED_OTHER,&p)) {perror("restore scheduler");return 1;}munlockall();
    fprintf(f,"sample,release,wake,finish\n");for(unsigned i=0;i<n;i++)fprintf(f,"%u,%"PRIu64",%"PRIu64",%"PRIu64"\n",i,a[i].release,a[i].wake,a[i].finish);
    fclose(f);printf("{\"count\":%u,\"checksum\":%u,\"clock\":\"CLOCK_MONOTONIC_ns\"}\n",n,pid.checksum);free(a);return 0;
}
