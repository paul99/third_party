/*
 * cyg-profile.h - Header file for CygProfiler
 *
 * Michal Ludvig <michal@logix.cz>
 * http://www.logix.cz/michal/devel
 *
 * This source code is a public domain.
 *
 * See cyg_profile.cc for details on usage.
 */

#ifndef THIRD_PARTY_CYGPROFILE_CYG_PROFILE_H_
#define THIRD_PARTY_CYGPROFILE_CYG_PROFILE_H_

namespace cygprofile {

#ifdef __cplusplus
extern "C" {
  #endif

  void cygprofile_enable(void)
      __attribute__((no_instrument_function));
  void cygprofile_disable(void)
      __attribute__((no_instrument_function));

  bool cygprofile_isenabled(void)
      __attribute__((no_instrument_function));

  int cygprofile_setfilename(const char* filename)
      __attribute__((no_instrument_function));

  char* cygprofile_getfilename(void)
      __attribute__((no_instrument_function));

  void cygprofile_start(const char* filename)
      __attribute__((no_instrument_function));

  void cygprofile_end(void)
      __attribute__((no_instrument_function));

  #ifdef __cplusplus
};
#endif

}  // namespace cygprofile

#endif
