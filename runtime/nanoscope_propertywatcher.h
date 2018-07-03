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

// #define SIGTIMER (SIGRTMIN + 3)
#define SIGTIMER (SIGPROF)
// #define SIGTIMER (SIGIO)

namespace art {

static const unsigned long PERF_PAGE_SIZE = sysconf(_SC_PAGESIZE);

struct read_format {
  uint64_t nr;
  struct {
    uint64_t value;
    uint64_t id;
  } values[];
};

class RingBuffer {
  private:
    const char* _start;
    unsigned long _offset;

  public:
    RingBuffer(struct perf_event_mmap_page* page) {
        _start = (const char*)page + PERF_PAGE_SIZE;
    }

    struct perf_event_header* seek(uint64_t offset) {
        _offset = (unsigned long)offset & (PERF_PAGE_SIZE - 1);
        return (struct perf_event_header*)(_start + _offset);
    }

    uint64_t next() {
        _offset = (_offset + sizeof(uint64_t)) & (PERF_PAGE_SIZE - 1);
        return *(uint64_t*)(_start + _offset);
    }
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
class NanoscopePropertyWatcher {
 public:
  static void attach(std::string package_name) {
    Thread* to_trace = Thread::Current();
#if defined(__ANDROID__)
    LOG(INFO) << "nanoscope: attach to thread " << Thread::Current() -> GetTid() <<  ", " << gettid() << ": " << package_name << "\n";
#endif
    NanoscopePropertyWatcher* watcher = new NanoscopePropertyWatcher(to_trace, package_name);
    // traced = to_trace;
    watcher->watch();
  }

 private:
  // Thread* this_traced;
  static Thread* traced;
  Thread* to_trace;
  bool use_perf = true;
  const std::string package_name;
  const std::string watched_properties[3] = {"dev.nanoscope", "dev.arttracing", "arttracing"};
  const std::string output_dir = "/data/data/" + package_name + "/files";

  std::string output_path;
#if defined(__ANDROID__)
  int64_t interval = 1000000;  // 1 ms
  static int fd;                       // fd for timer
  // static uint64_t id;                  // event id for timer
  static struct perf_event_mmap_page* page;
  timer_t timerid;
  static int sample_fd[2];             // fd for samples
#endif

  explicit NanoscopePropertyWatcher(Thread* t, std::string _package_name) : to_trace(t), package_name(_package_name) {}

  void watch() {
    // refresh_state(Thread::Current());
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
      if (!output_path.empty()) return;

      std::stringstream ss(value);

      std::string _package_name;
      std::getline(ss, _package_name, ':');
      if (_package_name.compare(package_name) != 0) {
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
      if(timer_mode == "perf"){
        LOG(INFO) << "nanoscope: timer mode: " << timer_mode;
        use_perf = true;
      } else if (timer_mode == "cpu"){
        LOG(INFO) << "nanoscope: timer mode: " << timer_mode;
        use_perf = false;
      } else {
        LOG(ERROR) << "nanoscope: unsupported timer mode: " << timer_mode;
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
  static void signal_handler(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED);
  static void signal_handler_timer(int sigo ATTRIBUTE_UNUSED, siginfo_t *siginfo ATTRIBUTE_UNUSED, void *ucontext ATTRIBUTE_UNUSED);
  void install_sig_handler();
  void set_up_counter(int sample_index, int groupfd, uint32_t type, uint64_t config);
#endif
  void setup_timer();

  void start_tracing(Thread* self, std::string _output_path) {
    // nanoscope tracing
    traced = to_trace;
    output_path = _output_path;
    remove(output_path.c_str());
    Locks::mutator_lock_->SharedLock(self);
    traced->StartTracing();
    Locks::mutator_lock_->SharedUnlock(self);
#if defined(__ANDROID__)
    setup_timer();
    set_up_counter(0, -1, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ);
    set_up_counter(1, sample_fd[0], PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN);
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
    ioctl(sample_fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
#endif
  }

  void stop_tracing(Thread* self) {
    if (output_path.empty()) {
      LOG(ERROR) << "nanoscope: No output path found.";
      return;
    }
#if defined(__ANDROID__)
    if(use_perf){
      ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
      close(fd);
      fd = 0;
      munmap(page, 2 * PERF_PAGE_SIZE);
      page = NULL;
      ioctl(sample_fd[0], PERF_EVENT_IOC_DISABLE, 0);
      for(int i = 0; i < 2; i++){
        close(sample_fd[i]);
        sample_fd[i] = 0;
      }
    } else {
      timer_delete(timerid);
    }
#endif
    Locks::mutator_lock_->SharedLock(self);
    traced->StopTracing(output_path);
    Locks::mutator_lock_->SharedUnlock(self);
    output_path = "";
  }
};

}  // namespace art

#endif  // ART_RUNTIME_NANOSCOPE_PROPERTYWATCHER_H_
