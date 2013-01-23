
/*
 * Copyright 2012 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkUtilsArm.h"

#if SK_ARM_NEON_IS_DYNAMIC

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define NEON_DEBUG  0

#if NEON_DEBUG
#include <android/log.h>
#include <sys/system_properties.h>
#define D(...)  __android_log_print(ANDROID_LOG_INFO, "SkUtilsArm", __VA_ARGS__)
#else
#define D(...)  ((void)0)
#endif

static bool            sHasArmNeon;
static pthread_once_t  sOnce = PTHREAD_ONCE_INIT;

static void probe_cpu_for_neon(void)
{
#if NEON_DEBUG
    // Allow forcing the mode through the environment during debugging.
#define PROP_NAME  "debug.skia.arm_neon_mode"
    char prop[PROP_VALUE_MAX];
    if (__system_property_get(PROP_NAME, prop) > 0) {
        D("%s: %s", PROP_NAME, prop);
        if (!strcmp(prop, "1")) {
            D("Forcing ARM Neon mode to full!");
            sHasArmNeon = true;
            return;
        }
        if (!strcmp(prop, "0")) {
            D("Disabling ARM NEON mode");
            sHasArmNeon = false;
            return;
        }
    }
    D("Running dynamic CPU feature detection");
#endif

    // There is no user-accessible CPUID instruction on ARM that we can use.
    // Instead, we must parse /proc/cpuinfo and look for the 'neon' feature.
    // For example, here's a typical output (Nexus S running ICS 4.0.3):
    /*
    Processor       : ARMv7 Processor rev 2 (v7l)
    BogoMIPS        : 994.65
    Features        : swp half thumb fastmult vfp edsp thumbee neon vfpv3 
    CPU implementer : 0x41
    CPU architecture: 7
    CPU variant     : 0x2
    CPU part        : 0xc08
    CPU revision    : 2

    Hardware        : herring
    Revision        : 000b
    Serial          : 3833c77d6dc000ec
    */
    char   buffer[4096];

    // If we fail any of the following, assume we don't have NEON instructions
    // This allows us to return immediately in case of error.
    sHasArmNeon = false;

    do {
        // open /proc/cpuinfo
        int fd = TEMP_FAILURE_RETRY(open("/proc/cpuinfo", O_RDONLY));
        if (fd < 0) {
            D("Could not open /proc/cpuinfo: %s", strerror(errno));
            break;
        }

        // Read the file. To simplify our search, we're going to place two
        // sentinel '\n' characters: one at the start of the buffer, and one at
        // the end. This means we reserve the first and last buffer bytes.
        buffer[0] = '\n';
        int size = TEMP_FAILURE_RETRY(read(fd, buffer+1, sizeof(buffer)-2));
        close(fd);

        if (size < 0) {  // should not happen
            D("Could not read /proc/cpuinfo: %s", strerror(errno));
            break;
        }

        D("START /proc/cpuinfo:\n%.*s\nEND /proc/cpuinfo", size, buffer+1);

        // Compute buffer limit, and place final sentinel
        char* buffer_end = buffer + 1 + size;
        buffer_end[0] = '\n';

        // Now, find a line that starts with "Features", i.e. look for
        // '\nFeatures ' in our buffer.
        char*  line = (char*) memmem(buffer, buffer_end - buffer, "\nFeatures\t", 10);
        if (line == NULL) {  // Weird, no Features line, bad kernel?
            D("Could not find a line starting with 'Features' in /proc/cpuinfo ?");
            break;
        }

        line += 10;  // Skip the "\nFeatures\t" prefix

        // Find the end of the current line
        char* line_end = (char*) memchr(line, '\n', buffer_end - line);
        if (line_end == NULL)
            line_end = buffer_end;

        // Now find an instance of 'neon' in the flags list. We want to
        // ensure it's only 'neon' and not something fancy like 'noneon'
        // so check that it follows a space.
        const char* neon = (const char*) memmem(line, line_end - line, " neon", 5);
        if (neon == NULL)
            break;

        // Ensure it is followed by a space or a newline.
        if (neon[5] != ' ' && neon[5] != '\n')
            break;

        // Fine, we support Arm NEON !
        sHasArmNeon = true;

    } while (0);

    if (sHasArmNeon) {
        D("Device supports ARM NEON instructions!");
    } else {
        D("Device does NOT support ARM NEON instructions!");
    }
}

bool sk_cpu_arm_has_neon(void)
{
    pthread_once(&sOnce, probe_cpu_for_neon);
    return sHasArmNeon;
}

#endif // SK_ARM_NEON_IS_DYNAMIC
