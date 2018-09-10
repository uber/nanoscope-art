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

#ifndef ART_RUNTIME_NANOSCOPE_PROPERTYWATCHER_H_
#define ART_RUNTIME_NANOSCOPE_PROPERTYWATCHER_H_

#include "base/mutex.h"
#include "thread.h"
#include "utils.h"
#include "runtime.h"
#include <sstream>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <cutils/process_name.h>
#include <sys/mman.h>
#include <sys/syscall.h>



#if defined(__ANDROID__)
// Need this next line to get around a check in "sys/_system_properties.h". These APIs are definitely not meant for
// external use, but it's the only way to observe system property changes.
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>
#endif

#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#endif

#define SIGTIMER (SIGPROF)
// Total number of perf_event_counter to gather sampling data, change this when
// add more counters. Right now we have 3: # of major page faults, # of minor page
// faults, # of context switches.
#define NUM_COUNTER 3

namespace art {
static const unsigned long PERF_PAGE_SIZE = sysconf(_SC_PAGESIZE);
// Data structures defined to read perf_event counters in sighandler
struct read_format {
  uint64_t nr;
  struct {
    uint64_t value;
    uint64_t id;
  } values[];
};

// This class enables starting and stopping tracing by setting the "dev.nanoscope" system property. For example, the
// command below starts tracing the com.example process on the thread passed to NanoScopePropertyWatcher::attach_to:
//
//     $ adb shell setprop dev.nanoscope com.example:data.txt
//
// The following command stops tracing and flushes the trace data to /data/data/com.example/files/data.txt:
//
//     $ adb shell setprop dev.nanoscope \'\'
//
// It's valid to set this property before the process is started. In this case, the tracing will be triggered on app start.

// Currently provides two ways of generating sampling signals:
// perf_timer mode: uses perf_event_open to set up counter that sends overflow signals, sampling interval is based on wall clock time
// cpu_timer mode: uses timer_settime to wake up and send signals periodically, samping interval is based on cpu time
// Enabling sampling by selecting one of the sampling mode, for example, the commands below enables sampling in perf_timer mode
//
//     $ adb shell setprop dev.nanoscope com.example:data.txt:perf_timer
//
// The commands below enables sampling in cpu_timer mode:
//
//     $ adb shell setprop dev.nanoscope com.example:data.txt:cpu_timer
//
class NanoscopePropertyWatcher {
 public:
  static void attach(std::string package_name) {
    Thread* watching_thread_ = Thread::Current();
    NanoscopePropertyWatcher* watcher = new NanoscopePropertyWatcher(watching_thread_, package_name);
    watcher->watch();
  }

  enum SampleMode {
    kSampleDisabled,              // Sampling disabled
    kSamplePerf,                  // perf_timer mode, use perf_event as sampling timer
    kSampleCpu                   // cpu_timer mpde, use timer_settime as sampling timer
  };

 private:
  const std::string package_name_;
  const std::string watched_properties_[3] = {"dev.nanoscope", "dev.arttracing", "arttracing"};
  const std::string output_dir_ = "/data/data/" + package_name_ + "/files";
  std::string output_path_;

  // Thread with tracing enabled. Use static field so we can access it in signal handler
  static Thread* tracing_thread_;
  // Use perf_event to generate sampling signal (perf_timer mode) or use timer_settime (cpu_timer mode) or sampling disabled
  static SampleMode sample_mode_;
  // Thread current nanoscope watcher thread is monitoring
  Thread* watching_thread_;

#if defined(__ANDROID__)
  // Sampling interval in ns
  int64_t sample_interval_ = 1000000;
  // fd of perf_event counter that acts as a timer. Only in perf_timer mode
  static int perf_timer_fd_;
  // mmap-ed page used by perf_event counter that acts as a timer. Only in perf_timer mode
  static struct perf_event_mmap_page* perf_timer_page_;

  // id of timer_settime. Only in cpu_timer mode
  timer_t timer_id_;

  // fds of perf_event counters used to gather sampling data.
  static int sample_fd_[NUM_COUNTER];

  // Set up signal handler for SIGPROF
  static void signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED);
  // Install the correct sighandler
  void install_sig_handler();

  // Set up perf_event counters used to gather sampling data
  void set_up_sample_counter(int counter_index, int groupfd, uint32_t type, uint64_t config);
#endif
  // Set up the sampling signal timer based on the timer mode
  void set_up_timer();

  explicit NanoscopePropertyWatcher(Thread* t, std::string _package_name) : package_name_(_package_name), watching_thread_(t) {}

  void watch() {
    refresh_state(Thread::Current());
    std::string thread_name = "nanoscope-propertywatcher:" + package_name_;

    NanoscopePropertyWatcher* watcher = this;
    new std::thread([watcher, thread_name]() {
      Thread* self = Thread::Attach(thread_name.c_str(), false, nullptr, false);
#if defined(__ANDROID__)
      unsigned int serial = 0;
      while (1) {
        serial = __system_property_wait_any(serial);
        watcher->refresh_state(self);
      }
#else
      (void) self;
#endif
    });
  }

  void refresh_state(Thread* self) {
    std::string value = get_system_property_value();

    if (value.empty()) {
      if (output_path_.empty()) return;

      stop_tracing(self);
    } else {
      if (!output_path_.empty()) return;

      std::stringstream ss(value);

      std::string _package_name;
      std::getline(ss, _package_name, ':');
      if (_package_name.compare(package_name_) != 0) {
        return;
      }

      std::string output_filename;
      std::getline(ss, output_filename, ':');
      if (output_filename.empty()) {
        LOG(INFO) << "nanoscope: Failed to parse output filename: " << value;
        return;
      }

      std::string timer_mode;
      std::getline(ss, timer_mode);

      if(timer_mode == "perf_timer"){
        LOG(INFO) << "nanoscope: sampling enabled, timer mode: " << timer_mode;
        sample_mode_ = kSamplePerf;
      } else if (timer_mode == "cpu_timer"){
        LOG(INFO) << "nanoscope: sampling enabled, timer mode: " << timer_mode;
        sample_mode_ = kSampleCpu;
      } else {
        LOG(INFO) << "nanoscope: sampling disabled";
        sample_mode_ = kSampleDisabled;
      }

      start_tracing(self, output_dir_ + "/" + output_filename);
    }
  }

  std::string get_system_property_value() {
    char* buffer = new char[1028];
#if defined(__ANDROID__)
    for (std::string watched_property : watched_properties_) {
      int length = __system_property_get(watched_property.c_str(), buffer);
      if (length > 0) break;
    }
#endif
    return std::string(buffer);
  }

  void start_tracing(Thread* self, std::string output_path) {
    tracing_thread_ = watching_thread_;
    output_path_ = output_path;
    remove(output_path_.c_str());

    // Enable allocation stats counter
    Runtime::Current()->SetStatsEnabled(true);

    // Start Nanoscope tracing
    Locks::mutator_lock_->SharedLock(self);
    tracing_thread_->StartTracing();
    Locks::mutator_lock_->SharedUnlock(self);

#if defined(__ANDROID__)
    // Set up sampling
    if(sample_mode_ != kSampleDisabled){
      install_sig_handler();
      set_up_timer();

      // Set up perf_event counters used to gather sampling data
      // All counters are in the same perf_event graoup, with the leader being the first counter
      // so that we can read all of them at the same time
      set_up_sample_counter(0, -1, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ);
      set_up_sample_counter(1, sample_fd_[0], PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN);
      set_up_sample_counter(2, sample_fd_[0], PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES);
      ioctl(perf_timer_fd_, PERF_EVENT_IOC_RESET, 0);
      ioctl(perf_timer_fd_, PERF_EVENT_IOC_REFRESH, 1);
      ioctl(sample_fd_[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
      ioctl(sample_fd_[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
#endif
  }

  void stop_tracing(Thread* self) {
    if (output_path_.empty()) {
      LOG(ERROR) << "nanoscope: No output path found.";
      return;
    }
    // Disable allocation stats counter
    Runtime::Current()->SetStatsEnabled(false);

    // Stop sampling
#if defined(__ANDROID__)
    if(sample_mode_ == kSamplePerf){
      // Delete perf_event counter that acts as a timer
      ioctl(perf_timer_fd_, PERF_EVENT_IOC_DISABLE, 0);
      close(perf_timer_fd_);
      perf_timer_fd_ = 0;
      munmap(perf_timer_page_, 2 * PERF_PAGE_SIZE);
      perf_timer_page_ = NULL;

      // Delete perf_event counters used to gather sampling data
      ioctl(sample_fd_[0], PERF_EVENT_IOC_DISABLE, 0);
      for(int i = 0; i < NUM_COUNTER; i++){
        close(sample_fd_[i]);
        sample_fd_[i] = 0;
      }
    } else if(sample_mode_ == kSampleCpu) {
      // Delete timer_settime timer
      timer_delete(timer_id_);

      // Delete perf_event counters used to gather sampling data
      ioctl(sample_fd_[0], PERF_EVENT_IOC_DISABLE, 0);
      for(int i = 0; i < NUM_COUNTER; i++){
        close(sample_fd_[i]);
        sample_fd_[i] = 0;
      }
    }
#endif

    // Stop tracing
    Locks::mutator_lock_->SharedLock(self);
    tracing_thread_->StopTracing(output_path_);
    Locks::mutator_lock_->SharedUnlock(self);
    output_path_ = "";
  }
};

}  // namespace art

#endif  // ART_RUNTIME_NANOSCOPE_PROPERTYWATCHER_H_
