/*
 * cyg-profile.cc - CygProfiler runtime functions.
 *
 * Michal Ludvig <michal@logix.cz>
 * http://www.logix.cz/michal/devel
 *
 * cyg-profile.cc
 * - Compile your program with -finstrument-functions and link
 *   together with this code.
 * - Logging is enabled as soon as your program calls
 *   cygprofile_enable() and disabled with cygprofile_disable().
 * - Before logging was enabled you can change the name
 *   of a logfile by calling cygprofile_setfilename().
 *
 * This implementation has been modified in the following ways:
 *   1) Upon profile enabling, the virtual address is logged as the
 *      first line to assist in symbolizing logged virutal addresses.
 *   2) Only function entry for the first time a particular function
 *      is entered is logged.  This is for profiling for code layout.
 *   3) The implementation is made thread-safe.
 *   4) The implementation logs the timestamp (seconds and millisecs).
 *   5) The implementation adds function cygprofile_start(filename) which
 *      calls cyg_setfilename and enables profiling and cygprofile_end()
 *      to guarantee log buffer is flushed and logfile is closed.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <set>

#include "cyg-profile.h"

namespace cygprofile {

const int kMaxFileNameSizeSize = 100;
#ifdef __ANDROID__
static const char kDefaultFileName[] = "/sdcard/cyglog.%d";
#else
const char* kDefaultFileName = "cyglog.%d";
#endif
const int kMaxLineSize = 512;

static FILE* gLogFile = NULL;
static volatile bool gCygProfileEnabled = false;
static char gCygProfileFileName[kMaxFileNameSizeSize + 1];

// Keeps track of all functions that have been logged.
static std::set<void*>* gFunctionsCalled = NULL;

#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#  ifdef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#    define PTHREAD_RECURSIVE_MUTEX_INITIALIZER PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#  else
#    error MISSING PTHREAD_RECURSIVE_MUTEX_INITIALIZER DEFINITION!
#  endif
#endif

// Ensure thread safety (see __cyg_profile_func_enter)
static pthread_mutex_t gMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static unsigned int gDepth = 0;

#ifdef __cplusplus
extern "C" {
#endif

  FILE* cygprofile_openlogfile(const char* filename)
      __attribute__((no_instrument_function));
  void cygprofile_closelogfile(void)
      __attribute__((no_instrument_function));

  // Note that these are linked internally by the compiler.
  // Don't call them directly!
  void __cyg_profile_func_enter(void* this_fn, void* call_site)
      __attribute__((no_instrument_function));
  void __cyg_profile_func_exit(void* this_fn, void* call_site)
      __attribute__((no_instrument_function));

#ifdef __cplusplus
};
#endif

// Called internally by instrumentation inserted by the compiler upon entering
// a function.  Instrumentation is inserted by the compiler for all functions
// whose source is compiled with the -finstrument-functions CFLAG.  The code
// logs the call along with the timestamp, process id and thread id.
void __cyg_profile_func_enter(void* this_fn, void* call_site) {
  struct timeval timestamp;
  long mtime;
  time_t seconds;
  pthread_mutex_lock(&gMutex);
  // avoid recursive calls
  if (gDepth == 0) {
    gDepth += 1;
    if (!gCygProfileEnabled) {
      cygprofile_enable();
    }
    if (gCygProfileEnabled && gLogFile) {
      // gFunctionsCalled is initialized in cygprofile_enable()
      if (gFunctionsCalled && gFunctionsCalled->find(this_fn) ==
          gFunctionsCalled->end()) {
        gettimeofday(&timestamp, NULL);
        seconds = time(NULL);
        mtime = timestamp.tv_usec;
        fprintf(gLogFile, "%ld %ld\t%d:%ld\t%p\n",
                seconds, mtime, getpid(), pthread_self(), this_fn);
        fflush(gLogFile);
        gFunctionsCalled->insert(this_fn);
      }
    }
    gDepth -= 1;
  }
  pthread_mutex_unlock(&gMutex);
}

// Called internally by instrumentation inserted by the compiler upon exiting
// a function.  Instrumentation is inserted by the compiler for all functions
// whose source is compiled with the -finstrument-functions CFLAG.  The exit
// of a function is ignored because only enter is useful for order profiling.
void __cyg_profile_func_exit(void* this_fn, void* call_site) {
  // Do not do anything on function exit.
}

// Enables profiling and writes the first lines to the log file.  The first
// line(s) report any code segments that are currently mapped in memory by
// reading the /proc/self/maps file and searching for "r-xp" access permission.
// The address of these sections are later used to symbolize the logged
// addresses of called functions.  The format of the /proc/self/maps file is:
//   00008000-0001f000 r-xp 00000000 b3:01 246        /system/bin/toolbox
//   0001f000-00021000 rw-p 00017000 b3:01 246        /system/bin/toolbox
//   00021000-00029000 rw-p 00000000 00:00 0          [heap]
//   40007000-40015000 r-xp 00000000 b3:01 574        /system/lib/libcutils.so
//   40015000-40016000 rw-p 0000e000 b3:01 574        /system/lib/libcutils.so
//   40016000-40025000 rw-p 00000000 00:00 0
//   4002c000-4002f000 r-xp 00000000 b3:01 613        /system/lib/liblog.so
//   4002f000-40030000 rw-p 00003000 b3:01 613        /system/lib/liblog.so
// It also prints column headers and START on a new line to indicate that
// logging is starting.  If the name of the log file has not yet been set, it
// sets the name of the log file to the default name (i.e. kDefaultFileName).
void cygprofile_enable(void) {
  char line[kMaxLineSize];
  void (*this_fn)(void) = &cygprofile_enable;
  char* p;
  char* q;
  std::ifstream mapsfile;

  // Return if profiling is already enabled
  if (gCygProfileEnabled)
    return;

  gFunctionsCalled = new std::set<void*>();

  if (!gCygProfileFileName[0])
    cygprofile_setfilename(kDefaultFileName);
  if (!cygprofile_openlogfile(gCygProfileFileName))
    return;

  mapsfile.open("/proc/self/maps");
  if (mapsfile.good()) {
    while (mapsfile.getline(line, kMaxLineSize)) {
      std::string str_line = std::string(line);
      int permindex = str_line.find("r-xp");
      if (permindex != std::string::npos) {
        int dashindex = str_line.find("-");
        int spaceindex = str_line.find(" ");
        void* start = reinterpret_cast<void*>
            (strtol((str_line.substr(0, dashindex)).c_str(),
                    &p, 16));
        if (*p != 0)
          fprintf(gLogFile,
                  "cyg-profile.cc: ERROR: could not determine start: %s.\n",
                  (str_line.substr(0, dashindex)).c_str());
        void* end = reinterpret_cast<void*>
            (strtol((str_line.substr(dashindex + 1,
                                     spaceindex - dashindex - 1)).c_str(),
                    &q, 16));
        if (*q != 0)
          fprintf(gLogFile,
                  "cyg-profile.cc: ERROR: could not determine end: %s.\n",
                  (str_line.substr(dashindex + 1,
                                   spaceindex - dashindex - 1)).c_str());
        if (this_fn >= start && this_fn < end)
          fprintf(gLogFile, "%s\n", str_line.c_str());
      }
    }
  } else {
    fprintf(gLogFile, "cyg-profile.cc: ERROR: Can't open maps file.");
  }
  mapsfile.close();
  fprintf(gLogFile, "secs       msecs\tpid:threadid\tfunc\nSTART\n");
  gCygProfileEnabled = true;
}

// Disables profiling.
void cygprofile_disable(void) {
  gCygProfileEnabled = false;
  pthread_mutex_lock(&gMutex);
  delete gFunctionsCalled;
  gFunctionsCalled = NULL;
  pthread_mutex_unlock(&gMutex);
}

// Returns true if profiling is enabled.
bool cygprofile_isenabled(void) {
  return gCygProfileEnabled;
}

// Sets the name of the log file.  Returns -1 without setting the name if
// profiling has already been enabled, because enabling profiling before
// setting the file name will set the name of the log file to the default
// name.  If the file name is greater than the maximum size (kMaxFileNameSizeSize)
// returns -2 without setting the file name.
int cygprofile_setfilename(const char* filename) {
  if (cygprofile_isenabled())
    return -1;

  if (strlen(filename) > kMaxFileNameSizeSize)
    return -2;

  const char* ptr = strstr(filename, "%d");
  if (ptr) {
    size_t len;
    len = ptr - filename;
    snprintf(gCygProfileFileName, len+1, "%s", filename);
    snprintf(&gCygProfileFileName[len], kMaxFileNameSizeSize - len,
             "%d", getpid());
    len = strlen(gCygProfileFileName);
    snprintf(&gCygProfileFileName[len], kMaxFileNameSizeSize - len,
             "%s", ptr + 2);
  } else {
    snprintf(gCygProfileFileName, kMaxFileNameSizeSize, "%s", filename);
  }

  if (gLogFile)
    cygprofile_closelogfile();

  return 0;
}

// Returns the log file name.  If it has yet to be set, it is first
// set to the default file name, kDefaultFileName.
char* cygprofile_getfilename(void) {
  if (!strlen(gCygProfileFileName))
    cygprofile_setfilename(kDefaultFileName);
  return gCygProfileFileName;
}

// Opens the file name specified and returns a pointer to the file.
FILE* cygprofile_openlogfile(const char* filename) {
  static int complained = 0;
  FILE* file;

  if (complained)
    return NULL;

  if (gLogFile)
    return gLogFile;

  file = fopen(filename, "w");
  if (!file) {
    fprintf(stderr, "WARNING: Can't open gLogFile '%s': %s\n",
            filename, strerror(errno));
    complained = 1;
    return NULL;
  }

  setlinebuf(file);
  gLogFile = file;

  return file;
}

// Closes the log file (i.e. gLogFile).
void cygprofile_closelogfile(void) {
  if (gLogFile)
    fclose(gLogFile);
}

// Given a file name, sets the name of the log file and then enables profiling.
void cygprofile_start(const char* filename) {
  cygprofile_setfilename(filename);
  cygprofile_enable();
}

// Indicates the end of profiling.  Disables profiling, writes "END" on a
// new line to indicate no more profiling, flushes the gLogFile and finally
// closes the log file.
void cygprofile_end(void) {
  cygprofile_disable();
  fprintf(gLogFile, "END\n");
  fflush(gLogFile);
  cygprofile_closelogfile();
}

}  // namespace cygprofile
