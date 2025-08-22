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
				 -MMD  \
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
GAB_LINK_DEPS = -lcgab -llinenoise -lc++
BINARY_FLAGS 	= -rdynamic -DGAB_CORE $(GAB_LINK_DEPS)

# A shared module needs undefined dynamic lookup
# As it is not linked with cgab. The symbols from cgab
# that these modules require will already exist,
# as they will be in the gab executable
CMOD_LINK_DEPS   = -lgrapheme -lllhttp -lbearssl
CXXMOD_LINK_DEPS = -larrow -larrow_bundled_dependencies
CXXMOD_INCLUDE   = -I$(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)/src/

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
$(BUILD_PREFIX)/%.o: $(SRC_PREFIX)/%.c
	$(CC) $(CFLAGS) $< -c -o $@

# This rule builds libcgab by archiving the cgab object files together.
$(BUILD_PREFIX)/libcgab.a: $(CGAB_OBJ)
	zig ar rcs $@ $^

# This rule builds the gab executable, linking with libcgab.a
$(BUILD_PREFIX)/gab: $(GAB_OBJ) $(BUILD_PREFIX)/libcgab.a $(BUILD_PREFIX)/liblinenoise.a $(VENDOR_PREFIX)/miniz/amalgamation/miniz.h
	$(CC) $(CFLAGS) $(BINARY_FLAGS) $(GAB_OBJ) -o $@

# This rule builds each c++ module shared library.
$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.cc \
							$(BUILD_PREFIX)/libarrow.a
	$(CXX) $(CXXFLAGS) $(CXXSHARED_FLAGS) $< -o $@

# This rule builds each c module shared library.
$(BUILD_PREFIX)/mod/%$(GAB_DYNLIB_FILEENDING): $(SRC_PREFIX)/%.c \
							$(BUILD_PREFIX)/libbearssl.a 	\
							$(BUILD_PREFIX)/libllhttp.a  	\
							$(BUILD_PREFIX)/libgrapheme.a
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

$(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX)/Makefile:
	mkdir -p $(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX)
	cd $(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX) && 	\
		CC="$(CC)" 																			\
		CXX="$(CXX)" 												 						\
		cmake .. 														 						\
		-DCMAKE_CXX_FLAGS=--target=$(GAB_TARGETS)	 			\
		-DCMAKE_C_FLAGS=--target=$(GAB_TARGETS) 		 		\
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5							\
		-DCMAKE_BUILD_TYPE=Release

$(BUILD_PREFIX)/liblinenoise.a: $(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX)/Makefile
	make -s -C $(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX)
	mv $(VENDOR_PREFIX)/linenoise/$(BUILD_PREFIX)/liblinenoise.a $(BUILD_PREFIX)/

# This rule uses cmake to generate a Makefile for apache-arrow.
# This is used to build libarrow.a and libarrow_bundled_dependencies.a
$(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)/Makefile:
	mkdir -p $(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)
	cd $(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX) && \
		CC="$(CC)" 													 									\
		CXX="$(CXX)" 												 									\
		cmake .. 														 									\
		-DCMAKE_CXX_FLAGS=--target=$(GAB_TARGETS)	 						\
		-DCMAKE_C_FLAGS=--target=$(GAB_TARGETS) 		 				 	\
		-DARROW_MIMALLOC=OFF 								 									\
		-DARROW_ENABLE_THREADING=OFF 				 									\
		-DARROW_DEPENDENCY_SOURCE=BUNDLED 	 									\
		-DARROW_ACERO=ON 										 									\
		-DARROW_BUILD_SHARED=OFF 						 									\
		-DARROW_COMPUTE=ON 									 									\
		-DARROW_CSV=ON

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

$(VENDOR_PREFIX)/miniz/amalgamation/miniz.h:
	cd $(VENDOR_PREFIX)/miniz
	./amalgamation.sh

$(BUILD_PREFIX)/libgrapheme.a:
	rm -f $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(VENDOR_PREFIX)/libgrapheme/src/*.o
	make libgrapheme.a CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/libgrapheme
	mv $(VENDOR_PREFIX)/libgrapheme/libgrapheme.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libllhttp.a:
	make clean -s -C $(VENDOR_PREFIX)/llhttp
	make CLANG="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/llhttp
	mv $(VENDOR_PREFIX)/llhttp/build/libllhttp.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libbearssl.a:
	make clean -s -C $(VENDOR_PREFIX)/BearSSL
	make lib AR="zig ar" LD="$(CC) --target=$(GAB_TARGETS)" CC="$(CC) --target=$(GAB_TARGETS)" -s -C $(VENDOR_PREFIX)/BearSSL
	mv $(VENDOR_PREFIX)/BearSSL/build/libbearssl.a $(BUILD_PREFIX)/

$(BUILD_PREFIX)/libarrow.a: $(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)/Makefile
	make -j 4 -s -C $(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)
	mv $(VENDOR_PREFIX)/apache-arrow/cpp/$(BUILD_PREFIX)/release/*.a $(BUILD_PREFIX)/

# These are some convenience rules for making the cli simpler.

gab: $(BUILD_PREFIX)/gab

lib: $(BUILD_PREFIX)/libcgab.a

cxxmodules: $(CXXMOD_SHARED)

cmodules: $(VENDOR_PREFIX)/ta.h libllhttp_generated libgrapheme_generated $(CMOD_SHARED)

test: gab cmodules cxxmodules
	mv $(BUILD_PREFIX)/mod/* mod/
	$(BUILD_PREFIX)/gab run test

clean:
	rm -rf $(BUILD_PREFIX)
	rm -f configuration
	rm -f libgrapheme_generated
	rm -f libllhttp_generated
	rm -f cacert.pem
	rm -f etag.txt

compile_commands:
	make clean
	bear -- make

.PHONY: clean


