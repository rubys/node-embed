srcdir := /Users/rubys/git/node
builddir := $(srcdir)/out/Release
obj := $(builddir)/obj

DEFS_Release := \
	'-D_DARWIN_USE_64_BIT_INODE=1' \
	'-DNODE_ARCH="x64"' \
	'-DNODE_WANT_INTERNALS=1' \
	'-DV8_DEPRECATION_WARNINGS=1' \
	'-DNODE_OPENSSL_SYSTEM_CERT_PATH=""' \
	'-DHAVE_INSPECTOR=1' \
	'-DHAVE_DTRACE=1' \
	'-D__POSIX__' \
	'-DNODE_USE_V8_PLATFORM=1' \
	'-DNODE_HAVE_I18N_SUPPORT=1' \
	'-DNODE_HAVE_SMALL_ICU=1' \
	'-DNODE_PLATFORM="darwin"' \
	'-DHAVE_OPENSSL=1' \
	'-DUCONFIG_NO_SERVICE=1' \
	'-DUCONFIG_NO_REGULAR_EXPRESSIONS=1' \
	'-DU_ENABLE_DYLOAD=0' \
	'-DU_STATIC_IMPLEMENTATION=1' \
	'-DU_HAVE_STD_STRING=1' \
	'-DUCONFIG_NO_BREAK_ITERATION=0' \
	'-DHTTP_PARSER_STRICT=0' \
	'-D_LARGEFILE_SOURCE' \
	'-D_FILE_OFFSET_BITS=64' \
	'-DNGHTTP2_STATICLIB'

INCS_Release := \
	-I$(srcdir)/src \
	-I$(obj)/gen \
	-I$(obj)/gen/include \
	-I$(obj)/gen/src \
	-I$(srcdir)/deps/v8/include \
	-I$(srcdir)/deps/icu-small/source/i18n \
	-I$(srcdir)/deps/icu-small/source/common \
	-I$(srcdir)/deps/zlib \
	-I$(srcdir)/deps/http_parser \
	-I$(srcdir)/deps/cares/include \
	-I$(srcdir)/deps/uv/include \
	-I$(srcdir)/deps/nghttp2/lib/includes \
	-I$(srcdir)/deps/openssl/openssl/include

CFLAGS_Release := \
	-Os \
	-gdwarf-2 \
	-mmacosx-version-min=10.7 \
	-arch x86_64 \
	-Wall \
	-Wendif-labels \
	-W \
	-Wno-unused-parameter

CFLAGS_CC_Release := \
	-std=gnu++1y \
	-stdlib=libc++ \
	-fno-rtti \
	-fno-exceptions \
	-fno-threadsafe-statics \
	-fno-strict-aliasing

LDFLAGS_Release := \
	-Wl,-force_load,$(builddir)/libnode.a \
	-Wl,-force_load,$(builddir)/libv8_base.a \
	-Wl,-force_load,$(builddir)/libzlib.a \
	-Wl,-force_load,$(builddir)/libuv.a \
	-Wl,-force_load,$(builddir)/libopenssl.a \
	-Wl,-no_pie \
	-Wl,-search_paths_first \
	-mmacosx-version-min=10.7 \
	-arch x86_64 \
	-L$(builddir) \
	-stdlib=libc++

LIBS := \
	-framework CoreFoundation \
	-lm

LD_INPUTS := $(OBJS) $(builddir)/libnode.a $(builddir)/libv8_libplatform.a $(builddir)/libicui18n.a $(builddir)/libzlib.a $(builddir)/libhttp_parser.a $(builddir)/libcares.a $(builddir)/libuv.a $(builddir)/libnghttp2.a $(builddir)/libopenssl.a $(builddir)/libv8_base.a $(builddir)/libv8_libbase.a $(builddir)/libv8_libsampler.a $(builddir)/libicuucx.a $(builddir)/libicudata.a $(builddir)/libicustubdata.a $(builddir)/libv8_snapshot.a

node_main: node_main.o node.o
	c++ $(LDFLAGS_Release) -o $@ $+ $(LIBS) $(LD_INPUTS)

node.o: node.cc node_embed.h
	c++ $(DEFS_Release) $(INCS_Release) $(CFLAGS_Release) \
	$(CFLAGS_CC_Release) -c $< -o $@

node_main.o: node_main.c node_embed.h
	cc $(CFLAGS_Release) -c $< -o $@
