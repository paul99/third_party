# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'targets': [
    {
      'target_name': 'android_gcm',
      'type' : 'none',
      'all_dependent_settings': {
        'variables': {
          'input_jars_paths' : [
            'sdk/extras/google/gcm/gcm-client/dist/gcm.jar',
          ],
        }
      }
    },
  ],
}
