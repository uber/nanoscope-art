/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_RUNTIME_ARTTRACING_PROPERTYWATCHER_H_
#define ART_RUNTIME_ARTTRACING_PROPERTYWATCHER_H_

#include "base/mutex.h"
#include "thread.h"
#include "utils.h"
#include <sstream>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <cutils/process_name.h>

#if defined(__linux__)
// Need this next line to get around a check in "sys/_system_properties.h". These APIs are definitely not meant for
// external use, but it's the only way to observe system property changes.
#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>
#include <sys/system_properties.h>
#endif

namespace art {

static bool starts_with(std::string str, std::string prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}

// We start this watcher in the Runtime::InitNonZygoteOrPostFork which is called from ZygoteHooks.postForkChild.
// Unfortunately, the process name is set to the correct package name shortly after this hook is called. To account
// for this, we just spin in the thread until the package name is updated.
std::string wait_for_package_name() {
  std::string zygote = "zygote";
  std::string preinitialized = "<pre-initialized>";
  std::string package_name;
  int count = 0;
  int limit = 100;
  for (package_name = std::string(get_process_name());
    starts_with(package_name, zygote) || package_name.compare(preinitialized) == 0;
    package_name = std::string(get_process_name())) {
    usleep(1000 * 100);  // Sleep for 100ms
    if (count++ > limit) {
      LOG(ERROR) << "arttracing: Could not retrieve package name.";
      return "<unknown>";
    }
  }
  return package_name;
}

// This class enables starting and stopping ART Tracing by setting the "arttracing" system property. For example, the
// command below starts tracing the com.example process on the thread passed to ARTTracingPropertyWatcher::attach_to:
//
//     $ adb shell setprop arttracing com.example:data.txt
//
// The following command stops tracing and flushes the trace data to /data/data/com.example/files/data.txt:
//
//     $ adb shell setprop arttracing \'\'
//
// It's valid to set this property before the process is started. In this case, the tracing will be triggered on app start.
class ARTTracingPropertyWatcher {
 public:
  static void attach_to(Thread* traced) {
    new std::thread([traced]() {
      std::string _package_name = wait_for_package_name();

      std::string thread_name = "arttracing-propertywatcher:" + _package_name;
      Thread* _self = Thread::Attach(thread_name.c_str(), false, nullptr, false);

      ARTTracingPropertyWatcher(_self, traced, _package_name).watch();
    });
  }

 private:
  Thread* const self;
  Thread* const traced;
  const std::string package_name;

  const std::string watched_property = "arttracing";
  const std::string output_dir = "/data/data/" + package_name + "/files";

  std::string output_path;

  ARTTracingPropertyWatcher(Thread* _self, Thread* _traced, std::string _package_name)
    : self(_self), traced(_traced), package_name(_package_name) {}

  void watch() {
    watch_system_property(watched_property, &ARTTracingPropertyWatcher::on_property_changed);
  }

  void on_property_changed(std::string value) {
    if (value.empty() && !output_path.empty()) {
      stop_tracing();
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
        LOG(INFO) << "arttracing: Failed to parse output filename: " << value;
        return;
      }

      start_tracing(output_dir + "/" + output_filename);
    }
  }

  void start_tracing(std::string _output_path) {
    output_path = _output_path;
    remove(output_path.c_str());

    Locks::mutator_lock_->SharedLock(self);
    traced->StartTracing();
    Locks::mutator_lock_->SharedUnlock(self);
  }

  void stop_tracing() {
    if (output_path.empty()) {
      LOG(ERROR) << "arttracing: No output path found.";
      return;
    }

    Locks::mutator_lock_->SharedLock(self);
    traced->StopTracing(output_path);
    Locks::mutator_lock_->SharedUnlock(self);
  }

  // Watch for changes to the given system property name. Call on_changed with new values.
  std::string watch_system_property(std::string name, void (ARTTracingPropertyWatcher::*on_changed)(std::string)) {
#if defined(__linux__)
    std::string previous;
    char* buffer = new char[1028];
    unsigned int serial = 0;
    while (1) {
      serial = __system_property_wait_any(serial);
      __system_property_get(name.c_str(), buffer);
      std::string value = std::string(buffer);
      if (value.compare(previous) != 0) {
        (*this.*on_changed)(value);
      }
      previous = value;
    }
#else
    (void) name;
    (void) on_changed;
    return "";
#endif
  }
};

}  // namespace art

#endif  // ART_RUNTIME_ARTTRACING_PROPERTYWATCHER_H_
