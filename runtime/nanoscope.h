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

#ifndef ART_RUNTIME_NANOSCOPE_H_
#define ART_RUNTIME_NANOSCOPE_H_

#include "thread.h"

#define NANO_TRACE_SCOPE_FROM_STRING(thread_self, name_string) FromStringScopedArtTrace ___nano_tracer(thread_self, reinterpret_cast<const char *>(name_string))
#define NANO_TRACE_SCOPE_FROM_STRING_AND_META(thread_self, name_string, meta) FromStringAndMetaScopedArtTrace ___nano_tracer_with_meta(thread_self, reinterpret_cast<const char *>(name_string), reinterpret_cast<const char *>(meta))

class FromStringScopedArtTrace {
 private:
  art::Thread* thread;

 public:
  ALWAYS_INLINE FromStringScopedArtTrace(art::Thread* t, const char* functionName) {
    thread = t;
    thread->TraceStart(functionName);
  }

  ALWAYS_INLINE ~FromStringScopedArtTrace() {
    thread->TraceEnd();
  }
};

class FromStringAndMetaScopedArtTrace {
 private:
  art::Thread* thread;

 public:
  ALWAYS_INLINE FromStringAndMetaScopedArtTrace(art::Thread* t, const char* functionName, const char* meta) {
    thread = t;
    thread->TraceStart(functionName, meta);
  }

  ALWAYS_INLINE ~FromStringAndMetaScopedArtTrace() {
    thread->TraceEnd();
  }
};

#endif  // ART_RUNTIME_NANOSCOPE_H_
