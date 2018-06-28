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

#if defined(__ANDROID__)
// Need this next line to get around a check in "sys/_system_properties.h". These APIs are definitely not meant for
// external use, but it's the only way to observe system property changes.
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>
#endif

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
    Thread* traced = Thread::Current();
    NanoscopePropertyWatcher* watcher = new NanoscopePropertyWatcher(traced, package_name);
    watcher->watch();
  }

 private:
  Thread* const traced;
  const std::string package_name;
  const std::string watched_properties[3] = {"dev.nanoscope", "dev.arttracing", "arttracing"};
  const std::string output_dir = "/data/data/" + package_name + "/files";

  std::string output_path;

  explicit NanoscopePropertyWatcher(Thread* _traced, std::string _package_name) : traced(_traced), package_name(_package_name) {}

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

  void start_tracing(Thread* self, std::string _output_path) {
    output_path = _output_path;
    remove(output_path.c_str());

    Locks::mutator_lock_->SharedLock(self);
    traced->StartTracing();
    Locks::mutator_lock_->SharedUnlock(self);
  }

  void stop_tracing(Thread* self) {
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
