CC  =zig cc
CXX =zig c++

SRC_PREFIX 	 	 	= src/**
BUILD_PREFIX 	 	= build-$(GAB_TARGETS)
INCLUDE_PREFIX 	= include
VENDOR_PREFIX   = vendor

INCLUDE		= -I$(INCLUDE_PREFIX) -isystem$(VENDOR_PREFIX) -L$(BUILD_PREFIX)

CFLAGS = -std=c23 \
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
LINK_CGAB = -lcgab
BINARY_FLAGS = -rdynamic -DGAB_CORE $(LINK_CGAB)

# A shared module needs undefined dynamic lookup
# As it is not linked with cgab. The symbols from cgab
# that these modules require will already exist,
# as they will be in the gab executable
LINK_DEPS    = -lgrapheme -lllhttp -lbearssl
SHARED_FLAGS = -shared -undefined dynamic_lookup $(LINK_DEPS)

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
CMOD_SHARED = $(CMOD_SRC:src/mod/%.c=$(BUILD_PREFIX)/mod/%.so)

all: gab cmodules

# This rule builds object files out of source files, recursively (for all c files in src/**)
$(BUILD_PREFIX)/%.o: $(SRC_PREFIX)/%.c
	$(CC) $(CFLAGS) $< -c -o $@

# This rule builds libcgab by archiving the cgab object files together.
$(BUILD_PREFIX)/libcgab.a: $(CGAB_OBJ)
	zig ar rcs $@ $^

# This rule builds the gab executable, linking with libcgab.a
$(BUILD_PREFIX)/gab: $(GAB_OBJ) $(BUILD_PREFIX)/libcgab.a
	$(CC) $(CFLAGS) $(BINARY_FLAGS) $(GAB_OBJ) -o $@

# This rule builds each c-module shared library.
$(BUILD_PREFIX)/mod/%.so: $(SRC_PREFIX)/%.c \
							$(BUILD_PREFIX)/libbearssl.a 	\
							$(BUILD_PREFIX)/libllhttp.a  	\
							$(BUILD_PREFIX)/libgrapheme.a
	$(CC) $(CFLAGS) $(SHARED_FLAGS) $< -o $@

cacert.pem:
	curl --etag-compare etag.txt --etag-save etag.txt --remote-name 'https://curl.se/ca/cacert.pem'

$(VENDOR_PREFIX)/ta.h: cacert.pem
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/BearSSL
	$(VENDOR_PREFIX)/BearSSL/build/brssl ta cacert.pem > vendor/ta.h
	make clean -s -C $(VENDOR_PREFIX)/BearSSL

# This rule generates libgraphemes code to be later compiled
# to specific targets
libgrapheme_generated: 
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/libgrapheme
	echo "libgrapheme generation done." >> libgrapheme_generated
	# Clean up native outputs, but leave our generated files.
	rm $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(VENDOR_PREFIX)/libgrapheme/src/*.o

# This rule generates the header file for llhttp
# To be later compiled to specific targets
libllhttp_generated:
	make CC="$(CC)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/llhttp.h $(VENDOR_PREFIX)/
	echo "libllhttp generation done." >> libllhttp_generated
	make clean -s -C $(VENDOR_PREFIX)/llhttp

$(BUILD_PREFIX)/libgrapheme.a:
	make libgrapheme.a CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/libgrapheme
	mv $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libllhttp.a:
	make CLANG="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/libllhttp.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libbearssl.a:
	make lib AR="zig ar" LD="$(CC) --target=$(GAB_TARGETS)" CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/BearSSL
	mv $(VENDOR_PREFIX)/BearSSL/build/libbearssl.a $(BUILD_PREFIX)/

# These are some convenience rules for making the cli simpler.

gab: $(BUILD_PREFIX)/gab

lib: $(BUILD_PREFIX)/libcgab.a

cmodules: $(VENDOR_PREFIX)/ta.h libllhttp_generated libgrapheme_generated $(CMOD_SHARED)

test: gab cmodules
	mv $(BUILD_PREFIX)/mod/* mod/
	$(BUILD_PREFIX)/gab run test

clean:
	make clean -s -C $(VENDOR_PREFIX)/libgrapheme
	make clean -s -C $(VENDOR_PREFIX)/llhttp
	make clean -s -C $(VENDOR_PREFIX)/BearSSL
	rm -rf $(BUILD_PREFIX)
	rm configuration
	rm libgrapheme_generated
	rm libllhttp_generated
	rm cacert.pem
	rm etag.txt

compile_commands:
	make clean
	bear -- make

.PHONY: clean


