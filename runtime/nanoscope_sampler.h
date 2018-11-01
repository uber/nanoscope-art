/*
 * Copyright (C) 2018 Uber Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_NANOSCOPE_SAMPLER_H_
#define ART_RUNTIME_NANOSCOPE_SAMPLER_H_

#include "runtime.h"
#include <sys/mman.h>
#include <sys/syscall.h>

#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#endif

#define SIGTIMER (SIGPROF)

namespace art{
// Right now we have 3 counters: # of major page faults, # of minor page
// faults, # of context switches.
enum CounterType {
  COUNTER_TYPE_MAJOR_PAGE_FAULTS = 0,
  COUNTER_TYPE_MINOR_PAGE_FAULTS,
  COUNTER_TYPE_CONTEXT_SWITCHES,
  // ===============================
  COUNTER_TYPE_LIMIT            // total number of counters
};

// Data structures defined to read perf_event counters in sighandler
struct read_format {
  uint64_t nr;
  struct {
    uint64_t value;
    uint64_t id;
  } values[COUNTER_TYPE_LIMIT];
};

const unsigned long PERF_PAGE_SIZE = sysconf(_SC_PAGESIZE);

enum SampleMode {
  kSampleDisabled,              // Sampling disabled
  kSamplePerf,                  // perf_timer mode, use perf_event as sampling timer
  kSampleCpu                   // cpu_timer mpde, use timer_settime as sampling timer
};

class NanoscopeSampler{
public:
  static void StartSampling(Thread* t, SampleMode sample_mode);
  static void StopSampling();

private:
  // Thread with sampling enabled. Use static field so we can access it in signal handler
  static Thread* sampling_thread_;
  // Use perf_event to generate sampling signal (perf_timer mode) or use timer_settime (cpu_timer mode) or sampling disabled
  static SampleMode sample_mode_;

#if defined(__ANDROID__)
  // Sampling interval in ns
  static int64_t sample_interval_;
  // fd of perf_event counter that acts as a timer. Only in perf_timer mode
  static int perf_timer_fd_;
  // mmap-ed page used by perf_event counter that acts as a timer. Only in perf_timer mode
  static struct perf_event_mmap_page* perf_timer_page_;

  // id of timer_settime. Only in cpu_timer mode
  static timer_t timer_id_;

  // fds of perf_event counters used to gather sampling data.
  static int sample_fd_[COUNTER_TYPE_LIMIT];
  // Set up signal handler for SIGPROF
  static void signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED);
  // Install the correct sighandler
  static void install_sig_handler();

  // Set up perf_event counters used to gather sampling data
  static void set_up_sample_counter(CounterType counter_type, int groupfd);
#endif
  // Set up the sampling signal timer based on the timer mode
  static void set_up_timer();
};

}

#endif  // ART_RUNTIME_NANOSCOPE_SAMPLER_H_
