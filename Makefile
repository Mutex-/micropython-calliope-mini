ECHO = @echo

TARGET_BUILD = build/bbc-microbit-classic-gcc-nosd
HEX_SRC = $(TARGET_BUILD)/source/microbit-micropython.hex
HEX_FINAL = build/firmware.hex
MBIT_VER_FILE = inc/genhdr/microbitversion.h
VER_ADDR_FILE = build/veraddr.txt

# The qstr table is regenerated whenever a source or header changes. The stamp
# lives outside $(TARGET_BUILD), which updateqstrs.sh removes when the table
# changes.
QSTR_STAMP = build/qstrs.stamp
QSTR_DEPS := $(shell find source inc/microbit -name '*.c' -o -name '*.cpp' -o -name '*.h') inc/py/qstrdefs.h

all: $(HEX_FINAL)

# Anything that depends on FORCE will be considered out-of-date
FORCE:
.PHONY: FORCE

$(HEX_FINAL): yotta $(VER_ADDR_FILE)
	tools/adduicr.py $(HEX_SRC) 0x$$(cat $(VER_ADDR_FILE)) -o $(HEX_FINAL)
	@arm-none-eabi-size $(HEX_SRC:.hex=)

yotta: fix-depfile-flags $(MBIT_VER_FILE) $(QSTR_STAMP)
	@yotta build

# yotta's mbed-gcc target sets the dependency-file flags with CMake's pre-3.20
# placeholder <DEPFILE>. CMake 3.20+ doesn't expand it, so every compile wrote
# its header list to one file literally named "DEPFILE", ninja recorded no
# header dependencies, and changing a header recompiled nothing. Switch the
# downloaded target to the current placeholder (it's re-downloaded by yotta, so
# this runs before every build), and rebuild from clean the first time.
MBED_GCC_COMPILER_FILES = $(wildcard yotta_targets/mbed-gcc/CMake/Compiler/GNU-*.cmake)

.PHONY: fix-depfile-flags
fix-depfile-flags:
	@for f in $(MBED_GCC_COMPILER_FILES); do \
	    if grep -q '<DEPFILE>' "$$f"; then \
	        sed -i 's/<DEPFILE>/<DEP_FILE>/' "$$f"; \
	        echo "fixed header dependency tracking in $$f, removing $(TARGET_BUILD)"; \
	        rm -rf $(TARGET_BUILD); \
	    fi; \
	done

# The scan preprocesses every source, and modos.c includes the generated
# version header, so that must exist first. Order-only (after the |): the
# version header is rewritten on every build and must not retrigger the scan.
$(QSTR_STAMP): $(QSTR_DEPS) | $(MBIT_VER_FILE)
	@tools/updateqstrs.sh $(TARGET_BUILD)
	@mkdir -p $(@D) && touch $@

# Regenerate the qstr table now, even if no source changed.
.PHONY: qstrs
qstrs: $(MBIT_VER_FILE)
	@tools/updateqstrs.sh $(TARGET_BUILD)
	@mkdir -p $(dir $(QSTR_STAMP)) && touch $(QSTR_STAMP)

$(MBIT_VER_FILE): FORCE
	python tools/makeversionhdr.py $(MBIT_VER_FILE)

$(VER_ADDR_FILE): yotta
	@arm-none-eabi-objdump -x $(HEX_SRC:.hex=) | grep microbit_version_string | cut -f 1 -d' ' > $(VER_ADDR_FILE)

deploy: $(HEX_FINAL)
	$(ECHO) "Deploying $<"
	@mount /dev/sdb
	@sleep 1s
	@cp $< /mnt/
	@sleep 1s
	@umount /mnt

serial:
	@picocom /dev/ttyACM0 -b 115200
