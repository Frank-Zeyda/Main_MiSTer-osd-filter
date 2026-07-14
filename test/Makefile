# Host-side unit tests for the .showlist/.hidelist/.nomedia helpers in
# ../file_io.cpp. The tested functions are extracted VERBATIM from
# file_io.cpp into helpers.inc, so the suite exercises exactly the code
# that ships in the MiSTer binary. Runs with any host C++14 compiler:
#
#   make -C test        (build and run)
#   make -C test clean

CXX ?= c++

test: test_lists
	./test_lists

helpers.inc: ../file_io.cpp
	sed -n '/^\/\/ Returns a copy of s with leading and trailing whitespace removed\.$$/,/^int ScanDirectory/p' $< | sed '$$d' > $@
	@grep -q "entry_visible" $@ || \
		{ echo "ERROR: helper extraction from file_io.cpp failed (markers moved?)"; rm -f $@; exit 1; }

test_lists: test_lists.cpp helpers.inc
	$(CXX) -std=gnu++14 -Wall -Wextra -o $@ $<

clean:
	rm -rf helpers.inc test_lists testdir

.PHONY: test clean
