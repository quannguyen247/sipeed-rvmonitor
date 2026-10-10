/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "shared.h"
#include "pid.h"
#ifndef RVMON_NPU_SHA256
#error Build with the verified telemetry module SHA256
#endif
#define NPU_MODULE "/opt/rvnano/rvmonitor/soph_tpu-monitor.ko"
#define RTOS_CTL "/opt/rvnano/freertos/rtos_ctl"
#define LINUX_PID "/opt/rvnano/freertos/linux_pid"
#define NPU_TEST "/opt/rvnano-npu/tensor-mul-selftest"
#define NPU_TEST_SHA "9237777676b52fb07a60e31e11ce268b946e6bf106b2b42ca54d0c49d17e43cd"
#define NPU_GUARD "/run/rvmonitor-npu-guard.lock"
#define NPU_USERS "/run/rvmonitor-npu-users.lock"
#define NPU_OWNER "/run/rvmonitor-npu.owner"
static volatile sig_atomic_t stopped;
static int loaded_here, terminal_active, guard_fd=-1, users_fd=-1, test_lock_fd=-1, quit_key;
static FILE *log_file;
static void leave_npu(void);
static struct termios saved_terminal;
static volatile struct rv_shared *shared;
struct snapshot {
    uint64_t wall_ns, linux_total, linux_idle, c1_elapsed, c1_idle;
    uint64_t npu_time, npu_busy, npu_jobs, self_ns;
    uint64_t clock_hz[3];
    unsigned clock_enabled[3], c1_state, c1_sleep, npu_active;
    long temperature_mC, memory_total_kB, memory_available_kB, memory_used_kB;
    int linux_valid, c1_valid, npu_valid, npu_loaded;
};
static uint64_t now_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static void stop_signal(int sig) {(void)sig;stopped=1;}
static int text_file(const char *p,char *b,size_t n) {
    FILE *f=fopen(p,"r");if(!f)return -1;
    size_t got=fread(b,1,n-1,f);fclose(f);b[got]=0;return (int)got;
}
static int run(char *const args[],const char *capture) {
    if(stopped)return 130;
    pid_t child=fork();if(child<0){perror("fork");return 1;}
    if(!child) {
        setpgid(0,0);signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);
        if(capture){int f=open(capture,O_CREAT|O_TRUNC|O_WRONLY,0600);if(f<0)_exit(126);dup2(f,1);close(f);}
        execv(args[0],args);perror(args[0]);_exit(127);
    }
    setpgid(child,child);int status=0,sent=0;
    while(waitpid(child,&status,0)<0) {
        if(errno!=EINTR){perror("waitpid");return 1;}
        if(stopped && !sent){kill(-child,SIGTERM);sent=1;}
    }
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
static void cleanup(void) {
    if(terminal_active) {
        tcsetattr(STDIN_FILENO,TCSANOW,&saved_terminal);
        printf("\033[?25h\033[?1049l");fflush(stdout);terminal_active=0;
    }
    if(shared){munmap((void*)shared,RV_SHARED_BYTES);shared=NULL;}
    if(log_file){fclose(log_file);log_file=NULL;}
    leave_npu();
    if(test_lock_fd>=0){close(test_lock_fd);test_lock_fd=-1;}
}
static int lock_path(const char *path,int *fd,const char *message) {
    *fd=open(path,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(*fd<0 || flock(*fd,LOCK_EX|LOCK_NB)){
        fprintf(stderr,"%s\n",message);return 1;}
    return 0;
}
static int verified_kernel(void) {
    struct utsname u;
    if(uname(&u) || strcmp(u.release,"5.10.271-rvnano") ||
       strcmp(u.version,"#4 Fri Oct 9 02:40:26 +07 2026")) {
        fprintf(stderr,"NPU requires verified kernel 5.10.271-rvnano #4.\n");return 1;
    }
    return 0;
}
static int verified_hash(const char *command,const char *expected) {
    /* Callers supply fixed commands, never a user path. */
    FILE *p=popen(command,"r");char digest[65]={0};
    if(!p){perror("sha256sum");return 1;}
    int found=fscanf(p,"%64s",digest),result=pclose(p);
    if(found!=1 || result || strcmp(digest,expected)) {
        fprintf(stderr,"NPU file hash mismatch.\n");return 1;
    }
    return 0;
}
static int lock_wait(int fd,int operation) {
    while(flock(fd,operation)) {
        if(errno!=EINTR || stopped)return -1;
    }
    return 0;
}
static uint64_t module_inode(void) {
    struct stat st;return stat("/sys/module/soph_tpu",&st)?0:(uint64_t)st.st_ino;
}
static int group_owns_npu(void) {
    char data[128],digest[65];uint64_t inode=0;
    if(text_file(NPU_OWNER,data,sizeof(data))<0)return 0;
    return sscanf(data,"%"SCNu64" %64s",&inode,digest)==2 && inode &&
           inode==module_inode() && !strcmp(digest,RVMON_NPU_SHA256);
}
static int record_owner(void) {
    int fd=open(NPU_OWNER,O_CREAT|O_TRUNC|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0){perror(NPU_OWNER);return 1;}
    char data[128];int n=snprintf(data,sizeof(data),"%"PRIu64" %s\n",module_inode(),RVMON_NPU_SHA256);
    int ok=write(fd,data,(size_t)n)==n;close(fd);
    if(!ok){perror("NPU ownership record");unlink(NPU_OWNER);return 1;}
    return 0;
}
static int join_npu(void) {
    guard_fd=open(NPU_GUARD,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    users_fd=open(NPU_USERS,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(guard_fd<0 || users_fd<0 || lock_wait(guard_fd,LOCK_EX)) {
        perror("NPU lifecycle lock");return 1;
    }
    int result=0;
    if(!module_inode()) {
        unlink(NPU_OWNER); /* A prior unloaded generation cannot be owned. */
        result=verified_kernel() || verified_hash("/usr/bin/sha256sum " NPU_MODULE,RVMON_NPU_SHA256);
        if(!result){char *const args[]={"/sbin/insmod",NPU_MODULE,NULL};result=run(args,0);}
        if(!result)result=record_owner();
    }
    if(!result && lock_wait(users_fd,LOCK_SH)){perror("NPU reader lease");result=1;}
    loaded_here=!result && group_owns_npu();
    flock(guard_fd,LOCK_UN);
    return result;
}
static void leave_npu(void) {
    if(guard_fd>=0 && users_fd>=0 && !flock(guard_fd,LOCK_EX)) {
        /* Conversion succeeds only for the last cooperating invocation. */
        if(!flock(users_fd,LOCK_EX|LOCK_NB)) {
            if(group_owns_npu()) {
                char buf[256];unsigned version,active=0;uint64_t stamp,busy,jobs;
                int pending=0;
                if(text_file("/proc/tpu/rvmonitor",buf,sizeof(buf))>0 &&
                   sscanf(buf,"version=%u now_ns=%"SCNu64" busy_ns=%"SCNu64" jobs=%"SCNu64" active=%u",
                          &version,&stamp,&busy,&jobs,&active)==5)pending=(active!=0);
                if(text_file("/sys/module/soph_tpu/refcnt",buf,sizeof(buf))>0 && strtol(buf,NULL,10)>0)pending=1;
                if(pending)fprintf(stderr,"NPU retained: an external client or command is active.\n");
                else {
                    /* Kernel reference checks cover non-cooperating users; never force. */
                    pid_t child=fork();int status=0;
                    if(!child){execl("/sbin/rmmod","rmmod","soph_tpu",(char*)NULL);_exit(127);}
                    if(child>0) {
                        pid_t done;do{done=waitpid(child,&status,0);}while(done<0 && errno==EINTR);
                        if(done<0 || !WIFEXITED(status) || WEXITSTATUS(status))
                            fprintf(stderr,"NPU unload refused; driver retained.\n");
                    }else perror("NPU cleanup fork");
                    if(!module_inode())unlink(NPU_OWNER);
                }
            }else unlink(NPU_OWNER); /* Pre-existing/reloaded drivers stay loaded. */
        }
        close(users_fd);users_fd=-1;flock(guard_fd,LOCK_UN);
    }
    if(users_fd>=0){close(users_fd);users_fd=-1;}
    if(guard_fd>=0){close(guard_fd);guard_fd=-1;}
    loaded_here=0;
}
static void map_c1(void) {
    char state[64];if(text_file("/sys/class/remoteproc/remoteproc0/state",state,sizeof(state))<0 || strncmp(state,"running",7))return;
    unsigned char b[16];FILE *f=fopen("/proc/device-tree/reserved-memory/rproc/reg","rb");
    if(!f)return;
    size_t got=fread(b,1,sizeof(b),f);fclose(f);if(got!=16)return;
    uint64_t base=0,size=0;for(int i=0;i<8;i++)base=(base<<8)|b[i];for(int i=8;i<16;i++)size=(size<<8)|b[i];
    if(base!=0x8fe00000UL || size!=0x200000UL){fprintf(stderr,"C1 DT carveout mismatch; telemetry unavailable.\n");return;}
    int fd=open("/dev/mem",O_RDONLY|O_SYNC);if(fd<0)return;
    void *p=mmap(NULL,RV_SHARED_BYTES,PROT_READ,MAP_SHARED,fd,RV_SHARED_ADDR);close(fd);
    if(p!=MAP_FAILED)shared=p;
}
static void sample(struct snapshot *s) {
    memset(s,0,sizeof(*s));s->temperature_mC=-1;
    FILE *f=fopen("/proc/stat","r");uint64_t c[8];
    if(f){if(fscanf(f,"cpu %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64,
       &c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&c[6],&c[7])==8){s->linux_valid=1;for(int i=0;i<8;i++)s->linux_total+=c[i];s->linux_idle=c[3]+c[4];}fclose(f);}
    if(shared && shared->magic==RV_MAGIC && shared->version==RV_VERSION && shared->timer_hz==RV_TIMER_HZ) {
        for(int i=0;i<50;i++) {
            uint32_t seq=shared->telemetry_seq;rv_fence();if(seq&1)continue;
            uint64_t boot=shared->boot_time,idle=shared->idle_ticks,open=shared->idle_since,stamp;
            unsigned state=shared->state,sleep=shared->sleep_mode;
            __asm__ volatile("rdtime %0":"=r"(stamp));
            rv_fence();
            if(seq==shared->telemetry_seq && stamp>=boot && (!open || stamp>=open)){
                uint64_t elapsed=stamp-boot;
                if(open)idle+=stamp-open;
                s->c1_elapsed=elapsed;s->c1_idle=idle;s->c1_state=state;s->c1_sleep=sleep;
                s->c1_valid=state!=RV_FAULT && state!=RV_BOOT && elapsed>=idle;break;
            }
        }
    }
    char buf[256];unsigned version=0;
    s->npu_loaded=access("/sys/module/soph_tpu",F_OK)==0;
    if(text_file("/proc/tpu/rvmonitor",buf,sizeof(buf))>0 &&
       sscanf(buf,"version=%u now_ns=%"SCNu64" busy_ns=%"SCNu64" jobs=%"SCNu64" active=%u",
              &version,&s->npu_time,&s->npu_busy,&s->npu_jobs,&s->npu_active)==5 && version==1)s->npu_valid=1;
    f=fopen("/sys/kernel/debug/clk/clk_summary","r");
    if(f){char line[512],name[128];unsigned enable,prepare,protect;uint64_t rate;
        while(fgets(line,sizeof(line),f))if(sscanf(line,"%127s %u %u %u %"SCNu64,name,&enable,&prepare,&protect,&rate)==5){
            const char *names[]={"clk_c906_0","clk_c906_1","clk_tpu"};
            for(int i=0;i<3;i++)if(!strcmp(name,names[i])){s->clock_hz[i]=rate;s->clock_enabled[i]=enable;}
        }fclose(f);}
    if(text_file("/sys/class/thermal/thermal_zone0/temp",buf,sizeof(buf))>0)s->temperature_mC=strtol(buf,NULL,10);
    f=fopen("/proc/meminfo","r");
    if(f){char line[128];long val;while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"MemTotal: %ld kB",&val)==1)s->memory_total_kB=val;
        if(sscanf(line,"MemAvailable: %ld kB",&val)==1)s->memory_available_kB=val;
    }fclose(f);}
    s->memory_used_kB=s->memory_total_kB-s->memory_available_kB;
    if(s->memory_used_kB<0)s->memory_used_kB=0;
    struct rusage u;if(!getrusage(RUSAGE_SELF,&u))s->self_ns=((uint64_t)u.ru_utime.tv_sec+u.ru_stime.tv_sec)*1000000000ULL+((uint64_t)u.ru_utime.tv_usec+u.ru_stime.tv_usec)*1000;
    s->wall_ns=now_ns();
}
static double percent(uint64_t now,uint64_t old,uint64_t total,uint64_t oldtotal,int valid,int inverse) {
    if(!valid || total<=oldtotal || now<old)return -1;
    uint64_t d=now-old,t=total-oldtotal;if(d>t)return -1;
    return 100.0*(inverse?1.0-(double)d/t:(double)d/t);
}
static void number(FILE *out,double n){if(n<0)fprintf(out,"null");else fprintf(out,"%.4f",n);}
/* C0/NPU activity covers the last sample window; C1 job/sleep is a snapshot. */
static const char *c0_state(double load) {
    return load<0?"N/A":load>0?"Running":"Idle";
}
static const char *c1_state(const struct snapshot *s) {
    if(!s->c1_valid)return "N/A";
    if(s->c1_state==RV_RUN)return "Running";
    return "Idle";
}
static const char *npu_state(const struct snapshot *s,double load) {
    return !s->npu_valid?"N/A":s->npu_active?"Running":load<0?"N/A":load>0?"Running":"Idle";
}
static void json_sample(FILE *out,const struct snapshot *s,double c0,double c1,double npu,double overhead,unsigned interval) {
    fprintf(out,"{\"monotonic_ns\":%"PRIu64",\"temperature_mC\":%ld,\"C0_percent\":",s->wall_ns,s->temperature_mC);number(out,c0);
    fprintf(out,",\"C1_percent\":");number(out,c1);fprintf(out,",\"NPU_command_busy_percent\":");number(out,npu);
    fprintf(out,",\"monitor_percent\":");number(out,overhead);
    fprintf(out,",\"clock_Hz\":[%"PRIu64",%"PRIu64",%"PRIu64"],\"clock_enabled\":[%u,%u,%u],\"C1_state\":%u,\"C1_elapsed_ticks\":%"PRIu64",\"C1_idle_ticks\":%"PRIu64",\"NPU_jobs\":%"PRIu64",\"NPU_busy_ns\":%"PRIu64",\"NPU_counter_time_ns\":%"PRIu64",\"NPU_active\":%u,\"NPU_loaded\":%s,\"NPU_owned\":%s,\"memory_total_kB\":%ld,\"memory_available_kB\":%ld,\"memory_used_kB\":%ld,\"C1_sleep_mode\":%u,\"CPU0_state\":\"%s\",\"CPU1_state\":\"%s\",\"NPU_state\":\"%s\",\"sample_interval_ms\":%u}\n",
           s->clock_hz[0],s->clock_hz[1],s->clock_hz[2],s->clock_enabled[0],s->clock_enabled[1],s->clock_enabled[2],s->c1_state,s->c1_elapsed,s->c1_idle,s->npu_jobs,s->npu_busy,s->npu_time,s->npu_active,s->npu_loaded?"true":"false",loaded_here?"true":"false",s->memory_total_kB,s->memory_available_kB,s->memory_used_kB,s->c1_sleep,c0_state(c0),c1_state(s),npu_state(s,npu),interval);
}
static void row(const char *label,double load,uint64_t hz,const char *state,int color) {
    printf("%-22s  ",label);
    if(color)printf("\033[36m");
    putchar('[');int bars=load<0?0:(int)(load/5+.5);
    for(int i=0;i<20;i++)putchar(i<bars?'#':' ');
    printf("]");if(color)printf("\033[0m");
    char value[32];if(load<0)snprintf(value,sizeof(value),"N/A");
    else snprintf(value,sizeof(value),"%.1f%%",load);
    printf(" %7s  ",value);
    if(hz)printf("%8.1f  ",hz/1000000.0);else printf("%8s  ","N/A");
    printf("%s\n",state);
}
static void display(const struct snapshot *s,double c0,double c1,double npu,int tty,unsigned interval,int settings) {
    /* Erase the previous frame: shorter state/number strings must not leave suffixes. */
    if(tty)printf("\033[H\033[2J\033[1;36m");
    printf("rvmonitor  /  Lichee RV Nano SG2002");
    if(tty)printf("\033[0m");
    struct utsname u;uname(&u);printf("\nLinux %s\n\n",u.release);
    if(s->temperature_mC>=0)printf("TEMP  %.1f C",s->temperature_mC/1000.0);else printf("TEMP  N/A");
    printf("    RAM USED  %.1f / %.1f MiB\n\n",s->memory_used_kB/1024.0,s->memory_total_kB/1024.0);
    printf("%-22s  %-22s %7s  %8s  %s\n","CORE","LOAD","%","MHz*","STATE");
    row("CPU0  C906 / Linux",c0,s->clock_hz[0],c0_state(c0),tty);
    row("CPU1  C906L / RTOS",c1,s->clock_hz[1],c1_state(s),tty);
    row("NPU   SG2002",npu,s->clock_hz[2],npu_state(s,npu),tty);
    printf("\n* configured clock    Sample: %u ms\n",interval);
    if(settings) {
        puts("Sampling settings (this session)\n"
             "1: 100 ms   2: 250 ms   3: 500 ms\n"
             "4: 1 s      5: 2 s      6: 5 s\n"
             "1-6: select    s / Esc: back    q: exit");
    }else puts("s: sampling settings    q / Esc: exit");
    if(tty)printf("\033[J");
    fflush(stdout);
}
static int compare_u64(const void *a,const void *b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
static int summarize_test(const char *file,int c1,const char *label) {
    FILE *f=fopen(file,"r");if(!f){perror(file);return 1;}
    char header[128];if(!fgets(header,sizeof(header),f)){fclose(f);return 1;}
    uint64_t wake[1000],max=0;unsigned count=0,miss=0,index;uint64_t a,b,c;
    while(fscanf(f,"%u,%"SCNu64",%"SCNu64",%"SCNu64"\n",&index,&a,&b,&c)==4){
        if(count>=1000 || index!=count || a>b || b>c){fclose(f);return 1;}
        wake[count++]=b-a;if(c-a>max)max=c-a;if(c-a>(c1?25000ULL:1000000ULL))miss++;
    }fclose(f);if(count!=1000)return 1;qsort(wake,count,sizeof(wake[0]),compare_u64);
    double factor=c1?.04:.001;
    printf("%s: n=%u, wake median/p99/max %.2f/%.2f/%.2f us, max finish %.2f us, late1ms=%u\n",
           label,count,(wake[499]+wake[500])*.5*factor,wake[989]*factor,wake[999]*factor,max*factor,miss);
    return 0;
}
static int cpu_test(int c1) {
    char dir[]="/run/rvmonitor-test-XXXXXX";if(!mkdtemp(dir)){perror("mkdtemp");return 1;}
    char file[128],output[128];snprintf(file,sizeof(file),"%s/control.csv",dir);snprintf(output,sizeof(output),"%s/output.log",dir);
    struct pid_state oracle={0,0,0,2166136261u};for(unsigned i=0;i<1000;i++)pid_step(&oracle,i);
    int result=0;
    if(c1){
        char *const ready[]={RTOS_CTL,"ready",NULL};result=run(ready,0);
        if(!result){char *const args[]={RTOS_CTL,"run","1000",file,NULL};result=run(args,0);}
        if(!result && (!shared || shared->state!=RV_DONE || shared->completed!=1000 || shared->checksum!=oracle.checksum))result=1;
        if(!result)result=summarize_test(file,1,"C1 FreeRTOS");
    }else {
        const char *modes[]={"other","fifo"};
        for(int i=0;i<2 && !result;i++){
            char *const args[]={LINUX_PID,"1000",file,(char*)modes[i],NULL};result=run(args,output);
            if(!result){char b[512];if(text_file(output,b,sizeof(b))<0)result=1;else{
                fputs(b,stdout);char *value=strstr(b,"\"checksum\":");
                if(!value || strtoul(value+11,NULL,10)!=oracle.checksum)result=1;
            }}
            if(!result)result=summarize_test(file,0,i?"C0 FIFO20":"C0 OTHER");
        }
    }
    unlink(file);unlink(output);rmdir(dir);
    printf("%s: expected PID/plant checksum=%u (same Q16 oracle). %s\n",c1?"C1":"C0",oracle.checksum,result?"FAIL":"PASS");
    return result;
}
static int npu_irq_count(uint64_t *count) {
    FILE *f=fopen("/proc/interrupts","r");if(!f){perror("/proc/interrupts");return 1;}
    char line[512];int found=0;*count=0;
    while(fgets(line,sizeof(line),f))if(strstr(line,"cvi-tpu-tdma")) {
        char *p=strchr(line,':');if(!p)break;p++;unsigned cpus=0;
        for(;;){while(*p==' ' || *p=='\t')p++;if(*p<'0' || *p>'9')break;
            char *end;errno=0;unsigned long long n=strtoull(p,&end,10);
            if(errno || end==p)break;
            *count+=n;cpus++;p=end;
        }
        found=cpus>0;break;
    }
    fclose(f);if(!found)fprintf(stderr,"NPU IRQ counter unavailable.\n");return !found;
}
static int npu_test(void) {
    if(access(NPU_TEST,X_OK)){perror(NPU_TEST);return 1;}
    if(verified_kernel() || verified_hash("/usr/bin/sha256sum " NPU_TEST,NPU_TEST_SHA))return 1;
    uint64_t before,after;if(npu_irq_count(&before))return 1;
    struct rlimit cores={0,0};if(setrlimit(RLIMIT_CORE,&cores)){perror("RLIMIT_CORE");return 1;}
    char *const args[]={NPU_TEST,NULL};int result=run(args,0);
    if(!result && (npu_irq_count(&after) || after<=before))result=1;
    if(result){fprintf(stderr,"NPU self-test failed (exit=%d).\n",result);return result;}
    printf("NPU: 4 INT8 cases, 121212 values match CPU; IRQ delta=%"PRIu64". PASS\n",after-before);
    return 0;
}
static int positive_arg(const char *arg,unsigned maximum,unsigned *value) {
    if(*arg<'0' || *arg>'9')return 1;
    char *end;errno=0;unsigned long n=strtoul(arg,&end,10);
    if(errno || *end || !n || n>maximum)return 1;
    *value=(unsigned)n;return 0;
}
static int open_log(const char *path) {
    int fd=open(path,O_CREAT|O_WRONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if(fd<0){perror(path);return 1;}
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || flock(fd,LOCK_EX|LOCK_NB) || ftruncate(fd,0)) {
        fprintf(stderr,"Log must be a writable regular file without another writer: %s\n",path);close(fd);return 1;
    }
    log_file=fdopen(fd,"w");if(!log_file){perror("fdopen");close(fd);return 1;}
    return 0;
}
static void usage(void) {
    puts("rvmonitor\n"
         "rvmonitor --nputest | --c0test | --c1test\n"
         "rvmonitor --logging [\"PATH\" [SAMPLES [INTERVAL_MS]]]\n"
         "Defaults: /root/rvmonitor_log, 10 samples, 1000 ms.\n"
         "Keys: s sampling, q/Esc exit. Root required.\n"
         "rvmonitor --help");
}
int main(int argc,char **argv) {
    unsigned interval=1000,limit=0;int logging=0,test=0;
    const char *log_path="/root/rvmonitor_log";
    if(argc==2 && !strcmp(argv[1],"--help")){usage();return 0;}
    if(argc>1) {
        if(!strcmp(argv[1],"--logging")) {
            if(argc>5){usage();return 2;}logging=1;limit=10;
            if(argc>=3){log_path=argv[2];if(!*log_path || *log_path=='-'){usage();return 2;}}
            if(argc>=4 && positive_arg(argv[3],UINT_MAX,&limit)){usage();return 2;}
            if(argc>=5 && positive_arg(argv[4],INT_MAX,&interval)){usage();return 2;}
        }else if(argc==2 && (!strcmp(argv[1],"--nputest") || !strcmp(argv[1],"--c0test") || !strcmp(argv[1],"--c1test"))) {
            test=!strcmp(argv[1],"--nputest")?1:!strcmp(argv[1],"--c0test")?2:3;
        }else{usage();return 2;}
    }
    if(geteuid()){fprintf(stderr,"Run as root: sudo rvmonitor\n");return 1;}
    atexit(cleanup);signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGHUP,stop_signal);signal(SIGPIPE,stop_signal);
    if(test && lock_path("/run/rvmonitor-test.lock",&test_lock_fd,
                         "Another rvmonitor test is running, or /run test lock is unavailable."))return 1;
    map_c1();
    if(test==2 || test==3)return cpu_test(test==3);
    if(logging && open_log(log_path))return 1;
    if(join_npu())return 1;
    if(test==1)return npu_test();
    int tty=!logging && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    /* One buffered flush per frame avoids exposing a half-drawn screen. */
    if(tty)setvbuf(stdout,NULL,_IOFBF,2048);
    if(tty && !tcgetattr(STDIN_FILENO,&saved_terminal)) {
        struct termios raw=saved_terminal;raw.c_lflag&=~(ICANON|ECHO);raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
        if(!tcsetattr(STDIN_FILENO,TCSANOW,&raw)){terminal_active=1;printf("\033[?1049h\033[?25l");}
    }
    struct snapshot before,after;sample(&before);unsigned emitted=0;
    double c0=-1,c1=-1,npu=-1;int settings=0;
    uint64_t next=before.wall_ns+(uint64_t)interval*1000000ULL;
    if(tty)display(&before,c0,c1,npu,tty,interval,settings);
    while(!stopped && (!limit || emitted<limit)) {
        uint64_t current=now_ns();
        uint64_t wait=next>current?next-current:0;
        if(tty) {
            struct pollfd p={STDIN_FILENO,POLLIN,0};
            int ready=poll(&p,1,(int)((wait+999999)/1000000));
            if(ready>0) {
                char keys[32];ssize_t n=read(STDIN_FILENO,keys,sizeof(keys));
                int changed=0,redraw=0;
                if(n<=0)stopped=1;
                for(ssize_t i=0;i<n;i++) {
                    char key=keys[i];
                    if(key=='q' || key=='Q'){stopped=1;quit_key=1;break;}
                    if(key==27){if(settings){settings=0;redraw=1;}else{stopped=1;quit_key=1;break;}}
                    else if(key=='s' || key=='S'){settings=!settings;redraw=1;}
                    else if(settings && key>='1' && key<='6') {
                        static const unsigned presets[]={100,250,500,1000,2000,5000};
                        interval=presets[key-'1'];settings=0;changed=1;redraw=1;
                    }
                }
                if(stopped)break;
                if(changed) {
                    /* Restart the measurement window at the newly chosen interval. */
                    sample(&before);c0=c1=npu=-1;
                    next=before.wall_ns+(uint64_t)interval*1000000ULL;
                }
                if(redraw)display(&before,c0,c1,npu,tty,interval,settings);
            }
        }else {
            struct timespec delay={(time_t)(wait/1000000000ULL),(long)(wait%1000000000ULL)};
            while(nanosleep(&delay,&delay) && errno==EINTR && !stopped){};
        }
        if(stopped)break;
        if(now_ns()<next)continue;
        sample(&after);
        c0=percent(after.linux_idle,before.linux_idle,after.linux_total,before.linux_total,after.linux_valid && before.linux_valid,1);
        c1=percent(after.c1_idle,before.c1_idle,after.c1_elapsed,before.c1_elapsed,after.c1_valid && before.c1_valid,1);
        npu=percent(after.npu_busy,before.npu_busy,after.npu_time,before.npu_time,after.npu_valid && before.npu_valid,0);
        double overhead=percent(after.self_ns,before.self_ns,after.wall_ns,before.wall_ns,1,0);
        if(logging) {
            json_sample(log_file,&after,c0,c1,npu,overhead,interval);
            if(fflush(log_file) || ferror(log_file)){perror("write log");return 1;}
        }else display(&after,c0,c1,npu,tty,interval,settings);
        fflush(stdout);before=after;emitted++;
        next=after.wall_ns+(uint64_t)interval*1000000ULL;
    }
    if(log_file) {
        if(fclose(log_file)){log_file=NULL;perror("close log");return 1;}
        log_file=NULL;
        printf("Log: %s (%u samples, %u ms)\n",log_path,emitted,interval);
    }
    return stopped && !quit_key?130:0;
}
