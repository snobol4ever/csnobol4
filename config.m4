# config generated Sun Apr 12 18:47:02 UTC 2026
# on runsc (Linux)
# by $Id: configure,v 1.432 2025-05-18 23:42:16 phil Exp $
undefine([include])
ADD_CPPFLAGS([-DHAVE_CONFIG_H])
# see config.h for more defines
VERS=2.3.3
BINDIR=$(DESTDIR)/usr/local/bin
MANDIR=$(DESTDIR)/usr/local/man
MAN1DIR=$(DESTDIR)/usr/local/man/man1
MAN3DIR=$(DESTDIR)/usr/local/man/man3
MAN7DIR=$(DESTDIR)/usr/local/man/man7
SNOLIB=$(DESTDIR)/usr/local/lib/snobol4
SNOLIB_LIB=$(DESTDIR)/usr/local/lib/snobol4/2.3.3/lib
SNOLIB_LOCAL=$(DESTDIR)/usr/local/lib/snobol4/2.3.3/local
SNOLIB_DOC=$(DESTDIR)/usr/local/lib/snobol4/2.3.3
INCLUDE_DIR=$(DESTDIR)/usr/local/lib/snobol4/2.3.3/include
DOC_DIR=$(DESTDIR)/usr/local/share/doc/snobol4-2.3.3
# cc is gcc
CCM=$(CC) -MM
ADD_CFLAGS([-Wall])
ADD_SNOBOL4_C_CFLAGS([-Wno-return-type -Wno-switch])
CC=cc
################
# C Compiler:
# cc -v
# Using built-in specs.
# COLLECT_GCC=cc
# COLLECT_LTO_WRAPPER=/usr/libexec/gcc/x86_64-linux-gnu/13/lto-wrapper
# OFFLOAD_TARGET_NAMES=nvptx-none:amdgcn-amdhsa
# OFFLOAD_TARGET_DEFAULT=1
# Target: x86_64-linux-gnu
# Configured with: ../src/configure -v --with-pkgversion='Ubuntu 13.3.0-6ubuntu2~24.04.1' --with-bugurl=file:///usr/share/doc/gcc-13/README.Bugs --enable-languages=c,ada,c++,go,d,fortran,objc,obj-c++,m2 --prefix=/usr --with-gcc-major-version-only --program-suffix=-13 --program-prefix=x86_64-linux-gnu- --enable-shared --enable-linker-build-id --libexecdir=/usr/libexec --without-included-gettext --enable-threads=posix --libdir=/usr/lib --enable-nls --enable-bootstrap --enable-clocale=gnu --enable-libstdcxx-debug --enable-libstdcxx-time=yes --with-default-libstdcxx-abi=new --enable-libstdcxx-backtrace --enable-gnu-unique-object --disable-vtable-verify --enable-plugin --enable-default-pie --with-system-zlib --enable-libphobos-checking=release --with-target-system-zlib=auto --enable-objc-gc=auto --enable-multiarch --disable-werror --enable-cet --with-arch-32=i686 --with-abi=m64 --with-multilib-list=m32,m64,mx32 --enable-multilib --with-tune=generic --enable-offload-targets=nvptx-none=/build/gcc-13-EldibY/gcc-13-13.3.0/debian/tmp-nvptx/usr,amdgcn-amdhsa=/build/gcc-13-EldibY/gcc-13-13.3.0/debian/tmp-gcn/usr --enable-offload-defaulted --without-cuda-driver --enable-checking=release --build=x86_64-linux-gnu --host=x86_64-linux-gnu --target=x86_64-linux-gnu --with-build-config=bootstrap-lto-lean --enable-link-serialization=2
# Thread model: posix
# Supported LTO compression algorithms: zlib zstd
# gcc version 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) 
################
# sizeof(int) = 4
# sizeof(void *) = 8
# sizeof(long) = 8
# sizeof(float) = 4
# sizeof(double) = 8
# alignment(double) = 8
OPT=-O3
# GLIBC version 2.39
MSTIME_C=$(SRCDIR)lib/bsd/mstime.c
INET_O=inet6.o
ADD_OBJS([bindresvport.o])
ADD_SRCS([$(BINDRESVPORT_C)])
ADD_OBJS([popen.o])
ADD_SRCS([$(POPEN_C)])
ADD_SNOBOL4_LDFLAGS([-lutil])
PTYIO_OBJ_C=$(SRCDIR)lib/bsd/ptyio_obj.c
TTY_C=$(SRCDIR)lib/posix/tty.c
LOAD_C=$(SRCDIR)lib/unix98/load.c
ADD_SNOBOL4_LDFLAGS([-ldl])
DYNAMIC_C=$(SRCDIR)lib/posix/dynamic.c
SNOBOL4_LDFLAGS=-rdynamic
# Shared Object Libraries (in config.h too)
SO_EXT=.so
SO_CFLAGS=-fPIC -fvisibility=hidden
SO_LD=cc
SO_LDFLAGS=-shared -fPIC
# Dynamicly Loaded Extensions (in config.h too)
DL_EXT=.so
DL_CFLAGS=-fPIC
DL_LD=cc
DL_LDFLAGS=-shared -fPIC
ADD_SNOBOL4_LDFLAGS([-lz])
ADD_SNOBOL4_LDFLAGS([-lbz2])
MEMIO_OBJ_C=$(SRCDIR)lib/posix/memio_obj.c
MEMIO_OBJ=memio_obj.o
MEMIO_SRC=$(MEMIO_OBJ_C)
SLEEP_C=$(SRCDIR)posix/sleep.c
ADD_OBJS([bufio_obj.o])
ADD_SRCS([$(BUFIO_OBJ_C)])
ADD_OBJS([compio_obj.o])
ADD_SRCS([$(COMPIO_OBJ_C)])
INSTALL=/usr/bin/install
define([INSTALL_DOCS],)
BLOCKS=1
MODULES=base64 dirs logic random stat time sprintf fork readline zlib ndbm readline ffi
TEST_SNOPATH=..:../modules/base64:../modules/dirs:../modules/logic:../modules/random:../modules/stat:../modules/time:../modules/sprintf:../modules/fork:../modules/readline:../modules/zlib:../modules/ndbm:../modules/readline:../modules/ffi:../snolib:.
# if the file local-config existed it would have been incorporated here
