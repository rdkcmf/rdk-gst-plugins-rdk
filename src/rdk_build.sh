#!/bin/bash

##########################################################################
# Copyright 2014 RDK Management
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation, version 2
# of the license.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.
##########################################################################
 

#######################################
#
# Build Framework standard script for
#
# GST Plugins component

# use -e to fail on any shell issue
# -e is the requirement from Build Framework
set -e


# default PATHs - use `man readlink` for more info
# the path to combined build
export RDK_PROJECT_ROOT_PATH=${RDK_PROJECT_ROOT_PATH-`readlink -m ../../..`}
export COMBINED_ROOT=$RDK_PROJECT_ROOT_PATH

# path to build script (this script)
export RDK_SCRIPTS_PATH=${RDK_SCRIPTS_PATH-`readlink -m $0 | xargs dirname`}

# path to components sources and target
export RDK_SOURCE_PATH=${RDK_SOURCE_PATH-$RDK_SCRIPTS_PATH}
export RDK_TARGET_PATH=${RDK_TARGET_PATH-$RDK_SOURCE_PATH}

# fsroot and toolchain (valid for all devices)
export RDK_FSROOT_PATH=${RDK_FSROOT_PATH-`readlink -m $RDK_PROJECT_ROOT_PATH/sdk/fsroot/ramdisk`}
export RDK_TOOLCHAIN_PATH=${RDK_TOOLCHAIN_PATH-`readlink -m $RDK_PROJECT_ROOT_PATH/sdk/toolchain/staging_dir`}


# default component name
export RDK_COMPONENT_NAME=${RDK_COMPONENT_NAME-`basename $RDK_SOURCE_PATH`}
pd=`pwd`
cd ${RDK_SOURCE_PATH}
export FSROOT=$RDK_FSROOT_PATH
export COMBINED_ROOT=$RDK_PROJECT_ROOT_PATH
export BUILDS_DIR=$RDK_PROJECT_ROOT_PATH
export RDK_DIR=$BUILDS_DIR
source $BUILDS_DIR/gst-plugins/soc/build/soc_env.sh
cd $pd


# component-specific vars
CLEAN_BUILD=0
CONFIG_MODE="HEADED_GW"

# parse arguments
INITIAL_ARGS=$@

function usage()
{
    set +x
    echo "Usage: `basename $0` [-h|--help] [-v|--verbose] [action]"
    echo "    -h    --help                  : this help"
    echo "    -v    --verbose               : verbose output"
    echo
    echo "Supported actions:"
    echo "      configure, clean, build (DEFAULT), rebuild, install"
}

# options may be followed by one colon to indicate they have a required argument
if ! GETOPT=$(getopt -n "build.sh" -o hv -l help,verbose -- "$@")
then
    usage
    exit 1
fi

eval set -- "$GETOPT"

while true; do
  case "$1" in
    -h | --help ) usage; exit 0 ;;
    -v | --verbose ) set -x ;;
    -- ) shift; break;;
    * ) break;;
  esac
  shift
done

ARGS=$@

if [ "x$BUILD_CONFIG" == "xhybrid-legacy" ] ; then
    export BUILD_CONFIG="hybrid"
fi

if [ "x$BUILD_CONFIG" == "xcef" ] || [ "x$BUILD_CONFIG" == "xcef-wkit" ] ;  then
    if [ "$RDK_PLATFORM_DEVICE" != "xi3" ] ; then
        export BUILD_CONFIG="hybrid"
    fi
fi

# component-specific vars
if [ "$RDK_PLATFORM_DEVICE" = "xi3" ] || [ "$RDK_PLATFORM_DEVICE" = "xi4" ]; then
    export CONFIG_MODE="CLIENT_TSB"
else
    case "$BUILD_CONFIG" in
        "mediaclient") export CONFIG_MODE=CLIENT_TSB;;
        "headlessmediaclient") export CONFIG_MODE=CLIENT_TSB;;
        "legacy") export CONFIG_MODE=HEADED_GW;;
        "hybrid") export CONFIG_MODE=HEADED_GW;;
        "headless") export CONFIG_MODE=HEADLESS_GW;;
        "headless-legacy") export CONFIG_MODE=HEADLESS_GW;;
     esac
fi

# functional modules

function configure()
{
    pd=`pwd`
    cd $RDK_DIR/gst-plugins/generic/
    aclocal -I cfg
    libtoolize --automake
    autoheader
    automake --foreign --add-missing
    rm -f configure
    autoconf
    echo "  CONFIG_MODE = $CONFIG_MODE"
    configure_options=" "

    if [ "x$DEFAULT_HOST" != "x" ]; then
        configure_options="--host $DEFAULT_HOST"
    fi

    if [ "$RDK_PLATFORM_SOC" = "stm" ];then
        configure_options="$STM_CONFIG_OPTIONS"
    fi

    configure_options="$configure_options --enable-shared --with-pic"
    generic_options=$configure_options
    if [ "x$BUILD_CONFIG" == "x" ] && [ "$RDK_PLATFORM_DEVICE" != "xi3" ] && [ "$RDK_PLATFORM_DEVICE" != "xi4" ]; then
        configure_options="$configure_options --enable-dtcpdec"
    elif [ "x$BUILD_CONFIG" == "xhybrid" ] && [ "$RDK_PLATFORM_DEVICE" == "rng150" ]; then
        configure_options="$configure_options --enable-dtcpdec --enable-dtcpenc --enable-httpsink --enable-httpsrc --enable-rbifilter"
    else
        configure_options="$configure_options --enable-dtcpdec --enable-dtcpenc --enable-httpsink --enable-aesencrypt --enable-aesdecrypt --enable-dvrsrc --enable-dvrsink --enable-httpsrc --enable-rbifilter"
    fi

    export ac_cv_func_malloc_0_nonnull=yes
    export ac_cv_func_memset=yes

	./configure --prefix=${RDK_FSROOT_PATH}/usr/local $configure_options
	if [ "$RDK_PLATFORM_SOC" = "stm" ];then
        cd ../soc
		aclocal -I cfg
		libtoolize --automake
		autoheader
		automake --foreign --add-missing
		rm -f configure
		autoconf
		./configure --prefix=${RDK_FSROOT_PATH}/usr/local $generic_options
		if [ ${RDK_PLATFORM_DEVICE} == "IntelXG5" ] && [ ${BUILD_CONFIG} == "headless" ]; then
            cd $RDK_DIR/gst-plugins/devices/src
            aclocal -I cfg
            libtoolize --automake
            autoheader
            automake --foreign --add-missing
            rm -f configure
            autoconf
            ./configure --prefix=${RDK_FSROOT_PATH}/usr/local $generic_options
		fi	
    fi
    if [ "$RDK_PLATFORM_DEVICE" != "xi3" ] && [ "x$BUILD_CONFIG" == "xhybrid"  ]; then
        cd $RDK_DIR/gst-plugins/devices/src
        aclocal -I cfg
        libtoolize --automake
        autoheader
        automake --foreign --add-missing
        rm -f configure
        autoconf
        ./configure --prefix=${RDK_FSROOT_PATH}/usr/local $generic_options
    fi
	cd $pd
}

function clean()
{
    pd=`pwd`
    CLEAN_BUILD=1
    dnames="$RDK_DIR/gst-plugins/generic  "
    if [ "$RDK_PLATFORM_DEVICE" != "xi3" ] && [ "x$BUILD_CONFIG" == "xhybrid"  ]; then
        dnames="$RDK_DIR/gst-plugins/generic/ $RDK_DIR/gst-plugins/generic/../devices/src"
    elif [ ${RDK_PLATFORM_DEVICE} == "IntelXG5" ] && [ ${BUILD_CONFIG} == "headless" ]; then
        dnames="$dnames $RDK_DIR/gst-plugins/generic/ $RDK_DIR/gst-plugins/generic/../devices/src"
    fi
    if [ "$RDK_PLATFORM_SOC" = "stm" ];then
    	dnames="$dnames $RDK_DIR/gst-plugins/generic/../soc"
    fi
    for dName in $dnames
    do
        cd $dName
 	if [ -f Makefile ]; then
    		make distclean
	fi
    	rm -f configure;
    	rm -rf aclocal.m4 autom4te.cache config.log config.status libtool
    	find . -iname "Makefile.in" -exec rm -f {} \; 
    	find . -iname "Makefile" | grep -v 'rbifilter' | xargs rm -f 
    	ls cfg/* | grep -v "Makefile.am" | xargs rm -f
    	cd $pd
    done
}

function build()
{
    cd ${RDK_SOURCE_PATH}
    export FSROOT=$RDK_FSROOT_PATH
    export COMBINED_ROOT=$RDK_PROJECT_ROOT_PATH
    export BUILDS_DIR=$RDK_PROJECT_ROOT_PATH
    export RDK_DIR=$BUILDS_DIR
    source $BUILDS_DIR/gst-plugins/soc/build/soc_env.sh

    pd=`pwd`
    cd $RDK_DIR/gst-plugins/generic
    make
    if [ "$RDK_PLATFORM_SOC" = "stm" ];then
        cd ../soc
        make
    fi
    if [ "$RDK_PLATFORM_DEVICE" != "xi3" ] && [ "x$BUILD_CONFIG" == "xhybrid"  ]; then
        cd ../devices/src
        make
    elif [ ${RDK_PLATFORM_DEVICE} == "IntelXG5" ] && [ ${BUILD_CONFIG} == "headless" ]; then
        cd ../devices/src
        make
    fi
    cd $pd
    echo "  $comp build success "
}

function rebuild()
{
    clean
    configure
    build
}

function install()
{
    pd=`pwd`
    cd $RDK_DIR/gst-plugins/generic
    make install
    if [ "$RDK_PLATFORM_SOC" = "stm" ];then
    	cd ../soc
	    make install
    fi
    if [ "$RDK_PLATFORM_DEVICE" != "xi3" ] && [ "x$BUILD_CONFIG" == "xhybrid"  ]; then
        cd ../devices/src
        make install
    elif [ ${RDK_PLATFORM_DEVICE} == "IntelXG5" ] && [ ${BUILD_CONFIG} == "headless" ]; then
        cd ../devices/src
        make install
    fi
    cd $pd
}


# run the logic

#these args are what left untouched after parse_args
HIT=false

for i in "$ARGS"; do
    case $i in
        configure)  HIT=true; configure ;;
        clean)      HIT=true; clean ;;
        build)      HIT=true; build ;;
        rebuild)    HIT=true; rebuild ;;
        install)    HIT=true; install ;;
        *)
            #skip unknown
        ;;
    esac
done

# if not HIT do build by default
if ! $HIT; then
  build
fi
