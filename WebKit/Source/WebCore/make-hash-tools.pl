#! /usr/bin/perl
#
#   This file is part of the WebKit project
#
#   Copyright (C) 2010 Andras Becsi (abecsi@inf.u-szeged.hu), University of Szeged
#
#   This library is free software; you can redistribute it and/or
#   modify it under the terms of the GNU Library General Public
#   License as published by the Free Software Foundation; either
#   version 2 of the License, or (at your option) any later version.
#
#   This library is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   Library General Public License for more details.
#
#   You should have received a copy of the GNU Library General Public License
#   along with this library; see the file COPYING.LIB.  If not, write to
#   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
#   Boston, MA 02110-1301, USA.

use strict;
use File::Basename;

my $outdir = $ARGV[0];
shift;
my $option = basename($ARGV[0],".gperf");


if ($option eq "ColorData") {

    my $colorDataGenerated         = "$outdir/ColorData.cpp";
    my $colorDataGperf             = $ARGV[0];
    shift;

    # Clank Hack

    # Do not upstream like this; make conditional on an environment variable or other param.

    # Clank change: don't inline the findBLAH routine.
    open C, ">$colorDataGenerated" || die "Could not open $colorDataGenerated for writing";
    print C << "EOF";
/* Clank Linux build *must* have __inline defined in string.h, so include that first. */
#include <string.h>
EOF
    close C;

    # end of Clank Hack

    system("gperf --key-positions=\"*\" -D -s 2 $colorDataGperf --output-file=$colorDataGenerated") == 0 || die "calling gperf failed: $?";

} else {
    die "Unknown option.";
}
