#/bin/sh
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cd `dirname $0`/../source/data

# Excludes curr data which is not used on Android.
echo Overwriting curr/reslocal.mk...
cat >curr/reslocal.mk <<END
CURR_CLDR_VERSION = 1.9
CURR_SYNTHETIC_ALIAS =
CURR_ALIAS_SOURCE =
CURR_SOURCE =
END

# Excludes region data. On Android Java API is used to get the data.
echo Overwriting region/reslocal.mk...
cat >region/reslocal.mk <<END
REGION_CLDR_VERSION = 1.9
REGION_SYNTHETIC_ALIAS =
REGION_ALIAS_SOURCE =
REGION_SOURCE =
END

# On Android Java API is used to get lang data, except for the script
# names of Hans and Hant which is not supported by Java API.
# Here remove all lang data except for Hans and Hant script names.
# See the comments in GetDisplayNameForLocale() (in Chromium's
# src/ui/base/l10n/l10n_util.cc) about why we need the scripts.
for i in lang/*.txt; do
  echo Overwriting $i...
  sed '/^    Keys{$/,/^    }$/d
       /^    Languages{$/,/^    }$/d
       /^    Scripts{$/,/^    }$/{
           /^    Scripts{$/p
           /^        Hans/p
           /^        Hant/p
           /^    }$/p
           d
       }
       /^    Types{$/,/^    }$/d
       /^    Variants{$/,/^    }$/d
       /^    codePatterns{$/,/^    }$/d
       /^    localeDisplayPattern{$/,/^    }$/d' -i $i
done

echo DONE.
