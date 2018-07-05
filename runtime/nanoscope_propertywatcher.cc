#include "nanoscope_propertywatcher.h"

art::Thread* art::NanoscopePropertyWatcher::traced = NULL;
#if defined(__ANDROID__)
int art::NanoscopePropertyWatcher::fd = -1;
struct perf_event_mmap_page* art::NanoscopePropertyWatcher::page = NULL;
#endif
