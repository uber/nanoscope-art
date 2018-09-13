#include "monitor_event.h"
#include <iostream>

namespace art{
std::ostream& operator<<(std::ostream& os, const NanoMonitorEvent& event)
{
  switch(event.event_type_){
    case NanoMonitorEvent::kAcquireThin:
    case NanoMonitorEvent::kAcquireFat:
      os << std::fixed << "{ \"pid\": 1, \"tid\":" << event.tid_ << ", \"ts\":" << event.ts_/1000.0
      << ",\"dur\":\"" << event.duration_/1000.0  << "\", \"ph\":\"X\", \"name\":\"contention\","
      << " \"args\":{ \"obj\":\"" << std::hex << event.obj_ << std::dec << "\", \"type\":\"" << event.event_type_
      << " \", \"location\":\"" << event.source_location_ << "\" } }";
      break;
    case NanoMonitorEvent::kInflateHash:
    case NanoMonitorEvent::kInflateRecursion:
    case NanoMonitorEvent::kInflateContention:
    case NanoMonitorEvent::kInflateWait:
      os << std::fixed << "{ \"pid\": 1, \"tid\":" << event.tid_ << ", \"ts\":" << event.ts_/1000.0
      << ", \"ph\":\"i\", \"s\":\"t\", \"name\":\""  << std::hex << event.obj_ << std::dec << "\", \"args\":{ \"type\":\"" << event.event_type_ << "\" } }";
      break;
    case NanoMonitorEvent::kMonitorWait:
      os << std::fixed << "{ \"pid\": 1, \"tid\":" << event.tid_ << ", \"ts\":" << event.ts_/1000.0
      << ",\"dur\":\"" << event.duration_/1000.0  << "\", \"ph\":\"X\", \"name\":\"wait\","
      << " \"args\":{ \"obj\":\"" << std::hex << event.obj_ << std::dec
      << "\", \"location\":\"" << event.source_location_ << "\" } }";
      break;
    default:
      LOG(ERROR) << "nanoscope: Invalid monitor event type";
      break;
  }
  return os;
}

}
