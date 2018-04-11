/*
 * Copyright (c) 2018 Uber Technologies, Inc.
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

#ifndef ART_RUNTIME_TRACE_BLACKLIST_H_
#define ART_RUNTIME_TRACE_BLACKLIST_H_

#include <fstream>

namespace art {
namespace tracing {
  std::set<std::string> blacklist;  // Set of methods we shouldn't trace. String representation generated with PrettyMethod().

  // Use this instead of LOG to avoid lock issues. Unclear why LOG causes problems.
  void log(std::string message) {
    LogMessage::LogLine(__FILE__, __LINE__, LogSeverity::WARNING, message.c_str());
  }

  bool initialize_blacklist() {
    std::ifstream blacklist_file("/system/trace_blacklist");
    std::string line;
    if (blacklist_file.is_open()) {
      while (std::getline(blacklist_file, line)) {
        blacklist.insert(line);
      }
      return true;
    } else {
      log("Failed to open trace blacklist file: " + std::string(strerror(errno)));
      return false;
    }
  }

  bool blacklist_initialized = initialize_blacklist();
}  // namespace tracing
}  // namespace art

#endif  // ART_RUNTIME_TRACE_BLACKLIST_H_
