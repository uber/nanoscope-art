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
#include <sstream>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <cutils/process_name.h>
#include <sys/mman.h>

#if defined(__ANDROID__)
// Need this next line to get around a check in "sys/_system_properties.h". These APIs are definitely not meant for
// external use, but it's the only way to observe system property changes.
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>
#endif

#if defined(__ANDROID__)
#include <linux/perf_event.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
static int perf_event_open(const perf_event_attr& attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags) {  // NOLINT
  return syscall(__NR_perf_event_open, &attr, pid, cpu, group_fd, flags);
}
#endif

#define SIGTIMER (SIGRTMIN + 3)

namespace art {

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
class NanoscopePropertyWatcher {
 public:
  static void attach(std::string package_name) {
    Thread* to_trace = Thread::Current();
    NanoscopePropertyWatcher* watcher = new NanoscopePropertyWatcher(package_name);
    traced = to_trace;
    watcher->watch();
  }

 private:
  static Thread* traced;

  const std::string package_name;
  const std::string watched_properties[3] = {"dev.nanoscope", "dev.arttracing", "arttracing"};
  const std::string output_dir = "/data/data/" + package_name + "/files";

  std::string output_path;
#if defined(__ANDROID__)
  static int fd;                       // perf_fd
#endif

  explicit NanoscopePropertyWatcher(std::string _package_name) : package_name(_package_name) {}

  void watch() {
    refresh_state(Thread::Current());
    std::string thread_name = "nanoscope-propertywatcher:" + package_name;

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
      if (output_path.empty()) return;

      stop_tracing(self);
    } else {
      std::stringstream ss(value);

      std::string _package_name;
      std::getline(ss, _package_name, ':');
      if (_package_name.compare(package_name) != 0) {
        return;
      }

      std::string output_filename;
      std::getline(ss, output_filename);
      if (output_filename.empty()) {
        LOG(INFO) << "nanoscope: Failed to parse output filename: " << value;
        return;
      }

      start_tracing(self, output_dir + "/" + output_filename);
    }
  }

  std::string get_system_property_value() {
    char* buffer = new char[1028];
#if defined(__ANDROID__)
    for (std::string watched_property : watched_properties) {
      int length = __system_property_get(watched_property.c_str(), buffer);
      if (length > 0) break;
    }
#endif
    return std::string(buffer);
  }

#if defined(__ANDROID__)
  static void signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo, void *ucontext ATTRIBUTE_UNUSED) {
    if (fd != siginfo -> si_fd) {
      LOG(ERROR) << "nanoscope: Sanity check fails: perf fd should be the same";
    }
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    // LOG(INFO) << "nanoscope: entering sig handler";
    traced->TimerHandler();
    ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
  }

  void install_sig_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGTIMER, &sa, NULL) < 0) {
      LOG(ERROR) << "nanoscope: Error setting up signal handler.";
      return;
    }
    LOG(INFO) << "nanoscope: set up sig handler for SIGTIMER";
  }
#endif

  void start_tracing(Thread* self, std::string _output_path) {
    // nanoscope tracing
    output_path = _output_path;
    remove(output_path.c_str());
    Locks::mutator_lock_->SharedLock(self);
    traced->StartTracing();
    Locks::mutator_lock_->SharedUnlock(self);

#if defined(__ANDROID__)
    install_sig_handler();
    int64_t interval = 10000000;  // 10 ms
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
    // pe.exclude_kernel = 1;
    // pe.exclude_hv = 1;
    pe.wakeup_events = 1;
    fd = perf_event_open(pe, traced->GetTid(), -1, -1, 0);
    if (fd < 0) {
       LOG(ERROR) << "nanoscope: Fail to open perf event file.";
       return;
    }

    void *blargh;

    blargh=mmap(NULL, (1+1)*4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK | O_ASYNC);
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
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
#endif
  }

  void stop_tracing(Thread* self) {
#if defined(__ANDROID__)
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    close(fd);
#endif
    if (output_path.empty()) {
      LOG(ERROR) << "nanoscope: No output path found.";
      return;
    }
    Locks::mutator_lock_->SharedLock(self);
    traced->StopTracing(output_path);
    Locks::mutator_lock_->SharedUnlock(self);

    output_path = "";

  }
};

}  // namespace art

#endif  // ART_RUNTIME_NANOSCOPE_PROPERTYWATCHER_H_
