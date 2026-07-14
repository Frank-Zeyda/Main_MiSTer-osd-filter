# Host-side unit tests for the .showlist/.hidelist/.nomedia helpers in
# ../file_io.cpp. The tested functions are extracted VERBATIM from
# file_io.cpp into helpers.inc, so the suite exercises exactly the code
# that ships in the MiSTer binary. Runs with any host C++14 compiler:
#
#   make -C test           (build and run)
#   make -C test sanitize  (run the suite under AddressSanitizer and
#                           UndefinedBehaviorSanitizer)
#   make -C test coverage  (line/region coverage of the extracted helpers;
#                           requires clang with llvm-cov and llvm-profdata,
#                           found via xcrun on macOS)
#   make -C test clean

CXX ?= c++

# Coverage toolchain: use llvm-cov/llvm-profdata from PATH if available,
# otherwise fall back to xcrun (macOS command-line tools).
LLVM_PROFDATA ?= $(shell command -v llvm-profdata 2>/dev/null || echo xcrun llvm-profdata)
LLVM_COV ?= $(shell command -v llvm-cov 2>/dev/null || echo xcrun llvm-cov)

test: test_lists
	./test_lists

helpers.inc: ../file_io.cpp
	sed -n '/^\/\/ Returns a copy of s with leading and trailing whitespace removed\.$$/,/^int ScanDirectory/p' $< | sed '$$d' > $@
	@grep -q "entry_visible" $@ || \
		{ echo "ERROR: helper extraction from file_io.cpp failed (markers moved?)"; rm -f $@; exit 1; }

# FileReadLine() and its whitespace macros are extracted verbatim as well,
# so the suite cannot drift from the reader semantics the firmware uses.
filereadline.inc: ../file_io.cpp
	sed -n '/^\#define IS_NEWLINE/,/^}/p' $< > $@
	@grep -q "FileReadLine" $@ || \
		{ echo "ERROR: FileReadLine extraction from file_io.cpp failed (markers moved?)"; rm -f $@; exit 1; }

test_lists: test_lists.cpp helpers.inc filereadline.inc
	$(CXX) -std=gnu++14 -Wall -Wextra -o $@ $<

sanitize: test_lists.cpp helpers.inc filereadline.inc
	$(CXX) -std=gnu++14 -Wall -Wextra -g -fsanitize=address,undefined -fno-sanitize-recover=all -o test_lists_san test_lists.cpp
	./test_lists_san

coverage: test_lists.cpp helpers.inc filereadline.inc
	$(CXX) -std=gnu++14 -Wall -Wextra -fprofile-instr-generate -fcoverage-mapping -o test_lists_cov test_lists.cpp
	LLVM_PROFILE_FILE=test_lists.profraw ./test_lists_cov > /dev/null
	$(LLVM_PROFDATA) merge -sparse test_lists.profraw -o test_lists.profdata
	$(LLVM_COV) report ./test_lists_cov -instr-profile=test_lists.profdata helpers.inc
	$(LLVM_COV) show ./test_lists_cov -instr-profile=test_lists.profdata helpers.inc > coverage.txt
	@echo "Line-by-line coverage of the extracted helpers written to coverage.txt"

clean:
	rm -rf helpers.inc filereadline.inc test_lists testdir test_lists_cov test_lists_san test_lists.profraw test_lists.profdata coverage.txt

.PHONY: test sanitize coverage clean
