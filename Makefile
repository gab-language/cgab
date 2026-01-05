CC  =zig cc
CXX =zig c++

# TODO: Proper defaults by detecting OS somehow
# ifndef GAB_TARGETS
# GAB_TARGETS = native
# endif
#
# ifndef GAB_CCFLAGS
# GAB_CCFLAGS = -g \
# 							-O0 \
# 							-fsanitize=address,undefined,leak,memory
# endif
#
# ifndef GAB_DYNLIB_FILEENDING
# GAB_DYNLIB_FILEENDING = .so
# endif

SRC_PREFIX 	 	 	= src/**
BUILD_PREFIX 	 	= build-$(GAB_TARGETS)
INCLUDE_PREFIX 	= include
VENDOR_PREFIX   = vendor

INCLUDE		= -I$(INCLUDE_PREFIX) -isystem$(VENDOR_PREFIX) -L$(BUILD_PREFIX)

CFLAGS = -std=c23 \
				 -fPIC \
				 -Wall \
				 -MMD  \
				 -fno-lto \
				 --target=$(GAB_TARGETS) \
				 -DGAB_TARGET_TRIPLE=\"$(GAB_TARGETS)\"\
				 -DGAB_DYNLIB_FILEENDING=\"$(GAB_DYNLIB_FILEENDING)\" \
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
GAB_LINK_DEPS = -lcgab
BINARY_FLAGS 	= -rdynamic -DGAB_CORE $(GAB_LINK_DEPS)

# A shared module needs undefined dynamic lookup
# As it is not linked with cgab. The symbols from cgab
# that these modules require will already exist,
# as they will be in the gab executable
CMOD_LINK_DEPS   = -lgrapheme -lllhttp -lbearssl

CXXMOD_LINK_DEPS = 
CXXMOD_INCLUDE   = 

CSHARED_FLAGS 	= -shared -undefined dynamic_lookup $(CMOD_LINK_DEPS)
CXXSHARED_FLAGS = -shared -undefined dynamic_lookup $(CXXMOD_LINK_DEPS) $(CXXMOD_INCLUDE)

# Source files in src/cgab are part of libcgab.
# Their object files are compiled and archived together into libcgab.a
CGAB_SRC = $(wildcard src/cgab/*.c)
CGAB_OBJ = $(CGAB_SRC:src/cgab/%.c=$(BUILD_PREFIX)/%.o)

# Source files in src/gab are part of gab's cli, which depends on libcgab
# They are compiled into an executable and linked statically
# with libcgab.a
GAB_SRC = $(wildcard src/gab/*.c)
GAB_OBJ = $(GAB_SRC:src/gab/%.c=$(BUILD_PREFIX)/%.o)

# Source files in src/mod/ are individual c-modules, importable from gab code.
# They are compiled individually into dynamic libraries, loaded at runtime.
CMOD_SRC 	 = $(wildcard src/mod/*.c)
CMOD_SHARED = $(CMOD_SRC:src/mod/%.c=$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING))

CXXMOD_SRC 	 = $(wildcard src/mod/*.cc)
CXXMOD_SHARED = $(CXXMOD_SRC:src/mod/%.cc=$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING))
all: gab cmodules cxxmodules

-include $(CGAB_OBJ:.o=.d) $(GAB_OBJ:.o=.d) $(CMOD_SHARED:$(GAB_DYNLIB_FILEENDING)=.d)

# This rule builds object files out of c source files
# Somewhat confusing that miniz amalgamation needs to be made here,
# but that is because the main *object file* is created before the executable,
# and the object file cannot compile without the header and impl from this.
$(BUILD_PREFIX)/%.o: $(SRC_PREFIX)/%.c $(VENDOR_PREFIX)/miniz/amalgamation/miniz.c
	$(CC) $(CFLAGS) $< -c -o $@

# This rule builds libcgab by archiving the cgab object files together.
$(BUILD_PREFIX)/libcgab.a: $(CGAB_OBJ)
	zig ar rcs $@ $^

# This rule builds the gab executable, linking with libcgab.a
$(BUILD_PREFIX)/gab: $(GAB_OBJ) 													\
							$(BUILD_PREFIX)/libcgab.a
	$(CC) $(CFLAGS) $(BINARY_FLAGS) $(GAB_OBJ) -o $@

# This rule builds each c++ module shared library.
$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.cc
	$(CXX) $(CXXFLAGS) $(CXXSHARED_FLAGS) $< -o $@

# This rule builds each c module shared library.
$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.c \
							$(BUILD_PREFIX)/libbearssl.a 	\
							$(BUILD_PREFIX)/libllhttp.a  	\
							$(BUILD_PREFIX)/libgrapheme.a \
							$(VENDOR_PREFIX)/sqlite3.c    \
							$(VENDOR_PREFIX)/duckdb.cc
	$(CC) $(CFLAGS) $(CSHARED_FLAGS) $< -o $@

# This curls a mozilla cert used for TLS clients.
cacert.pem:
	curl --etag-compare etag.txt --etag-save etag.txt --remote-name 'https://curl.se/ca/cacert.pem'

# This rule is the bear-ssl generated file equating to our certificate.
# Used in TLS clients.
$(VENDOR_PREFIX)/ta.h: cacert.pem
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/BearSSL
	$(VENDOR_PREFIX)/BearSSL/build/brssl ta cacert.pem > vendor/ta.h
	make clean -s -C $(VENDOR_PREFIX)/BearSSL

$(VENDOR_PREFIX)/sqlite3.c:
	mkdir -p $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX)
	cd $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX) && \
		../configure --enable-all
	make -s -C $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX) sqlite3.c
	cp $(VENDOR_PREFIX)/sqlite/$(BUILD_PREFIX)/sqlite3.c $(VENDOR_PREFIX)/

$(VENDOR_PREFIX)/duckdb.cc:
	cd $(VENDOR_PREFIX)/duckdb && \
		python scripts/amalgamation.py
	cp $(VENDOR_PREFIX)/duckdb/src/amalgamation/duckdb.cpp $(VENDOR_PREFIX)/duckdb.cc

# These rules generates header files for libgrapheme
$(VENDOR_PREFIX)/libgrapheme/gen/%.h: 
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/libgrapheme $<

$(VENDOR_PREFIX)/libgrapheme/gen2/%.h: 
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/libgrapheme $<

$(VENDOR_PREFIX)/llhttp.h:
	cd $(VENDOR_PREFIX)/llhttp && npm i
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/llhttp.h $(VENDOR_PREFIX)/
	make clean -s -C $(VENDOR_PREFIX)/llhttp

$(VENDOR_PREFIX)/miniz/amalgamation/miniz.c:
	cd $(VENDOR_PREFIX)/miniz && \
		./amalgamate.sh


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
																$(VENDOR_PREFIX)/libgrapheme/gen2/character.gen.h
	# Bit of a funky clean here. The makefile doesn't provide a way to clean *without* hitting the generated headers.
	find $(VENDOR_PREFIX)/libgrapheme -name "*.o" | xargs rm
	# Make for our given target.
	make libgrapheme.a CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/libgrapheme
	# Move the library in.
	mv $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libllhttp.a: $(VENDOR_PREFIX)/llhttp.h
	make clean -s -C $(VENDOR_PREFIX)/llhttp
	make CLANG="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/libllhttp.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libbearssl.a:
	make clean -s -C $(VENDOR_PREFIX)/BearSSL
	make lib AR="zig ar" LD="$(CC) --target=$(GAB_TARGETS)" CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/BearSSL
	mv $(VENDOR_PREFIX)/BearSSL/build/libbearssl.a $(BUILD_PREFIX)/

# These are some convenience rules for making the cli simpler.

gab: $(BUILD_PREFIX)/gab

lib: $(BUILD_PREFIX)/libcgab.a

cxxmodules: $(CXXMOD_SHARED)

cmodules: $(VENDOR_PREFIX)/ta.h $(CMOD_SHARED)

test: gab cmodules cxxmodules
	mv $(BUILD_PREFIX)/mod/* mod/
	$(BUILD_PREFIX)/gab run test

clean:
	rm -rf $(BUILD_PREFIX)*
	rm -f configuration
	rm -f cacert.pem
	rm -f etag.txt

compile_commands:
	make clean
	bear -- make

.PHONY: clean


