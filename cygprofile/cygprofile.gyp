# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
  },
  'targets': [
    {
      'target_name': 'cygprofile',
      'type': 'static_library',
      'include_dirs': [ '../..', ],
      'sources': [
        'cyg-profile.h',
        'cyg-profile.cc',
      ],
      'cflags!': [ '-finstrument-functions' ],
    },
    {
      'target_name': 'cygprofile_test',
      'type': 'executable',
      'dependencies': [
        './cygprofile.gyp:cygprofile',
      ],
      'sources': [
        'cygprofile_unittest.cc',
        'cyg-profile.h',
      ],
    },
  ],
}
