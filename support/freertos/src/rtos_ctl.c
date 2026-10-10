/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include "shared.h"
static uint64_t ns(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static void die(const char *m) { perror(m);exit(1); }
static void doorbell(int ack) {
    int fd=open("/sys/class/remoteproc/remoteproc0/device/wake",O_WRONLY);
    if(fd<0)die("C1 mailbox wake");
    const char *value=ack?"0\n":"1\n";
    if(write(fd,value,2)!=2){close(fd);die("C1 mailbox wake write");}
    close(fd);
}
static void validate_region(void) {
    uint8_t b[16];FILE *f=fopen("/proc/device-tree/reserved-memory/rproc/reg","rb");
    if(!f || fread(b,1,16,f)!=16)die("reserved rproc");
    fclose(f);
    uint64_t base=0,size=0;for(int i=0;i<8;i++)base=(base<<8)|b[i];for(int i=8;i<16;i++)size=(size<<8)|b[i];
    if(base!=0x8fe00000 || size!=0x200000) { fprintf(stderr,"Unexpected DT rproc carveout\n");exit(1); }
}
int main(int argc,char **argv) {
    if(argc<2) { fprintf(stderr,"status | ready | run COUNT FILE | ping COUNT FILE\n");return 2; }
    if(strcmp(argv[1],"status") && strcmp(argv[1],"ready") && strcmp(argv[1],"run") && strcmp(argv[1],"ping"))return 2;
    int lock=open("/run/rvnano-c1-command.lock",O_CREAT|O_RDWR|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB))die("another C1 command is active");
    validate_region();int f=open("/dev/mem",O_RDWR|O_SYNC);if(f<0)die("/dev/mem");
    volatile struct rv_shared *s=mmap(NULL,RV_SHARED_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,f,RV_SHARED_ADDR);
    if(s==MAP_FAILED)die("mmap reserved rproc shared RAM");
    close(f);
    printf("{\"magic\":%u,\"version\":%u,\"state\":%u,\"hart\":%u,\"tick_hz\":%u,\"timer_hz\":%u,\"ticks\":%"PRIu64",\"completed\":%u,\"assert_line\":%u,\"fault_cause\":%"PRIu64",\"fault_pc\":%"PRIu64"}\n",s->magic,s->version,s->state,s->hart,s->tick_hz,s->timer_hz,s->ticks,s->completed,s->assert_line,s->fault_cause,s->fault_pc);
    printf("{\"sleep_mode\":%u,\"timer_irqs\":%"PRIu64",\"wake_irqs\":%"PRIu64"}\n",s->sleep_mode,s->timer_irqs,s->wake_irqs);
    if(!strcmp(argv[1],"status"))return 0;
    if(s->magic!=RV_MAGIC || s->version!=RV_VERSION || s->tick_hz!=1000 || s->timer_hz!=RV_TIMER_HZ ||
       (s->state!=RV_READY && s->state!=RV_DONE)) { fprintf(stderr,"Firmware not ready\n");return 1; }
    if(!strcmp(argv[1],"ready")) {
        uint64_t irqs=s->wake_irqs;unsigned req=s->ping_request+1;
        s->ping_request=req;rv_fence();doorbell(0);
        uint64_t deadline=ns()+1000000000;
        while(s->ping_reply!=req){if(s->state==RV_FAULT || ns()>deadline){fprintf(stderr,"C1 interrupt ready timeout\n");return 1;}usleep(100);}
        rv_fence();doorbell(1);
        if(s->wake_irqs<=irqs){fprintf(stderr,"C1 mailbox IRQ did not advance\n");return 1;}
        munmap((void*)s,RV_SHARED_BYTES);return 0;
    }
    if(argc!=4)return 2;
    unsigned n=(unsigned)strtoul(argv[2],NULL,10);FILE *o=fopen(argv[3],"w");if(!o)die("output");
    if(!strcmp(argv[1],"run")) {
        if(!n || n>RV_MAX_SAMPLES || s->state==RV_RUN)return 2;
        unsigned req=s->request+1;s->count=n;s->command=RV_CMD_START;rv_fence();s->request=req;rv_fence();doorbell(0);
        uint64_t end=ns()+((uint64_t)n+5000)*1000000;
        while(s->acknowledge!=req || s->state==RV_RUN) {
            if(s->state==RV_FAULT || ns()>end) { fprintf(stderr,"RTOS fault or test timeout\n");return 1; }
            usleep(20000);
        }
        rv_fence();doorbell(1);if(s->state!=RV_DONE || s->completed!=n)return 1;
        fprintf(o,"sample,release,wake,finish\n");for(unsigned i=0;i<n;i++) {
            struct rv_sample x=s->samples[i];fprintf(o,"%u,%"PRIu64",%"PRIu64",%"PRIu64"\n",i,x.release,x.wake,x.finish);
        }
        printf("{\"count\":%u,\"checksum\":%u,\"stack_free_words\":%u,\"timebase_hz\":%u}\n",n,s->checksum,s->stack_free_words,s->timer_hz);
    } else if(!strcmp(argv[1],"ping")) {
        if(!n || n>10000)return 2;
        fprintf(o,"sample,round_trip_ns,remote_service_ticks\n");
        for(unsigned i=0;i<n;i++) {
            unsigned req=s->ping_request+1;uint64_t start=ns();s->ping_request=req;rv_fence();doorbell(0);
            while(s->ping_reply!=req) { if(ns()-start>1000000000) {fprintf(stderr,"ping timeout\n");return 1;} }
            rv_fence();uint64_t end=ns();doorbell(1);fprintf(o,"%u,%"PRIu64",%"PRIu64"\n",i,end-start,s->ping_send-s->ping_receive);
        }
    } else return 2;
    fclose(o);munmap((void*)s,RV_SHARED_BYTES);return 0;
}
