# Renfuda (連札) card database
#
#   make test        build and run the core unit tests on the host (Linux)
#   make bench       100,000 card measurement (acceptance test 15)
#   make app         cross-build the RenkuOS/Haiku application
#   make package     app + presets, samples, themes, icons -> Renfuda.zip
#   make hpkg        installable package
#   make data        regenerate presets, samples, themes, JIS tables
#
# The cross build uses the RenkuOS tree in $(RENKU) (generated/ must exist).

HOST_CXX ?= g++
CXXFLAGS_COMMON = -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-maybe-uninitialized
BUILD = build

CORE_SRC = $(wildcard core/*.cpp)

# ---- host (core + tests) -----------------------------------------------------

HOST_OBJ = $(patsubst core/%.cpp,$(BUILD)/host/core/%.o,$(CORE_SRC))

$(BUILD)/host/core/%.o: core/%.cpp core/*.h core/*.inc
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS_COMMON) -Icore -c $< -o $@

$(BUILD)/host/libbdbcore.a: $(HOST_OBJ)
	ar rcs $@ $^

$(BUILD)/host/core_tests: tests/CoreTests.cpp tests/Test.h $(BUILD)/host/libbdbcore.a
	$(HOST_CXX) $(CXXFLAGS_COMMON) -Icore tests/CoreTests.cpp \
		$(BUILD)/host/libbdbcore.a -lpthread -lz -o $@

$(BUILD)/host/bench: tests/Bench.cpp $(BUILD)/host/libbdbcore.a
	$(HOST_CXX) $(CXXFLAGS_COMMON) -Icore tests/Bench.cpp \
		$(BUILD)/host/libbdbcore.a -lpthread -lz -o $@

$(BUILD)/host/csv_bench: tests/CsvBench.cpp $(BUILD)/host/libbdbcore.a
	$(HOST_CXX) $(CXXFLAGS_COMMON) -Icore tests/CsvBench.cpp \
		$(BUILD)/host/libbdbcore.a -lpthread -lz -o $@

csvbench: $(BUILD)/host/csv_bench
	$(BUILD)/host/csv_bench $(BUILD)/bench

$(BUILD)/host/make_samples: tools/MakeSamples.cpp $(BUILD)/host/libbdbcore.a
	$(HOST_CXX) $(CXXFLAGS_COMMON) -Icore tools/MakeSamples.cpp \
		$(BUILD)/host/libbdbcore.a -lpthread -lz -o $@

# Regenerates the bundled data (presets, samples, themes, tables).
data: $(BUILD)/host/make_samples
	python3 tools/make_themes.py
	python3 tools/make_presets.py
	$(BUILD)/host/make_samples tools/data/renfudasample.csv samples

.PHONY: test bench csvbench app package hpkg clean data

test: $(BUILD)/host/core_tests
	$(BUILD)/host/core_tests

bench: $(BUILD)/host/bench
	$(BUILD)/host/bench $(BUILD)/bench

# ---- cross build (RenkuOS / Haiku x86_64) ------------------------------------

RENKU ?= $(HOME)/renku
GEN = $(RENKU)/generated
CROSS_CXX = $(GEN)/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++
DEVEL = $(GEN)/objects/haiku/x86_64/packaging/packages_build/regular/hpkg_-haiku_devel.hpkg/contents/develop
HDR = $(DEVEL)/headers
REL = $(GEN)/objects/haiku/x86_64/release
GCCLIB = $(wildcard $(GEN)/build_packages/gcc_syslibs-*-x86_64/lib)
ZLIB = $(wildcard $(GEN)/build_packages/zlib-*-x86_64)
RC = $(GEN)/objects/linux/x86_64/release/tools/rc/rc
XRES = $(GEN)/objects/linux/x86_64/release/tools/xres
MIMESET = $(GEN)/objects/linux/x86_64/release/tools/mimeset

HAIKU_INC = -idirafter $(HDR) -idirafter $(HDR)/posix -idirafter $(HDR)/bsd \
	-idirafter $(HDR)/gnu -idirafter $(HDR)/os \
	$(foreach d,$(wildcard $(HDR)/os/*/),-idirafter $(d)) \
	-idirafter $(HDR)/os/add-ons/graphics \
	-idirafter $(HDR)/private/interface -idirafter $(HDR)/private/shared \
	-idirafter $(HDR)/private/netservices -idirafter $(ZLIB)/develop/headers
HAIKU_LIBDIRS = -B$(DEVEL)/lib -L$(REL)/kits -L$(REL)/kits/translation -L$(REL)/kits/game \
	-L$(REL)/kits/tracker -L$(REL)/kits/interface -L$(REL)/kits/shared \
	-L$(REL)/system/libroot -L$(GCCLIB) -L$(ZLIB)/develop/lib \
	-L$(REL)/kits/network/libnetapi/ssl -L$(REL)/kits/locale \
	-L$(DEVEL)/lib
HAIKU_LIBS = -lbe -lgame -ltranslation -ltracker -lnetservices -lshared -lbnetapi -lroot -lz

APP_SRC = $(wildcard app/*.cpp)
CROSS_CORE_OBJ = $(patsubst core/%.cpp,$(BUILD)/haiku/core/%.o,$(CORE_SRC))
CROSS_APP_OBJ = $(patsubst app/%.cpp,$(BUILD)/haiku/app/%.o,$(APP_SRC))

$(BUILD)/haiku/core/%.o: core/%.cpp core/*.h core/*.inc
	@mkdir -p $(dir $@)
	$(CROSS_CXX) $(CXXFLAGS_COMMON) $(HAIKU_INC) -Icore -c $< -o $@

$(BUILD)/haiku/app/%.o: app/%.cpp app/*.h core/*.h
	@mkdir -p $(dir $@)
	$(CROSS_CXX) $(CXXFLAGS_COMMON) $(HAIKU_INC) -Icore -Iapp -c $< -o $@

$(BUILD)/haiku/BDB.rsrc: app/BDB.rdef
	@mkdir -p $(dir $@)
	$(RC) -o $@ $<

$(BUILD)/haiku/Renfuda/Renfuda: $(CROSS_CORE_OBJ) $(CROSS_APP_OBJ) $(BUILD)/haiku/BDB.rsrc
	@mkdir -p $(dir $@)
	$(CROSS_CXX) $(CROSS_CORE_OBJ) $(CROSS_APP_OBJ) $(HAIKU_LIBDIRS) \
		$(HAIKU_LIBS) -o $@
	$(XRES) -o $@ $(BUILD)/haiku/BDB.rsrc

app: $(BUILD)/haiku/Renfuda/Renfuda

# Bundled data next to the executable: presets (ja, en), samples (ja, en),
# themes, icons.
DATA_DIRS = presets samples themes icons

package: app
	cd $(BUILD)/haiku/Renfuda && rm -rf $(DATA_DIRS)
	cp -r $(DATA_DIRS) $(BUILD)/haiku/Renfuda/
	cd $(BUILD)/haiku && rm -f Renfuda.zip && zip -qry Renfuda.zip Renfuda
	@echo "-> $(BUILD)/haiku/Renfuda.zip"

# ---- hpkg (pkgman install ./bdb-*.hpkg) --------------------------------------

PKG_VERSION = 0.5.0-1
PKG_TOOL = env LD_LIBRARY_PATH=$(GEN)/objects/linux/lib \
	$(GEN)/objects/linux/x86_64/release/tools/package/package
HPKG = $(BUILD)/renfuda-$(PKG_VERSION)-x86_64.hpkg
ADDATTR = env LD_LIBRARY_PATH=$(GEN)/objects/linux/lib \
	$(GEN)/objects/linux/x86_64/release/tools/addattr/addattr

hpkg: app
	rm -rf $(BUILD)/hpkg $(HPKG)
	mkdir -p $(BUILD)/hpkg/apps/Renfuda $(BUILD)/hpkg/data/deskbar/menu/Applications
	cp $(BUILD)/haiku/Renfuda/Renfuda $(BUILD)/hpkg/apps/Renfuda/Renfuda
	# Attributes Tracker shows (the zip cannot carry them; the application
	# sets them on its first start).
	$(ADDATTR) -t mime BEOS:TYPE application/x-vnd.Be-elfexecutable \
		$(BUILD)/hpkg/apps/Renfuda/Renfuda
	$(ADDATTR) -t mime BEOS:APP_SIG application/x-vnd.renfuda-app \
		$(BUILD)/hpkg/apps/Renfuda/Renfuda
	$(ADDATTR) -f app/Renfuda.hvif -c VICN BEOS:ICON \
		$(BUILD)/hpkg/apps/Renfuda/Renfuda
	cp -r $(DATA_DIRS) $(BUILD)/hpkg/apps/Renfuda/
	ln -s ../../../../apps/Renfuda/Renfuda \
		$(BUILD)/hpkg/data/deskbar/menu/Applications/Renfuda
	sed 's/@VERSION@/$(PKG_VERSION)/g' packaging/PackageInfo.in \
		> $(BUILD)/hpkg/.PackageInfo
	$(PKG_TOOL) create -C $(BUILD)/hpkg $(HPKG)
	@echo "-> $(HPKG)"

clean:
	rm -rf $(BUILD)
