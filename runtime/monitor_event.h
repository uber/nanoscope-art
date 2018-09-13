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

#ifndef ART_RUNTIME_MONITOR_EVENTS_H_
#define ART_RUNTIME_MONITOR_EVENTS_H_

#include <stdio.h>
#include <sys/types.h>
#include "utils.h"

namespace art {

class NanoMonitorEvent {
public:
  enum NanoMonitorEventType {
    kAcquireThin = 1,
    kAcquireFat,
    kInflateContention,
    kInflateRecursion,
    kInflateHash,
    kInflateWait,
    kMonitorWait
  };

  NanoMonitorEvent(NanoMonitorEventType event_type, pid_t tid, uint64_t ts, mirror::Object* obj, uint64_t duration = 0, std::string source_location = "NA")
  : event_type_(event_type), tid_(tid), ts_(ts), obj_(obj), duration_(duration), source_location_(source_location) {}

  friend std::ostream& operator<<(std::ostream &os, const  NanoMonitorEvent& n);

private:
  NanoMonitorEventType event_type_;
  pid_t     tid_;
  uint64_t  ts_;
  mirror::Object*  obj_;
  uint64_t  duration_;
  std::string  source_location_;
};

}  // namespace art

#endif  // ART_RUNTIME_MONITOR_EVENTS_H_
