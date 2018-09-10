#include "nanoscope_propertywatcher.h"
#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <time.h>

// perf_event_open API
static int perf_event_open(const perf_event_attr& attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {  // NOLINT
  return syscall(__NR_perf_event_open, &attr, pid, cpu, group_fd, flags);
}
#include <linux/unistd.h>
#include <signal.h>
#include <time.h>
#endif

art::Thread* art::NanoscopePropertyWatcher::tracing_thread_ = NULL;
art::NanoscopePropertyWatcher::SampleMode art::NanoscopePropertyWatcher::sample_mode_ = art::NanoscopePropertyWatcher::kSampleDisabled;
#if defined(__ANDROID__)
int art::NanoscopePropertyWatcher::perf_timer_fd_ = -1;
int art::NanoscopePropertyWatcher::sample_fd_ [] = { [0 ... (NUM_COUNTER - 1)] = -1 };
struct perf_event_mmap_page* art::NanoscopePropertyWatcher::perf_timer_page_ = NULL;
#endif

namespace art{
  void NanoscopePropertyWatcher::set_up_timer(){
#if defined(__ANDROID__)
    if(sample_mode_ == kSamplePerf){

      // Set up perf_event counter that acts as a timer
      struct perf_event_attr pe;
      memset(&pe, 0, sizeof(struct perf_event_attr));
      pe.type = PERF_TYPE_SOFTWARE;
      pe.size = sizeof(struct perf_event_attr);
      pe.config = PERF_COUNT_SW_CPU_CLOCK;
      pe.sample_period = sample_interval_;
      pe.sample_type = PERF_SAMPLE_TIME;
      pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
      pe.disabled = 1;
      pe.pinned = 1;
      pe.wakeup_events = 1;

      // perf_event_open pid = -1, cpuid = 0, counts cpu time of all threads on cpu0
      // which essentially gives us wall clock time
      perf_timer_fd_ = perf_event_open(pe, -1, 0, -1, 0);
      if (perf_timer_fd_ < 0) {
        LOG(ERROR) << "nanoscope: Fail to open perf event file: master ";
        LOG(ERROR) << "nanoscope: " << strerror(errno);
        return;
      }

      void* p = mmap(NULL, (1+1)*PERF_PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, perf_timer_fd_, 0);
      perf_timer_page_ = (struct perf_event_mmap_page*)p;
      fcntl(perf_timer_fd_, F_SETFL, O_ASYNC);
      fcntl(perf_timer_fd_, F_SETSIG, SIGTIMER);

      // Deliver the signal to the tracing thread when counter overflows
      struct f_owner_ex fown_ex;
      fown_ex.type = F_OWNER_TID;
      fown_ex.pid  = tracing_thread_->GetTid();
      int ret = fcntl(perf_timer_fd_, F_SETOWN_EX, &fown_ex);
      if (ret == -1) {
        LOG(ERROR) << "nanoscope: Failed to set the owner of the perf event file";
        return;
      }
    } else if (sample_mode_ == kSampleCpu){
      struct sigevent sev;
      struct itimerspec its;
      long long freq_nanosecs;
      sev.sigev_notify = SIGEV_THREAD_ID;
      sev.sigev_signo = SIGTIMER;
      sev.sigev_notify_thread_id = tracing_thread_ -> GetTid();
      sev.sigev_value.sival_ptr = &timer_id_;
      if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &timer_id_) == -1)
        LOG(ERROR) << "nanoscope: Failed to create timer";;
      freq_nanosecs = sample_interval_;   // 1ms
      its.it_value.tv_sec = freq_nanosecs / 1000000000;
      its.it_value.tv_nsec = freq_nanosecs % 1000000000;
      its.it_interval.tv_sec = its.it_value.tv_sec;
      its.it_interval.tv_nsec = its.it_value.tv_nsec;

      if (timer_settime(timer_id_, 0, &its, NULL) == -1)
        LOG(ERROR) << "nanoscope: Failed to set timer";;
    } else {
        UNREACHABLE();
    }
#endif
  }

#if defined(__ANDROID__)
  void NanoscopePropertyWatcher::set_up_sample_counter(int counter_index, int groupfd,
    uint32_t type, uint64_t config){
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = type;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = config;
    pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
    pe.disabled = 1;

    // Pid = tracing thread's tid, cpuid = -1. Counts tracing thread on any cpu.
    sample_fd_[counter_index] = perf_event_open(pe, tracing_thread_->GetTid(), -1, groupfd, 0);
    if (sample_fd_[counter_index] < 0) {
      LOG(ERROR) << "nanoscope: Fail to open perf event file: slave ";
      LOG(ERROR) << "nanoscope: " << strerror(errno);
      return;
    }
    fcntl(sample_fd_[counter_index], F_SETFL, O_ASYNC);
  }

  void NanoscopePropertyWatcher::signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED) {
    // Read perf_event sample values
    char buf[4096];
    struct read_format* rf = (struct read_format*) buf;
    read(sample_fd_[0], buf, sizeof(buf));
    uint64_t vals[NUM_COUNTER] = {0};
    for(int i = 0; i < NUM_COUNTER; i++){
      vals[i] = rf->values[i].value;
    }

    // Read thread CPU time
    struct timespec thread_cpu_time;
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_cpu_time) < 0){
      LOG(ERROR) << "nanoscope: error get clock time";
    }

    tracing_thread_ -> TimerHandler(thread_cpu_time.tv_sec * 1e+9 + thread_cpu_time.tv_nsec, vals[0], vals[1], vals[2]);

    // Restart timer
    if(sample_mode_ == kSamplePerf){
      ioctl(perf_timer_fd_, PERF_EVENT_IOC_RESET, 0);
      ioctl(perf_timer_fd_, PERF_EVENT_IOC_REFRESH, 1);
    }
  }

  void NanoscopePropertyWatcher::install_sig_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(SIGTIMER, &sa, NULL) < 0) {
      LOG(ERROR) << "nanoscope: Error setting up signal handler.";
      return;
    }
    LOG(INFO) << "nanoscope: set up sig handler for SIGTIMER";
  }
#endif

}
