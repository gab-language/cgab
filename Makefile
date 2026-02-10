NATIVECC  = zig cc
TARGETCC	= zig cc --target=$(GAB_TARGETS)
TARGETCXX = zig c++

SRC_PREFIX 	 	 	= src/**
BUILD_PREFIX 	 	= build-$(GAB_TARGETS)
INCLUDE_PREFIX 	= include
VENDOR_PREFIX   = vendor

GAB_VERSION_TAG = 0.0.5

INCLUDE		= -I$(INCLUDE_PREFIX) -isystem$(VENDOR_PREFIX) -L$(BUILD_PREFIX)

CFLAGS = -std=c23 \
				 -fPIC \
				 -Wall \
				 -MMD  \
				 -DGAB_TARGET_TRIPLE=\"$(GAB_TARGETS)\"\
				 -DGAB_DYNLIB_FILEENDING=\"$(GAB_DYNLIB_FILEENDING)\" \
				 -DGAB_BUILDTYPE=\"$(GAB_BUILDTYPE)\"\
				 $(INCLUDE) \
				 $(GAB_CCFLAGS)

CXXFLAGS = -std=c++23 \
				 -fPIC \
				 -Wall \
				 --target=$(GAB_TARGETS) \
				 -DGAB_TARGET_TRIPLE=\"$(GAB_TARGETS)\"\
				 -DGAB_DYNLIB_FILEENDING=\"$(GAB_DYNLIB_FILEENDING)\" \
				 $(INCLUDE) \
				 $(GAB_CCFLAGS)

# A binary executable needs to keep all cgab symbols,
# in case they are used by a dynamically loaded c-module.
# This is why -rdynamic is used.
#
GAB_LINK_DEPS = 
BINARY_FLAGS 	= -rdynamic -Wl,--no-gc-sections -DGAB_CORE $(GAB_LINK_DEPS) $(GAB_BINARYFLAGS)

# A shared module needs undefined dynamic lookup
# As it is not linked with cgab. The symbols from cgab
# that these modules require will already exist,
# as they will be in the gab executable
# Best way to conditionally link in dynamic stuff like -framework Cocoa?
#
# These below are static, and so are optimized out if not used.
CMOD_LINK_DEPS   =

CXXMOD_LINK_DEPS = 
CXXMOD_INCLUDE   = 

CSHARED_FLAGS 	= -shared -undefined dynamic_lookup $(CMOD_LINK_DEPS)
CXXSHARED_FLAGS = -shared -undefined dynamic_lookup $(CXXMOD_LINK_DEPS) $(CXXMOD_INCLUDE)

# Source files in src/cgab are part of libcgab.
# Their object files are compiled and archived together into libcgab.a
# There are corresponding *determinstic* versions, prefixed with d.
CGAB_SRC = $(wildcard src/cgab/*.c)
CGAB_OBJ = $(CGAB_SRC:src/cgab/%.c=$(BUILD_PREFIX)/%.o)

# Source files in src/gab are part of gab's cli, which depends on libcgab
# They are compiled into an executable and linked statically
# with libcgab.a
# There are corresponding *determinstic* versions, prefixed with d.
GAB_SRC = $(wildcard src/gab/*.c)
GAB_OBJ = $(GAB_SRC:src/gab/%.c=$(BUILD_PREFIX)/%.o)

# Source files in src/mod/ are individual c-modules, importable from gab code.
# They are compiled individually into dynamic libraries, loaded at runtime.
CMOD_SRC 	 = $(wildcard src/mod/*.c)
CMOD_SHARED = $(CMOD_SRC:src/mod/%.c=$(BUILD_PREFIX)/mod/%.cgab-$(GAB_VERSION_TAG)-$(GAB_TARGETS)$(GAB_DYNLIB_FILEENDING))

CXXMOD_SRC 	 = $(wildcard src/mod/*.cc)
CXXMOD_SHARED = $(CXXMOD_SRC:src/mod/%.cc=$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING))

all: amalgamation gab cmodules cxxmodules

-include $(CGAB_OBJ:.o=.d) $(GAB_OBJ:.o=.d) $(CMOD_SHARED:$(GAB_DYNLIB_FILEENDING)=.d)

# This rule builds object files out of c source files
# Somewhat confusing that miniz amalgamation needs to be made here,
# but that is because the main *object file* is created before the executable,
# and the object file cannot compile without the header and impl from this.
$(BUILD_PREFIX)/%.o: $(SRC_PREFIX)/%.c $(VENDOR_PREFIX)/miniz/amalgamation/miniz.c
	$(TARGETCC) $(CFLAGS) $< -c -o $@

# This rule builds libcgab by archiving the cgab object files together.
$(BUILD_PREFIX)/libcgab.a: $(CGAB_OBJ)
	zig ar rcs $@ $^

# This rule builds a gab.c *amalgamation* style cfile.
$(BUILD_PREFIX)/gab.c: $(CGAB_SRC) 
	cat $^ > $@

# This rule builds the gab executable, linking with libcgab.a
$(BUILD_PREFIX)/gab: $(GAB_OBJ) $(BUILD_PREFIX)/libcgab.a
	$(TARGETCC) $(CFLAGS) $(BINARY_FLAGS) -o $@ $^

# This rule builds each c++ module shared library.
$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.cc
	$(TARGETCXX) $(CXXFLAGS) $(CXXSHARED_FLAGS) $< -o $@

# This rule builds each c module shared library.
# per-library flags are declared in the configuration.
# They are passed through the funky basename-notdir call.
# Essential, the cio module receives flags through the cio_FLAGS environment variable.
$(BUILD_PREFIX)/mod/%.cgab-$(GAB_VERSION_TAG)-$(GAB_TARGETS)$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.c \
							$(BUILD_PREFIX)/libbearssl.a 	\
							$(BUILD_PREFIX)/libllhttp.a  	\
							$(BUILD_PREFIX)/libgrapheme.a \
							$(VENDOR_PREFIX)/sqlite3.c    \
							$(VENDOR_PREFIX)/duckdb.cc
	$(TARGETCC) $(CFLAGS) $(CSHARED_FLAGS) $($(basename $(notdir $<))_FLAGS) $< -o $@

# This curls a mozilla cert used for TLS clients.
cacert.pem:
	curl --etag-compare etag.txt --etag-save etag.txt --remote-name 'https://curl.se/ca/cacert.pem'

# This rule is the bear-ssl generated file equating to our certificate.
# Used in TLS clients.
$(VENDOR_PREFIX)/ta.h: cacert.pem
	make CC="$(NATIVECC)" -C $(VENDOR_PREFIX)/BearSSL
	$(VENDOR_PREFIX)/BearSSL/build/brssl ta cacert.pem > vendor/ta.h
	make clean -s -C $(VENDOR_PREFIX)/BearSSL

$(VENDOR_PREFIX)/sqlite3.c:
	mkdir -p $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX)
	cd $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX) && \
		../configure --enable-all
	make -C $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX) sqlite3.c
	cp $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX)/sqlite3.c $(VENDOR_PREFIX)/

$(VENDOR_PREFIX)/duckdb.cc:
	cd $(VENDOR_PREFIX)/duckdb && \
		python scripts/amalgamation.py
	cp $(VENDOR_PREFIX)/duckdb/src/amalgamation/duckdb.cpp $(VENDOR_PREFIX)/duckdb.cc

# These rules generates header files for libgrapheme
$(VENDOR_PREFIX)/libgrapheme/gen/%.h: 
	make CC="$(NATIVECC)" -C $(VENDOR_PREFIX)/libgrapheme gen/$*.h

$(VENDOR_PREFIX)/llhttp.h:
	cd $(VENDOR_PREFIX)/llhttp && npm i
	make -C $(VENDOR_PREFIX)/llhttp
	cp $(VENDOR_PREFIX)/llhttp/build/llhttp.h $(VENDOR_PREFIX)/
	make clean -s -C $(VENDOR_PREFIX)/llhttp

$(VENDOR_PREFIX)/miniz/amalgamation/miniz.c:
	cd $(VENDOR_PREFIX)/miniz && \
		./amalgamate.sh

$(VENDOR_PREFIX)/unthread/bin/unthread.o:
	make CC="$(TARGETCC)" CFLAGS="$(CFLAGS)" -s -C $(VENDOR_PREFIX)/unthread bin/unthread.o

# These two rules clean before generating the library. This is because the target *may* have been different from our last call,
# so any intermediate object files may no longer be valid.

# This is a little funky. We request that all the header files are generated ahead of time, so that these may be preserved across
# targets. (As they are actually target-agnostic).
$(BUILD_PREFIX)/libgrapheme.a:  $(VENDOR_PREFIX)/libgrapheme/gen/bidirectional.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/bidirectional-test.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/case.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/character.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/character-test.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/line.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/line-test.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/sentence.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/sentence-test.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/word.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/word-test.h \
																$(VENDOR_PREFIX)/libgrapheme/gen/util.h
	# Remove intermiate object files.
	# We do this instead of make clean, bc we want the .h generated files.
	rm -f $(VENDOR_PREFIX)/libgrapheme/src/*.o

	make CC="$(TARGETCC)" -C $(VENDOR_PREFIX)/libgrapheme libgrapheme.a 
	# Copy the library in.
	mv $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libllhttp.a: $(VENDOR_PREFIX)/llhttp.h
	make clean -s -C $(VENDOR_PREFIX)/llhttp
	make CLANG="$(TARGETCC)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/libllhttp.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libbearssl.a:
	make clean -s -C $(VENDOR_PREFIX)/BearSSL
	make BUILD=$(BUILD_PREFIX) AR="zig ar" LD="$(TARGETCC)" CC="$(TARGETCC)" -s -C $(VENDOR_PREFIX)/BearSSL lib
	cp $(VENDOR_PREFIX)/BearSSL/$(BUILD_PREFIX)/libbearssl.a $(BUILD_PREFIX)/

# These are some convenience rules for making the cli simpler.

gab: $(BUILD_PREFIX)/gab

lib: $(BUILD_PREFIX)/libcgab.a

amalgamation: $(BUILD_PREFIX)/gab.c

cxxmodules: $(CXXMOD_SHARED)

cmodules: $(VENDOR_PREFIX)/ta.h $(CMOD_SHARED)

unthread: $(VENDOR_PREFIX)/unthread/bin/unthread.o

test: gab cmodules cxxmodules
	cp $(BUILD_PREFIX)/mod/* mod/
	$(BUILD_PREFIX)/gab run test

clean:
	rm -rf $(BUILD_PREFIX)*
	rm -f *.configuration
	rm -f cacert.pem
	rm -f etag.txt

clean-mod:
	make clean -C vendor/BearSSL
	make clean -C vendor/libgrapheme
	make clean -C vendor/llhttp
	make clean -C vendor/unthread

compile_commands:
	make clean
	bear -- make

.PHONY: clean


