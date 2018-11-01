#include "nanoscope_sampler.h"
#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <time.h>
#include "thread.h"

// perf_event_open API
static int perf_event_open(const perf_event_attr& attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {  // NOLINT
  return syscall(__NR_perf_event_open, &attr, pid, cpu, group_fd, flags);
}
#include <linux/unistd.h>
#include <signal.h>
#include <time.h>
#endif

namespace art{
Thread* NanoscopeSampler::sampling_thread_ = NULL;
SampleMode NanoscopeSampler::sample_mode_ = kSampleDisabled;
#if defined(__ANDROID__)
int64_t NanoscopeSampler::sample_interval_ = 1000000;         // 1000000ns
int NanoscopeSampler::perf_timer_fd_ = -1;
int NanoscopeSampler::sample_fd_ [] = { [0 ... (COUNTER_TYPE_LIMIT - 1)] = -1 };
struct perf_event_mmap_page* NanoscopeSampler::perf_timer_page_ = NULL;
timer_t NanoscopeSampler::timer_id_ = 0;
#endif

void NanoscopeSampler::set_up_timer(){
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
  fown_ex.pid  = sampling_thread_->GetTid();
  int ret = fcntl(perf_timer_fd_, F_SETOWN_EX, &fown_ex);
  if (ret == -1) {
    LOG(ERROR) << "nanoscope: Failed to set the owner of the perf event file";
    return;
  }
} else if (sample_mode_ == kSampleCpu) {
  struct sigevent sev;
  struct itimerspec its;
  long long freq_nanosecs;
  sev.sigev_notify = SIGEV_THREAD_ID;
  sev.sigev_signo = SIGTIMER;
  sev.sigev_notify_thread_id = sampling_thread_ -> GetTid();
  sev.sigev_value.sival_ptr = &timer_id_;
  if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &timer_id_) == -1) {
    LOG(ERROR) << "nanoscope: Failed to create timer";
  }
  freq_nanosecs = sample_interval_;   // 1ms
  its.it_value.tv_sec = freq_nanosecs / 1000000000;
  its.it_value.tv_nsec = freq_nanosecs % 1000000000;
  its.it_interval.tv_sec = its.it_value.tv_sec;
  its.it_interval.tv_nsec = its.it_value.tv_nsec;

  if (timer_settime(timer_id_, 0, &its, NULL) == -1) {
    LOG(ERROR) << "nanoscope: Failed to set timer";
  }
 } else {
  UNREACHABLE();
 }
#endif
}

#if defined(__ANDROID__)
void NanoscopeSampler::set_up_sample_counter(CounterType counter_type, int groupfd){
  uint64_t type, config;
  switch(counter_type){
    case COUNTER_TYPE_MAJOR_PAGE_FAULTS:
      type = PERF_TYPE_SOFTWARE;
      config = PERF_COUNT_SW_PAGE_FAULTS_MAJ;
      break;
    case COUNTER_TYPE_MINOR_PAGE_FAULTS:
      type = PERF_TYPE_SOFTWARE;
      config = PERF_COUNT_SW_PAGE_FAULTS_MIN;
      break;
    case COUNTER_TYPE_CONTEXT_SWITCHES:
      type = PERF_TYPE_SOFTWARE;
      config = PERF_COUNT_SW_CONTEXT_SWITCHES;
      break;
    default:
      LOG(ERROR) << "nanoscope: wrong counter type";
      return;
  }

  // Set up counter
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(struct perf_event_attr));
  pe.type = type;
  pe.size = sizeof(struct perf_event_attr);
  pe.config = config;
  pe.read_format = PERF_FORMAT_GROUP|PERF_FORMAT_ID;
  pe.disabled = 1;

  // Pid = tracing thread's tid, cpuid = -1. Counts tracing thread on any cpu.
  sample_fd_[counter_type] = perf_event_open(pe, sampling_thread_->GetTid(), -1, groupfd, 0);
  if (sample_fd_[counter_type] < 0) {
    LOG(ERROR) << "nanoscope: Fail to open perf event file: slave ";
    LOG(ERROR) << "nanoscope: " << strerror(errno);
    return;
  }
  fcntl(sample_fd_[counter_type], F_SETFL, O_ASYNC);
}

void NanoscopeSampler::signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED) {
  // Read perf_event sample values
  struct read_format rf;
  int to_read_bytes = sizeof(struct read_format);
  int read_bytes = 0;
  do {
    int r = read(sample_fd_[0], ((char*)(&rf))+read_bytes, to_read_bytes);    
    if (r == to_read_bytes) {
      break;
    } else if (r == -1) {
      LOG(ERROR) << "nanoscope: error get clock time";
      return;
    } else {
      read_bytes += r;
      to_read_bytes -= r;
    }
  } while (true);

  // Read thread CPU time
  struct timespec thread_cpu_time;
  if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_cpu_time) < 0){
    LOG(ERROR) << "nanoscope: error get clock time";
  }

  sampling_thread_ -> TimerHandler(thread_cpu_time.tv_sec * 1e+9 + thread_cpu_time.tv_nsec,
				   rf.values[0].value, rf.values[1].value, rf.values[2].value);

  // Restart timer
  if(sample_mode_ == kSamplePerf){
    ioctl(perf_timer_fd_, PERF_EVENT_IOC_RESET, 0);
    ioctl(perf_timer_fd_, PERF_EVENT_IOC_REFRESH, 1);
  }
}

void NanoscopeSampler::install_sig_handler() {
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

void NanoscopeSampler::StartSampling(Thread* t, SampleMode sample_mode){
  sampling_thread_ = t;
  sample_mode_ = sample_mode;

#if defined(__ANDROID__)
  // Set up sampling
  install_sig_handler();
  set_up_timer();

  // Set up perf_event counters used to gather sampling data
  // All counters are in the same perf_event group, with the leader being the first counter
  // so that we can read all of them at the same time
  set_up_sample_counter(COUNTER_TYPE_MAJOR_PAGE_FAULTS, -1);
  set_up_sample_counter(COUNTER_TYPE_MINOR_PAGE_FAULTS, sample_fd_[0]);
  set_up_sample_counter(COUNTER_TYPE_CONTEXT_SWITCHES, sample_fd_[0]);

  // Enable allocation stats counter
  Runtime::Current()->SetStatsEnabled(true);

  // Starts all counters
  if(sample_mode_ == kSamplePerf){
    ioctl(perf_timer_fd_, PERF_EVENT_IOC_RESET, 0);
    ioctl(perf_timer_fd_, PERF_EVENT_IOC_REFRESH, 1);
  }
  ioctl(sample_fd_[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
  ioctl(sample_fd_[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
#endif
}

void NanoscopeSampler::StopSampling(){
#if defined(__ANDROID__)
  if(sample_mode_ == kSamplePerf){
    // Disable allocation stats counter
    Runtime::Current()->SetStatsEnabled(false);

    // Delete perf_event counter that acts as a timer
    ioctl(perf_timer_fd_, PERF_EVENT_IOC_DISABLE, 0);
    close(perf_timer_fd_);
    perf_timer_fd_ = 0;
    munmap(perf_timer_page_, 2 * PERF_PAGE_SIZE);
    perf_timer_page_ = NULL;

    // Delete perf_event counters used to gather sampling data
    ioctl(sample_fd_[0], PERF_EVENT_IOC_DISABLE, 0);
    for(int i = 0; i < COUNTER_TYPE_LIMIT; i++){
      close(sample_fd_[i]);
      sample_fd_[i] = 0;
    }
  } else if(sample_mode_ == kSampleCpu) {
    // Disable allocation stats counter
    Runtime::Current()->SetStatsEnabled(false);

    // Delete timer_settime timer
    timer_delete(timer_id_);

    // Delete perf_event counters used to gather sampling data
    ioctl(sample_fd_[0], PERF_EVENT_IOC_DISABLE, 0);
    for(int i = 0; i < COUNTER_TYPE_LIMIT; i++){
      close(sample_fd_[i]);
      sample_fd_[i] = 0;
    }
  }
#endif
}

}
