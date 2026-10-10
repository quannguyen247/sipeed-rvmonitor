/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#include "FreeRTOS.h"
#include "task.h"
#include "shared.h"
#include "pid.h"
#include <string.h>
extern uint64_t ullNextTime;
static volatile struct rv_shared *const shared=(void*)RV_SHARED_ADDR;
static StaticTask_t control_tcb, idle_tcb;
static StackType_t control_stack[512], idle_stack[256];
static TaskHandle_t control_handle;
static uint64_t boot_time, idle_time;
static inline uint64_t now(void) { uint64_t t; __asm__ volatile("rdtime %0":"=r"(t));return t; }
static void control(void *unused) {
    (void)unused;
    for (;;) {
        while (shared->request==shared->acknowledge)
            (void)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        rv_fence();
        uint32_t request=shared->request, command=shared->command;
        if (command!=RV_CMD_START) { shared->state=RV_READY;rv_fence();shared->acknowledge=request;continue; }
        uint32_t count=shared->count;
        if (!count || count>RV_MAX_SAMPLES) rv_assert(__LINE__);
        struct pid_state pid={0,0,0,2166136261u};
        TickType_t expected=xTaskGetTickCount();
        shared->completed=0;shared->state=RV_RUN;rv_fence();shared->acknowledge=request;
        for (uint32_t i=0;i<count;i++) {
            vTaskDelayUntil(&expected,1);
            uint64_t release=shared->first_compare+((uint64_t)expected-1)*RV_PERIOD_TICKS;
            uint64_t wake=now();
            pid_step(&pid,i);
            uint64_t finish=now();
            shared->samples[i]=(struct rv_sample){release,wake,finish};
            rv_fence();shared->completed=i+1;
        }
        shared->checksum=pid.checksum;
        shared->stack_free_words=uxTaskGetStackHighWaterMark(NULL);
        rv_fence();shared->state=RV_DONE;
    }
}
static void compare_at(uint64_t deadline) {
    extern volatile uint32_t *pulMachineTimerCompareRegisterL,*pulMachineTimerCompareRegisterH;
    *pulMachineTimerCompareRegisterL=0xffffffffu;
    *pulMachineTimerCompareRegisterH=deadline>>32;
    *pulMachineTimerCompareRegisterL=(uint32_t)deadline;
    rv_fence();
}
static uint64_t idle_enter(unsigned mode) {
    uint64_t start=now();
    shared->telemetry_seq++;rv_fence();
    shared->elapsed_ticks=start-boot_time;shared->idle_ticks=idle_time;
    shared->idle_since=start;shared->sleep_mode=mode;
    rv_fence();shared->telemetry_seq++;rv_fence();
    return start;
}
static void idle_leave(uint64_t start,uint64_t end) {
    idle_time+=end-start;
    shared->telemetry_seq++;rv_fence();
    shared->elapsed_ticks=end-boot_time;shared->idle_ticks=idle_time;
    shared->idle_since=0;shared->sleep_mode=0;
    rv_fence();shared->telemetry_seq++;rv_fence();
}
void vApplicationTickHook(void) {
    shared->ticks++;shared->timer_irqs++;
    shared->last_deadline=ullNextTime-2*RV_PERIOD_TICKS;
}
void vApplicationIdleHook(void) {
    /* The kernel's tickless hook handles an indefinitely blocked control task. */
    if(shared->state==RV_READY || shared->state==RV_DONE)return;
    __asm__ volatile("csrci mstatus,8" ::: "memory");
    uint64_t start=idle_enter(1);
    __asm__ volatile("wfi" ::: "memory");
    idle_leave(start,now());
    __asm__ volatile("csrsi mstatus,8" ::: "memory");
}
void rv_suppress_ticks(uint64_t expected) {
    (void)expected;
    __asm__ volatile("csrci mstatus,8" ::: "memory");
    if(eTaskConfirmSleepModeStatus()!=eNoTasksWaitingTimeout) {
        __asm__ volatile("csrsi mstatus,8" ::: "memory");return;
    }
    /* No delayed task or timer exists here. Only the mailbox is a wake source. */
    uint64_t due=ullNextTime-RV_PERIOD_TICKS;
    __asm__ volatile("csrc mie,%0" :: "r"(0x80UL) : "memory");
    compare_at(UINT64_MAX);
    uint64_t start=idle_enter(2), pending;
    do {
        __asm__ volatile("csrr %0,mip":"=r"(pending));
        if((pending&0x800UL) || shared->request!=shared->acknowledge ||
           shared->ping_request!=shared->ping_reply)break;
        __asm__ volatile("wfi" ::: "memory");
    } while(1); /* WFI may return spuriously; never re-enable a periodic wake. */
    uint64_t end=now();idle_leave(start,end);
    uint64_t skipped=end>=due?(end-due)/RV_PERIOD_TICKS+1:0;
    if(skipped){vTaskStepTick((TickType_t)skipped);shared->ticks+=skipped;due+=skipped*RV_PERIOD_TICKS;}
    ullNextTime=due+RV_PERIOD_TICKS;compare_at(due);
    __asm__ volatile("csrs mie,%0" :: "r"(0x80UL) : "memory");
    __asm__ volatile("csrsi mstatus,8" ::: "memory");
}
void vApplicationGetIdleTaskMemory(StaticTask_t **t,StackType_t **s,uint32_t *n) {
    *t=&idle_tcb;*s=idle_stack;*n=256;
}
void rv_assert(unsigned line) {
    shared->assert_line=line;shared->state=RV_FAULT;rv_fence();
    __asm__ volatile("csrci mstatus,8");for (;;) __asm__ volatile("wfi");
}
void vApplicationStackOverflowHook(TaskHandle_t t,char *name) { (void)t;(void)name;rv_assert(__LINE__); }
/* Same C906L PLIC context/IRQ and mailbox registers as the pinned vendor SDK. */
#define RV_MBOX_BASE 0x01900000UL
#define RV_MBOX_IRQ 61u
static volatile uint32_t *reg32(uint64_t a){return (volatile uint32_t*)(uintptr_t)a;}
static void mailbox_init(void) {
    *reg32(0x70000000UL+4*RV_MBOX_IRQ)=7;
    /* PLIC machine context of the auxiliary hart; leave other enables intact. */
    *reg32(0x70002000UL+4*(RV_MBOX_IRQ/32))|=1u<<(RV_MBOX_IRQ%32);
    *reg32(0x70200000UL)=0;
    *reg32(RV_MBOX_BASE+0x30)=1; /* clear only TX channel 0 for CPU2 */
    rv_fence();
}
void do_irq(void) {
    BaseType_t yield=pdFALSE;
    for(;;){
        uint32_t irq=*reg32(0x70200004UL);if(!irq)break;
        if(irq!=RV_MBOX_IRQ)rv_assert(__LINE__);
        uint32_t pending=*reg32(RV_MBOX_BASE+0x38)&1u;
        if(pending){
            *reg32(RV_MBOX_BASE+0x30)=pending;
            *reg32(RV_MBOX_BASE+0x08)&=~pending;rv_fence();
            shared->wake_irqs++;
            uint32_t ping=shared->ping_request;
            if(ping!=shared->ping_reply){
                shared->ping_receive=now();shared->ping_send=now();
                rv_fence();shared->ping_reply=ping;
            }
            if(shared->request!=shared->acknowledge)
                vTaskNotifyGiveFromISR(control_handle,&yield);
        }
        *reg32(0x70200004UL)=irq;rv_fence();
    }
    portYIELD_FROM_ISR(yield);
}
void __attribute__((aligned(4))) early_trap(void) {
    uint64_t a,b,c;__asm__ volatile("csrr %0,mcause":"=r"(a));
    __asm__ volatile("csrr %0,mepc":"=r"(b));__asm__ volatile("csrr %0,mtval":"=r"(c));
    shared->fault_cause=a;shared->fault_pc=b;shared->fault_value=c;rv_assert(__LINE__);
}
void main(void) {
    uint64_t hart;__asm__ volatile("csrr %0,mhartid":"=r"(hart));
    memset((void*)shared,0,sizeof(*shared));
    boot_time=now();idle_time=0;
    shared->magic=RV_MAGIC;shared->version=RV_VERSION;shared->state=RV_BOOT;
    shared->hart=(uint32_t)hart;shared->tick_hz=1000;shared->timer_hz=RV_TIMER_HZ;
    shared->ticks=0;shared->request=shared->acknowledge=0;
    shared->boot_time=boot_time;mailbox_init();
    control_handle=xTaskCreateStatic(control,"PID1kHz",512,NULL,3,control_stack,&control_tcb);
    configASSERT(control_handle);
    /* Vendor setup will run again in startScheduler; overridden below records phase. */
    shared->state=RV_READY;rv_fence();vTaskStartScheduler();rv_assert(__LINE__);
}
void vPortSetupTimerInterrupt(void) {
    extern volatile uint32_t *pulMachineTimerCompareRegisterL,*pulMachineTimerCompareRegisterH;
    uint64_t hart;__asm__ volatile("csrr %0,mhartid":"=r"(hart));
    pulMachineTimerCompareRegisterL=(void*)(0x74004000UL+hart*8);
    pulMachineTimerCompareRegisterH=(void*)(0x74004004UL+hart*8);
    uint64_t first=now()+RV_PERIOD_TICKS;
    *pulMachineTimerCompareRegisterL=0xffffffffu;
    *pulMachineTimerCompareRegisterH=first>>32;
    *pulMachineTimerCompareRegisterL=(uint32_t)first;
    shared->first_compare=first;ullNextTime=first+RV_PERIOD_TICKS;rv_fence();
}
void *memset(void *dst,int c,size_t n) { unsigned char *p=dst;while(n--)*p++=(unsigned char)c;return dst; }
void *memcpy(void *dst,const void *src,size_t n) { unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;return dst; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b;while(n--) { if(*x!=*y)return *x-*y;x++;y++; }return 0; }
size_t strlen(const char *p) { const char *s=p;while(*p)p++;return (size_t)(p-s); }
char *strncpy(char *d,const char *s,size_t n) { char *r=d;while(n && *s) { *d++=*s++;n--; }while(n--)*d++=0;return r; }
/* Required even without RPMsg by this board's remoteproc parse_fw callback. */
__attribute__((section(".resource_table"),used))
const uint32_t resource_table[4]={1,0,0,0};
