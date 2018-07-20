#include "nanoscope_propertywatcher.h"
#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <time.h>
static int perf_event_open(const perf_event_attr& attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {  // NOLINT
  return syscall(__NR_perf_event_open, &attr, pid, cpu, group_fd, flags);
}
#include <linux/unistd.h>
#include <signal.h>
#include <time.h>
#endif

art::Thread* art::NanoscopePropertyWatcher::traced = NULL;
#if defined(__ANDROID__)
int art::NanoscopePropertyWatcher::fd = -1;
int art::NanoscopePropertyWatcher::sample_fd [] = { [0 ... (NUM_COUNTER - 1)] = -1 };
// uint64_t art::NanoscopePropertyWatcher::id = -1;
// uint64_t art::NanoscopePropertyWatcher::id2 = -1;
struct perf_event_mmap_page* art::NanoscopePropertyWatcher::page = NULL;
#endif

namespace art{
  void NanoscopePropertyWatcher::setup_timer(){
#if defined(__ANDROID__)
    if(use_perf){
      LOG(INFO) << "nanoscope: start_tracing for thread " << to_trace -> GetTid();
      install_sig_handler();
      // singal timer setup
      struct perf_event_attr pe;
      memset(&pe, 0, sizeof(struct perf_event_attr));
      pe.type = PERF_TYPE_SOFTWARE;
      pe.size = sizeof(struct perf_event_attr);
      pe.config = PERF_COUNT_SW_CPU_CLOCK;
      pe.sample_period = interval;
      pe.sample_type = PERF_SAMPLE_TIME;
      pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
      pe.disabled = 1;
      pe.pinned = 1;
      pe.wakeup_events = 1;
      // fd = perf_event_open(pe, traced->GetTid(), -1, -1, 0);
      fd = perf_event_open(pe, -1, 0, -1, 0);
      if (fd < 0) {
        LOG(ERROR) << "nanoscope: Fail to open perf event file: master ";
        LOG(ERROR) << "nanoscope: " << strerror(errno);
        return;
      }
      // ioctl(fd, PERF_EVENT_IOC_ID, &id);

      void* p = mmap(NULL, (1+1)*PERF_PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
      page = (struct perf_event_mmap_page*)p;
      fcntl(fd, F_SETFL, O_ASYNC);
      fcntl(fd, F_SETSIG, SIGTIMER);

      // Deliver the signal to the thread
      struct f_owner_ex fown_ex;
      fown_ex.type = F_OWNER_TID;
      fown_ex.pid  = traced->GetTid();
      int ret = fcntl(fd, F_SETOWN_EX, &fown_ex);
      if (ret == -1) {
        LOG(ERROR) << "nanoscope: Failed to set the owner of the perf event file";
        return;
      }
    } else {
      install_sig_handler();
      struct sigevent sev;
      struct itimerspec its;
      long long freq_nanosecs;
      sev.sigev_notify = SIGEV_THREAD_ID;
      sev.sigev_signo = SIGTIMER;
      sev.sigev_notify_thread_id = traced -> GetTid();
      sev.sigev_value.sival_ptr = &timerid;
      if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &timerid) == -1)
        LOG(ERROR) << "nanoscope: Failed to create timer";;
      freq_nanosecs = 100000000;   // 1ms
      its.it_value.tv_sec = freq_nanosecs / 1000000000;
      its.it_value.tv_nsec = freq_nanosecs % 1000000000;
      its.it_interval.tv_sec = its.it_value.tv_sec;
      its.it_interval.tv_nsec = its.it_value.tv_nsec;

      if (timer_settime(timerid, 0, &its, NULL) == -1)
        LOG(ERROR) << "nanoscope: Failed to set timer";;
    }
#endif
  }

#if defined(__ANDROID__)
  void NanoscopePropertyWatcher::set_up_counter(int sample_index, int groupfd,
    uint32_t type, uint64_t config){
    // set up the sample counters
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = type;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = config;
    pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
    pe.disabled = 1;

    sample_fd[sample_index] = perf_event_open(pe, traced->GetTid(), -1, groupfd, 0);
    if (sample_fd[sample_index] < 0) {
      LOG(ERROR) << "nanoscope: Fail to open perf event file: slave ";
      LOG(ERROR) << "nanoscope: " << strerror(errno);
      return;
    }
    fcntl(sample_fd[sample_index], F_SETFL, O_ASYNC);
  }

  void NanoscopePropertyWatcher::signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED) {
    char buf[4096];
    struct read_format* rf = (struct read_format*) buf;
    read(sample_fd[0], buf, sizeof(buf));
    uint64_t vals[NUM_COUNTER] = {0};
    for(int i = 0; i < NUM_COUNTER; i++){
      vals[i] = rf->values[i].value;
    }

    struct timespec thread_cpu_time;
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_cpu_time) < 0){
      LOG(ERROR) << "nanoscope: error get clock time";
    }
    traced -> TimerHandler(thread_cpu_time.tv_sec * 1e+9 + thread_cpu_time.tv_nsec, vals[0], vals[1], vals[2]);
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
  }

  void NanoscopePropertyWatcher::signal_handler_timer(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED) {
    struct timespec thread_cpu_time;
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_cpu_time) < 0){
      LOG(ERROR) << "nanoscope: error get clock time";
    }
    traced -> TimerHandler(thread_cpu_time.tv_sec * 1e+9 + thread_cpu_time.tv_nsec);
  }

  void NanoscopePropertyWatcher::install_sig_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    if(use_perf)
      sa.sa_sigaction = signal_handler;
    else
      sa.sa_sigaction = signal_handler_timer;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(SIGTIMER, &sa, NULL) < 0) {
      LOG(ERROR) << "nanoscope: Error setting up signal handler.";
      return;
    }
    LOG(INFO) << "nanoscope: set up sig handler for SIGTIMER";
  }
#endif

}
