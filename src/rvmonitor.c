/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Nguyen Dong Quan
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
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
static volatile sig_atomic_t stopped;
static int loaded_here, terminal_active, lock_fd=-1, test_lock_fd=-1, quit_key;
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
    if(loaded_here && access("/sys/module/soph_tpu",F_OK)==0) {
        char buf[256];unsigned version,active;uint64_t stamp,busy,jobs;
        int pending=0;
        if(text_file("/proc/tpu/rvmonitor",buf,sizeof(buf))>0 &&
           sscanf(buf,"version=%u now_ns=%"SCNu64" busy_ns=%"SCNu64" jobs=%"SCNu64" active=%u",&version,&stamp,&busy,&jobs,&active)==5)
            pending=(active!=0);
        if(text_file("/sys/module/soph_tpu/refcnt",buf,sizeof(buf))>0 && strtol(buf,NULL,10)>0)pending=1;
        if(pending)fprintf(stderr,"NPU driver retained: another open device or command is active.\n");
        else {
            /* Never force unload. Kernel reference checks cover concurrent users. */
            pid_t p=fork();int status=0;
            if(!p){execl("/sbin/rmmod","rmmod","soph_tpu",(char*)NULL);_exit(127);}
            if(p>0){while(waitpid(p,&status,0)<0 && errno==EINTR){};
                if(!WIFEXITED(status) || WEXITSTATUS(status))fprintf(stderr,"NPU unload refused; driver retained.\n");}
        }
        loaded_here=0;
    }
    if(lock_fd>=0){close(lock_fd);lock_fd=-1;}
    if(test_lock_fd>=0){close(test_lock_fd);test_lock_fd=-1;}
}
static int lock_path(const char *path,int *fd,const char *message) {
    *fd=open(path,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(*fd<0 || flock(*fd,LOCK_EX|LOCK_NB)){
        fprintf(stderr,"%s\n",message);return 1;}
    return 0;
}
static int ensure_npu(void) {
    if(access("/sys/module/soph_tpu",F_OK)==0)return 0;
    struct utsname u;if(uname(&u) || strcmp(u.release,"5.10.271-rvnano") ||
       strcmp(u.version,"#4 Fri Oct 9 02:40:26 +07 2026")){
        fprintf(stderr,"NPU module requires the verified kernel 5.10.271-rvnano build #4.\n");return 1;}
    /* Fixed path, with a build-time digest; no command contains user input. */
    FILE *p=popen("/usr/bin/sha256sum " NPU_MODULE,"r");char digest[65]={0};
    if(!p){perror("sha256sum");return 1;}
    int found=fscanf(p,"%64s",digest);int result=pclose(p);
    if(found!=1 || result || strcmp(digest,RVMON_NPU_SHA256)){
        fprintf(stderr,"NPU telemetry module hash mismatch.\n");return 1;}
    char *const args[]={"/sbin/insmod",NPU_MODULE,NULL};
    if(run(args,0))return 1;
    loaded_here=1;return 0;
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
static void number(double n){if(n<0)printf("null");else printf("%.4f",n);}
/* C0/NPU activity covers the last sample window; C1 job/sleep is a snapshot. */
static const char *c0_state(double load) {
    return load<0?"N/A":load>0?"Active":"Idle";
}
static const char *c1_state(const struct snapshot *s) {
    if(!s->c1_valid)return "N/A";
    if(s->c1_state==RV_RUN)return "Running";
    return s->c1_sleep==2?"Sleep":"Idle";
}
static const char *npu_state(const struct snapshot *s,double load) {
    return s->npu_active?"Busy":load<0?"N/A":load>0?"Busy":"Idle";
}
static void json_sample(const struct snapshot *s,double c0,double c1,double npu,double overhead) {
    printf("{\"monotonic_ns\":%"PRIu64",\"temperature_mC\":%ld,\"C0_percent\":",s->wall_ns,s->temperature_mC);number(c0);
    printf(",\"C1_percent\":");number(c1);printf(",\"NPU_command_busy_percent\":");number(npu);
    printf(",\"monitor_percent\":");number(overhead);
    printf(",\"clock_Hz\":[%"PRIu64",%"PRIu64",%"PRIu64"],\"clock_enabled\":[%u,%u,%u],\"C1_state\":%u,\"C1_elapsed_ticks\":%"PRIu64",\"C1_idle_ticks\":%"PRIu64",\"NPU_jobs\":%"PRIu64",\"NPU_busy_ns\":%"PRIu64",\"NPU_counter_time_ns\":%"PRIu64",\"NPU_active\":%u,\"NPU_loaded\":%s,\"NPU_owned\":%s,\"memory_total_kB\":%ld,\"memory_available_kB\":%ld,\"memory_used_kB\":%ld,\"C1_sleep_mode\":%u,\"CPU0_state\":\"%s\",\"CPU1_state\":\"%s\",\"NPU_state\":\"%s\"}\n",
           s->clock_hz[0],s->clock_hz[1],s->clock_hz[2],s->clock_enabled[0],s->clock_enabled[1],s->clock_enabled[2],s->c1_state,s->c1_elapsed,s->c1_idle,s->npu_jobs,s->npu_busy,s->npu_time,s->npu_active,s->npu_loaded?"true":"false",loaded_here?"true":"false",s->memory_total_kB,s->memory_available_kB,s->memory_used_kB,s->c1_sleep,c0_state(c0),c1_state(s),npu_state(s,npu));
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
static void display(const struct snapshot *s,double c0,double c1,double npu,int tty) {
    if(tty)printf("\033[H\033[1;36m");
    printf("rvmonitor  /  Lichee RV Nano SG2002");
    if(tty)printf("\033[0m");
    struct utsname u;uname(&u);printf("\nLinux %s\n\n",u.release);
    if(s->temperature_mC>=0)printf("TEMP  %.1f C",s->temperature_mC/1000.0);else printf("TEMP  N/A");
    printf("    RAM USED  %.1f / %.1f MiB\n\n",s->memory_used_kB/1024.0,s->memory_total_kB/1024.0);
    printf("%-22s  %-22s %7s  %8s  %s\n","CORE","LOAD","%","MHz*","STATE");
    row("CPU0  C906 / Linux",c0,s->clock_hz[0],c0_state(c0),tty);
    row("CPU1  C906L / RTOS",c1,s->clock_hz[1],c1_state(s),tty);
    row("NPU   SG2002",npu,s->clock_hz[2],npu_state(s,npu),tty);
    puts("\n* configured clock    q / Esc: exit    --help: commands");
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
static void usage(void) {
    puts("rvmonitor [--once | --samples N] [--json] [--interval MS]\n"
         "rvmonitor --nputest | --c0test | --c1test\n"
         "Root required. Default refresh 250 ms. CPU0=C0/Linux; CPU1=C1/FreeRTOS.\n"
         "CPU tests can run in another terminal while the monitor is open.\n"
         "One test at a time. Close the monitor before --nputest.\n"
         "Quit with q/Esc/Ctrl+C; unload only a driver this invocation loaded.\n"
         "Pre-existing/active NPU drivers are retained. No clock or voltage changes.");
}
int main(int argc,char **argv) {
    unsigned interval=250,limit=0;int json=0,test=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")){usage();return 0;}
        else if(!strcmp(argv[i],"--once"))limit=1;
        else if(!strcmp(argv[i],"--json"))json=1;
        else if(!strcmp(argv[i],"--samples") || !strcmp(argv[i],"--interval")){
            int sampling=!strcmp(argv[i],"--samples");if(++i==argc){usage();return 2;}
            char *end;unsigned long val=strtoul(argv[i],&end,10);
            if(*end || (sampling?(val<1 || val>10000):(val<100 || val>10000))){usage();return 2;}
            if(sampling)limit=(unsigned)val;else interval=(unsigned)val;
        }else if(!strcmp(argv[i],"--nputest") || !strcmp(argv[i],"--c0test") || !strcmp(argv[i],"--c1test")){
            if(test){usage();return 2;}test=!strcmp(argv[i],"--nputest")?1:!strcmp(argv[i],"--c0test")?2:3;
        }else{usage();return 2;}
    }
    if(geteuid()){fprintf(stderr,"Run as root: sudo rvmonitor\n");return 1;}
    atexit(cleanup);signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGHUP,stop_signal);signal(SIGPIPE,stop_signal);
    if(test && lock_path("/run/rvmonitor-test.lock",&test_lock_fd,
                         "Another rvmonitor test is running, or /run test lock is unavailable."))return 1;
    /* CPU tests never own the NPU module and may run beside one observer. */
    if((!test || test==1) && lock_path("/run/rvmonitor.lock",&lock_fd,
       "Another monitor/NPU test is running. CPU tests may run alongside a monitor."))return 1;
    map_c1();
    if(test==2 || test==3)return cpu_test(test==3);
    if(ensure_npu())return 1;
    if(test==1){char *const args[]={"/usr/local/bin/rvnano-npu-selftest",NULL};return run(args,0);}
    int tty=!json && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if(tty && !tcgetattr(STDIN_FILENO,&saved_terminal)) {
        struct termios raw=saved_terminal;raw.c_lflag&=~(ICANON|ECHO);raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
        if(!tcsetattr(STDIN_FILENO,TCSANOW,&raw)){terminal_active=1;printf("\033[?1049h\033[?25l");}
    }
    struct snapshot before,after;sample(&before);unsigned emitted=0;
    while(!stopped && (!limit || emitted<limit)) {
        if(tty){struct pollfd p={STDIN_FILENO,POLLIN,0};int ready=poll(&p,1,(int)interval);
            if(ready>0){char keys[32];ssize_t n=read(STDIN_FILENO,keys,sizeof(keys));if(n<=0)stopped=1;for(ssize_t i=0;i<n;i++)if(keys[i]=='q' || keys[i]=='Q' || keys[i]==27){stopped=1;quit_key=1;}}
        }else{struct timespec delay={interval/1000,(interval%1000)*1000000L};while(nanosleep(&delay,&delay) && errno==EINTR && !stopped){};}
        if(stopped)break;
        sample(&after);
        double c0=percent(after.linux_idle,before.linux_idle,after.linux_total,before.linux_total,after.linux_valid && before.linux_valid,1);
        double c1=percent(after.c1_idle,before.c1_idle,after.c1_elapsed,before.c1_elapsed,after.c1_valid && before.c1_valid,1);
        double npu=percent(after.npu_busy,before.npu_busy,after.npu_time,before.npu_time,after.npu_valid && before.npu_valid,0);
        double overhead=percent(after.self_ns,before.self_ns,after.wall_ns,before.wall_ns,1,0);
        if(json)json_sample(&after,c0,c1,npu,overhead);else display(&after,c0,c1,npu,tty);
        fflush(stdout);before=after;emitted++;
    }
    return stopped && !quit_key?130:0;
}
