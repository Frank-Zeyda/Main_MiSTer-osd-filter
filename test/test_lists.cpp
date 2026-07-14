/*
 * Host-side unit tests for the .showlist/.hidelist/.nomedia helper logic
 * of ../file_io.cpp.
 *
 * Test context
 * ------------
 * The functions under test are extracted VERBATIM from ../file_io.cpp into
 * helpers.inc by the accompanying Makefile, so this suite exercises exactly
 * the code that ships in the MiSTer binary — including entry_visible(), the
 * per-entry decision function used by ScanDirectory().
 *
 * The MiSTer file-I/O facilities used by that code are treated as follows:
 * FileReadLine() and its whitespace macros are ALSO extracted verbatim from
 * ../file_io.cpp (filereadline.inc), so the reader semantics cannot drift;
 * FileOpenTextReader() and FileExists() are host stand-ins below with the
 * same observable semantics as the originals (they cannot be extracted, as
 * they depend on the firmware's fileTYPE machinery).
 *
 * Conventions
 * -----------
 * Each test case is a function named test_*, preceded by a comment block
 * documenting:
 *
 *   Description   - the property being verified
 *   Context       - why the property matters for the OSD file browser
 *   Preconditions - required initial state (fixture files, environment)
 *   Inputs        - the concrete inputs exercised
 *   Expected      - expected outputs and/or state changes
 *   Abnormal      - error/edge behaviour deliberately provoked (if any)
 *
 * Test cases are self-contained: each one (re)creates the shared fixture
 * directory testdir/ as documented in its Preconditions and never relies
 * on files written by another test case. Run via: make -C test
 */
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>

/* ---- host stand-ins for the codebase facilities used by the helpers ---- */

struct fileTextReader
{
	fileTextReader() : size(0), buffer(nullptr), pos(nullptr) {}
	~fileTextReader() { if (buffer) free(buffer); }

	size_t size;
	char *buffer;
	char *pos;
};

/* Mimics file_io.cpp's FileOpenTextReader(), including its quirk of
 * failing on an existing but empty file (FileReadAdv() returns 0). */
static bool FileOpenTextReader(fileTextReader *reader, const char *filename)
{
	FILE *f = fopen(filename, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = (char*)malloc(size + 1);
	if (!buf) { fclose(f); return false; }
	memset(buf, 0, size + 1);
	long rd = (long)fread(buf, 1, size, f);
	fclose(f);
	if (!rd) { free(buf); return false; }
	reader->size = size;
	reader->buffer = buf;
	reader->pos = buf;
	return true;
}

/* FileReadLine() and the IS_NEWLINE/IS_WHITESPACE macros, extracted
 * verbatim from ../file_io.cpp by the Makefile. */
#include "filereadline.inc"

static int FileExists(const char *name, int use_zip = 1)
{
	(void)use_zip;
	struct stat st;
	return !stat(name, &st) && S_ISREG(st.st_mode);
}

#include "helpers.inc"

/* ---- test scaffolding ---- */

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg) do { \
	checks++; \
	if (!(cond)) { failures++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } \
	else { printf("ok:   %s\n", msg); } \
} while (0)

/* Prints a banner identifying the running test case. */
static void begin_test(const char *name)
{
	printf("\n--- %s ---\n", name);
}

/* (Re)creates an empty fixture directory testdir/. */
static void reset_fixture()
{
	system("rm -rf testdir && mkdir -p testdir");
}

/* Removes the fixture directory. */
static void remove_fixture()
{
	system("rm -rf testdir");
}

/* Creates a file with the given (binary-exact) content. */
static void write_file(const std::string& path, const std::string& content)
{
	FILE *f = fopen(path.c_str(), "wb");
	fwrite(content.data(), 1, content.size(), f);
	fclose(f);
}

/* Test utility mirroring read_list()'s per-line pipeline: a single list
 * line is trimmed and escaped, then regex-matched against a full name.
 * Returns false (rather than aborting) for invalid regex patterns. */
static bool matches(const char *line, const char *name)
{
	std::string pattern = escape_special(trim(line));
	if (pattern.empty()) return false;
	try {
		return std::regex_match(name, std::regex(pattern));
	} catch (std::regex_error&) {
		return false;
	}
}

/*
 * Test case: test_trim
 *
 * Description   - trim() returns a copy of its argument with leading and
 *                 trailing whitespace removed and interior whitespace kept.
 * Context       - every line of a .showlist/.hidelist is trimmed before
 *                 escaping, so patterns must tolerate editor padding and
 *                 carriage returns (FileReadLine() strips \r\n at line
 *                 breaks, but a lone trailing \r must not reach the regex).
 * Preconditions - none (pure function).
 * Inputs        - strings with spaces/tabs/CR at either end, interior
 *                 whitespace, an empty string, an all-whitespace string.
 * Expected      - padding removed; interior whitespace preserved; empty
 *                 and all-whitespace inputs yield the empty string.
 * Abnormal      - empty/all-whitespace inputs must not crash (an earlier
 *                 implementation had undefined pointer arithmetic for "").
 */
static void test_trim()
{
	begin_test("trim");
	CHECK(trim("  hello  ") == "hello", "strips spaces both sides");
	CHECK(trim("hello") == "hello", "leaves clean string alone");
	CHECK(trim("") == "", "handles empty string");
	CHECK(trim("   \t ") == "", "handles all-whitespace string");
	CHECK(trim("name\r") == "name", "strips trailing CR");
	CHECK(trim("\ta b\t") == "a b", "keeps interior whitespace");
}

/*
 * Test case: test_escape_literal
 *
 * Description   - escape_special() escapes every ECMAScript regex
 *                 metacharacter outside backtick regions, so that plain
 *                 lines match file names literally.
 * Context       - ROM names are full of ()[]{}.+ etc.; users writing plain
 *                 names into a list must not trip over regex semantics.
 * Preconditions - none (pure function).
 * Inputs        - names containing ( ) [ ] { } . * + ? ^ $ and a
 *                 backslash — every character escape_special() escapes.
 * Expected      - each name regex-matches itself and only itself; regex
 *                 metacharacters have no special effect.
 * Abnormal      - none.
 */
static void test_escape_literal()
{
	begin_test("escape_special: literal by default");
	CHECK(matches("Sonic (USA) [!].bin", "Sonic (USA) [!].bin"),
		"name with ()[]. matches itself");
	CHECK(!matches("file.txt", "fileAtxt"),
		"dot is escaped (no wildcard meaning)");
	CHECK(!matches("Sonic (USA) [!].bin", "Sonic (USA) [x].bin"),
		"brackets are escaped (no char-class meaning)");
	CHECK(matches("C++ (v1.2) {beta}", "C++ (v1.2) {beta}"),
		"plus, parens, braces escaped");
	CHECK(!matches("game*", "gamex"),
		"star is escaped (no repetition meaning)");
	CHECK(matches("game*", "game*"),
		"star matches literal star");
	CHECK(matches("a\\b", "a\\b"),
		"backslash is escaped (matches a literal backslash)");
	CHECK(!matches("a\\b", "ab"),
		"escaped backslash does not act as a regex escape");
	CHECK(matches("^start$", "^start$"),
		"caret and dollar are escaped (match literally)");
	CHECK(!matches("^start$", "start"),
		"escaped caret/dollar do not act as anchors");
	CHECK(matches("what?", "what?"),
		"question mark is escaped (matches literally)");
	CHECK(!matches("what?", "wha"),
		"escaped question mark does not make 't' optional");
}

/*
 * Test case: test_escape_regex_regions
 *
 * Description   - content between backticks passes through unescaped, so
 *                 raw ECMAScript regex fragments can be embedded in a line;
 *                 the backticks themselves are dropped from the pattern.
 * Context       - this is the feature's "advanced mode" (cf. issue #443):
 *                 e.g. `.*\.mra` in a .hidelist hides all MRA files.
 * Preconditions - none (pure function).
 * Inputs        - lines mixing literal text with one or more `...` regions,
 *                 an unmatched (unterminated) backtick, adjacent backticks,
 *                 a 10000-character line.
 * Expected      - regex constructs work inside regions and are literal
 *                 outside; mixed lines anchor the literal part exactly.
 * Abnormal      - an unmatched backtick opens a region that extends to the
 *                 end of the line (documented behaviour, not an error); a
 *                 line of two backticks escapes to the empty pattern.
 */
static void test_escape_regex_regions()
{
	begin_test("escape_special: backtick regex regions");
	CHECK(matches("`.*`.bin", "foo.bin"),
		"region passes .* through as regex");
	CHECK(!matches("`.*`.bin", "foo_bin"),
		"dot outside region remains literal");
	CHECK(matches("Super `.*`", "Super Mario World.sfc"),
		"literal prefix plus regex suffix");
	CHECK(!matches("Super `.*`", "Duper Mario"),
		"literal prefix must match exactly");
	CHECK(matches("`(foo|bar)` fighters", "bar fighters"),
		"alternation inside region");
	CHECK(escape_special("a`b`c") == "abc",
		"backticks themselves are dropped");
	CHECK(matches("`.*", "anything at all"),
		"unmatched backtick: region extends to end of line");
	CHECK(escape_special("``") == "",
		"two adjacent backticks yield the empty pattern");
	std::string longline(10000, 'x');
	CHECK(escape_special(longline).length() == 10000,
		"no length limit on patterns");
}

/* Deterministic pseudo-random generator (LCG) for the property test:
 * a fixed seed keeps every run reproducible. */
static unsigned lcg_next(unsigned& state)
{
	state = state * 1664525u + 1013904223u;
	return state >> 16;
}

/*
 * Test case: test_literal_identity_property
 *
 * Description   - property-based check of the feature's core invariant:
 *                 for ANY name not containing a backtick, escape_special()
 *                 yields a regex that matches exactly that name — no more,
 *                 no less (literal identity).
 * Context       - the per-character tests above verify each metacharacter
 *                 individually; this sweeps the whole printable-ASCII
 *                 space in combination, catching any escape that was not
 *                 thought of explicitly.
 * Preconditions - none (pure functions; fixed PRNG seed 20260713 makes
 *                 the run fully reproducible).
 * Inputs        - 2000 pseudo-random names of 1..24 printable ASCII
 *                 characters (backticks excluded by construction).
 * Expected      - every name matches its own escaped pattern, every
 *                 pattern compiles, and appending one character always
 *                 breaks the (full-name) match.
 * Abnormal      - a std::regex_error during compilation counts as a
 *                 property violation (tallied, reported via the checks).
 */
static void test_literal_identity_property()
{
	begin_test("property: literal identity for backtick-free names");
	unsigned state = 20260713u;
	const int N = 2000;
	int compiled = 0, matched = 0, longer_rejected = 0;
	for (int i = 0; i < N; i++)
	{
		int len = 1 + lcg_next(state) % 24;
		std::string name;
		for (int j = 0; j < len; j++)
		{
			char c = (char)(32 + lcg_next(state) % 95); /* printable ASCII */
			if (c == '`') c = 'x';                      /* exclude backticks */
			name += c;
		}
		try
		{
			std::regex re(escape_special(name));
			compiled++;
			if (std::regex_match(name, re)) matched++;
			if (!std::regex_match(name + "!", re)) longer_rejected++;
		}
		catch (std::regex_error&) { /* counted via compiled */ }
	}
	CHECK(compiled == N, "every escaped name compiles as a regex");
	CHECK(matched == N, "every name matches its own escaped pattern");
	CHECK(longer_rejected == N, "appending a character always breaks the match");
}

/*
 * Test case: test_read_list_format
 *
 * Description   - read_list() (via read_showlist()) parses a list file
 *                 into compiled regex patterns, applying the documented
 *                 file format: UTF-8 BOM tolerated, CRLF line endings,
 *                 #/; comment lines, blank lines, whitespace-padded
 *                 entries, and backtick regex lines.
 * Context       - list files are typically created on a PC (Windows
 *                 editors add BOM and CRLF) and copied over Samba; the
 *                 format must survive that path unchanged.
 * Preconditions - fixture testdir/ freshly created by this test; a
 *                 .showlist written with a representative mix of lines.
 * Inputs        - .showlist containing: BOM prefix, a CRLF literal line,
 *                 blank and whitespace-only lines, '#' and ';' comments,
 *                 a raw-regex line, and a padded literal line.
 * Expected      - the file is reported present; exactly the 3 pattern
 *                 lines survive; the first pattern matches its name,
 *                 proving the BOM was stripped rather than glued on.
 * Abnormal      - none (abnormal lines are covered by the next case).
 */
static void test_read_list_format()
{
	begin_test("read_list: documented file format");
	reset_fixture();
	write_file("testdir/.showlist",
		"\xEF\xBB\xBF"                /* UTF-8 BOM (Windows editors) */
		"Sonic (USA).bin\r\n"         /* CRLF line ending */
		"\n"                          /* blank line: skipped */
		"   \n"                       /* whitespace-only: skipped */
		"# a comment line\n"          /* comment: skipped */
		"  ; another comment\n"       /* comment after whitespace */
		"`.*\\.(gg|sms)`\n"           /* raw regex line */
		"  Mario  \n");               /* padded literal */
	std::vector<std::regex> v;
	CHECK(read_showlist("testdir", v), "file reported present");
	CHECK(v.size() == 3, "exactly 3 patterns survive (BOM/comments/blank skipped)");
	CHECK(v.size() >= 1 && std::regex_match("Sonic (USA).bin", v[0]),
		"BOM stripped from first pattern");
	CHECK(v.size() >= 2 && std::regex_match("game.gg", v[1]),
		"raw regex line compiled and matching");
	CHECK(v.size() >= 3 && std::regex_match("Mario", v[2]),
		"padded literal trimmed and matching");
	remove_fixture();
}

/*
 * Test case: test_read_list_abnormal
 *
 * Description   - read_list() copes with abnormal inputs: absent files,
 *                 empty files, invalid regex lines, lines that escape to
 *                 an empty pattern, very long lines, and tiny files.
 * Context       - lists are hand-written; a single bad line must neither
 *                 crash the firmware nor knock out the rest of the file
 *                 (an earlier implementation silently dropped everything
 *                 after an over-long line).
 * Preconditions - fixture testdir/ freshly created by this test; list
 *                 files written per the inputs below.
 * Inputs        - (a) no .hidelist at all; (b) a zero-byte .hidelist;
 *                 (c) a .showlist whose lines are: an invalid raw regex
 *                 `[`, a line of two backticks, an 8000-character literal,
 *                 and a normal line after those; (d) a 2-byte file "a\n"
 *                 (shorter than a BOM).
 * Expected      - (a) reported not present, no patterns; (b) reported
 *                 PRESENT with zero patterns — an empty whitelist hides
 *                 everything, preserving the pre-refactor semantics;
 *                 (c) the invalid and empty-pattern lines are skipped
 *                 while the 8000-char line and the following line both
 *                 survive; (d) parsed as one pattern.
 * Abnormal      - the invalid `[` line additionally causes a diagnostic
 *                 on stdout ("Invalid pattern in ..."), mirroring what
 *                 the firmware writes to the MiSTer log. This warning in
 *                 the test output is EXPECTED and not a failure.
 */
static void test_read_list_abnormal()
{
	begin_test("read_list: abnormal inputs");
	reset_fixture();
	std::vector<std::regex> v;

	CHECK(!read_hidelist("testdir", v), "absent file reported not present");
	CHECK(v.empty(), "absent file yields no patterns");

	write_file("testdir/.hidelist", "");
	CHECK(read_hidelist("testdir", v), "empty file still counts as present");
	CHECK(v.empty(), "empty file yields no patterns");

	printf("NOTE: the following 'Invalid pattern' warning is intentional:\n");
	std::string longname(8000, 'x');
	write_file("testdir/.showlist",
		"`[`\n"                       /* invalid regex: skipped, warns */
		"``\n"                        /* escapes to empty pattern: skipped */
		+ longname + "\n"             /* 8000-char literal line */
		"afterwards\n");              /* must still be parsed */
	CHECK(read_showlist("testdir", v), "file with bad lines reported present");
	CHECK(v.size() == 2, "invalid and empty-pattern lines skipped, others kept");
	CHECK(v.size() >= 1 && std::regex_match(longname, v[0]),
		"8000-char line parsed in full");
	CHECK(v.size() >= 2 && std::regex_match("afterwards", v[1]),
		"line after the long line still read");

	write_file("testdir/.hidelist", "a\n");
	CHECK(read_hidelist("testdir", v) && v.size() == 1,
		"2-byte file (shorter than a BOM) parsed as one pattern");
	remove_fixture();
}

/*
 * Test case: test_has_nomedia
 *
 * Description   - has_nomedia() reports whether a directory contains a
 *                 .nomedia marker file, judging by existence only.
 * Context       - .nomedia is the zero-cost hiding variant suggested by
 *                 the upstream maintainer in issue #443; it must work
 *                 with an empty file created via `touch`.
 * Preconditions - fixture testdir/ freshly created by this test.
 * Inputs        - testdir/ without a marker, with a zero-byte marker,
 *                 with a non-empty marker; a non-existent directory.
 * Expected      - false / true / true / false respectively: content is
 *                 ignored, only existence matters.
 * Abnormal      - a missing directory is indistinguishable from a missing
 *                 marker (false), by design.
 */
static void test_has_nomedia()
{
	begin_test("has_nomedia");
	reset_fixture();
	CHECK(!has_nomedia("testdir"), "no marker: false");
	write_file("testdir/.nomedia", "");
	CHECK(has_nomedia("testdir"), "empty (touch'ed) marker detected");
	write_file("testdir/.nomedia", "arbitrary content\n");
	CHECK(has_nomedia("testdir"), "marker content is ignored");
	CHECK(!has_nomedia("no_such_dir_xyz"), "missing directory: false");
	remove_fixture();
}

/*
 * Test case: test_entry_visible_showlist
 *
 * Description   - with only a .showlist present, entry_visible() shows an
 *                 entry iff it matches at least one pattern; the parent
 *                 entry ".." is exempt from filtering.
 * Context       - whitelist semantics: unlisted files AND folders vanish
 *                 from the OSD, but upward navigation must always work.
 * Preconditions - fixture testdir/ freshly created with a .showlist of
 *                 two literals and one raw-regex pattern; no .hidelist,
 *                 no .nomedia.
 * Inputs        - listed literal, regex-matched name, padded literal,
 *                 unlisted file, unlisted directory, "..".
 * Expected      - read_filters() flags exactly the showlist as present;
 *                 listed/matched entries are visible; the unlisted file
 *                 and directory are hidden; ".." stays visible.
 * Abnormal      - none.
 */
static void test_entry_visible_showlist()
{
	begin_test("entry_visible: showlist only");
	reset_fixture();
	write_file("testdir/.showlist",
		"Sonic (USA).bin\n"
		"`.*\\.(gg|sms)`\n"
		"  Mario  \n");
	dir_filters f;
	read_filters("testdir", f);
	CHECK(f.showlist_present && !f.hidelist_present && !f.nomedia_present,
		"read_filters flags exactly the present file");
	CHECK(entry_visible(f, "Sonic (USA).bin", DT_REG), "listed literal visible");
	CHECK(entry_visible(f, "game.gg", DT_REG), "regex-matched file visible");
	CHECK(entry_visible(f, "Mario", DT_REG), "padded literal visible");
	CHECK(!entry_visible(f, "Zelda.n64", DT_REG), "unlisted file hidden");
	CHECK(!entry_visible(f, "unlisted_dir", DT_DIR), "unlisted folder hidden");
	CHECK(entry_visible(f, "..", DT_DIR), "parent entry ('..') never hidden");
	remove_fixture();
}

/*
 * Test case: test_entry_visible_hidelist
 *
 * Description   - .hidelist entries are always hidden, overriding a
 *                 .showlist match; with only a .hidelist present,
 *                 everything unlisted stays visible.
 * Context       - blacklist semantics and their precedence over the
 *                 whitelist, as documented in the README.
 * Preconditions - fixture testdir/ freshly created; first with both lists
 *                 (the showlist admits *.gg and *.sms via regex, and the
 *                 hidelist hides the same class), then with the .showlist
 *                 removed.
 * Inputs        - a file matched by both lists, a file matched only by
 *                 the showlist, an unlisted file, a hidden folder name.
 * Expected      - both lists: the doubly-matched file is hidden, the
 *                 showlist-only file visible. Hidelist only: unlisted
 *                 entries visible by default, the listed folder hidden.
 * Abnormal      - none.
 */
static void test_entry_visible_hidelist()
{
	begin_test("entry_visible: hidelist overrides / hidelist only");
	reset_fixture();
	write_file("testdir/.showlist",
		"Sonic (USA).bin\n"
		"`.*\\.(gg|sms)`\n");
	write_file("testdir/.hidelist",
		"`.*\\.(gg|sms)`\n"
		"secret folder\n");
	dir_filters f;
	read_filters("testdir", f);
	CHECK(f.showlist_present && f.hidelist_present, "both lists found");
	CHECK(!entry_visible(f, "game.gg", DT_REG),
		"hidelist overrides showlist match");
	CHECK(entry_visible(f, "Sonic (USA).bin", DT_REG),
		"file not matching hide patterns stays visible");

	system("rm -f testdir/.showlist");
	dir_filters f2;
	read_filters("testdir", f2);
	CHECK(!f2.showlist_present && f2.hidelist_present, "only .hidelist present");
	CHECK(entry_visible(f2, "anything.rom", DT_REG),
		"hidelist only: unlisted entries visible by default");
	CHECK(!entry_visible(f2, "secret folder", DT_DIR),
		"hidelist only: listed folder hidden");
	remove_fixture();
}

/*
 * Test case: test_entry_visible_nomedia
 *
 * Description   - a .nomedia marker hides all regular files while leaving
 *                 subfolders and ".." visible; it takes precedence over a
 *                 .showlist (which cannot resurrect a hidden file) and
 *                 leaves directories subject to list filtering.
 * Context       - the issue #443 use case: hide an auto-updated MRA list
 *                 wholesale while keeping curated subfolders navigable.
 * Preconditions - fixture testdir/ freshly created with a .nomedia
 *                 marker; a .showlist is added for the precedence checks.
 * Inputs        - a regular file, a subfolder, "..", a showlisted file,
 *                 an unlisted folder.
 * Expected      - marker alone: files hidden, folders and ".." visible.
 *                 Marker + showlist: the showlisted file is still hidden
 *                 (the marker wins for files); the unlisted folder is
 *                 hidden (lists still apply to folders).
 * Abnormal      - none.
 */
static void test_entry_visible_nomedia()
{
	begin_test("entry_visible: .nomedia marker");
	reset_fixture();
	write_file("testdir/.nomedia", "");
	dir_filters f;
	read_filters("testdir", f);
	CHECK(!f.showlist_present && !f.hidelist_present && f.nomedia_present,
		"only the marker is present");
	CHECK(!entry_visible(f, "any.rom", DT_REG), "regular files hidden");
	CHECK(entry_visible(f, "subfolder", DT_DIR), "subfolders remain visible");
	CHECK(entry_visible(f, "..", DT_DIR), "parent entry remains visible");

	write_file("testdir/.showlist", "keepme.rom\n");
	dir_filters f2;
	read_filters("testdir", f2);
	CHECK(f2.showlist_present && f2.nomedia_present,
		"marker and showlist both present");
	CHECK(!entry_visible(f2, "keepme.rom", DT_REG),
		"marker beats a matching showlist entry for files");
	CHECK(!entry_visible(f2, "unlisted_dir", DT_DIR),
		"folders still subject to showlist filtering");
	remove_fixture();
}

/*
 * Test case: test_entry_visible_no_filters
 *
 * Description   - with no filter files at all, every entry is visible.
 * Context       - the default for every user who does not opt in: the
 *                 fork must behave exactly like stock firmware.
 * Preconditions - none; a non-existent directory is queried.
 * Inputs        - a directory path holding no filter files; arbitrary
 *                 file, folder and ".." names.
 * Expected      - read_filters() reports nothing present; entry_visible()
 *                 returns true for files, folders and "..".
 * Abnormal      - querying a non-existent directory behaves like an empty
 *                 one (nothing present), by design.
 */
static void test_entry_visible_no_filters()
{
	begin_test("entry_visible: no filters (stock behaviour)");
	dir_filters f;
	read_filters("no_such_dir_xyz", f);
	CHECK(!f.showlist_present && !f.hidelist_present && !f.nomedia_present,
		"nothing reported present");
	CHECK(entry_visible(f, "whatever.rom", DT_REG), "files visible");
	CHECK(entry_visible(f, "somedir", DT_DIR), "folders visible");
	CHECK(entry_visible(f, "..", DT_DIR), "parent entry visible");
}

/*
 * Test case: test_readme_examples
 *
 * Description   - the example list files given in ../README.md work
 *                 exactly as the README describes.
 * Context       - documentation-drift guard: if the feature's behaviour
 *                 or the README's examples change independently, this
 *                 test fails. KEEP IN SYNC with README.md, "Examples".
 * Preconditions - fixture testdir/ freshly created per example.
 * Inputs        - (1) the games/Genesis .showlist example (two literal
 *                 names plus a `Micro Machines.*` regex); (2) the BIOS
 *                 .hidelist example; (3) the main-menu .hidelist example.
 * Expected      - (1) both literals and a "Micro Machines ..." title are
 *                 visible, another game is hidden; (2) BIOS-named files
 *                 in any casing and the wip folder are hidden, a game
 *                 stays visible; (3) _Utility and _Console are hidden,
 *                 another section and ".." stay visible.
 * Abnormal      - none.
 */
static void test_readme_examples()
{
	begin_test("README examples behave as documented");

	reset_fixture();
	write_file("testdir/.showlist",
		"Sonic The Hedgehog (USA, Europe).md\n"
		"Streets of Rage 2 (USA).md\n"
		"`Micro Machines.*`\n");
	dir_filters f1;
	read_filters("testdir", f1);
	CHECK(entry_visible(f1, "Sonic The Hedgehog (USA, Europe).md", DT_REG),
		"Genesis example: first literal visible");
	CHECK(entry_visible(f1, "Streets of Rage 2 (USA).md", DT_REG),
		"Genesis example: second literal visible");
	CHECK(entry_visible(f1, "Micro Machines 2 - Turbo Tournament (Europe).md", DT_REG),
		"Genesis example: regex admits Micro Machines titles");
	CHECK(!entry_visible(f1, "Golden Axe (World).md", DT_REG),
		"Genesis example: unlisted game hidden");

	reset_fixture();
	write_file("testdir/.hidelist",
		"`.*[Bb][Ii][Oo][Ss].*`\n"
		"wip\n");
	dir_filters f2;
	read_filters("testdir", f2);
	CHECK(!entry_visible(f2, "Genesis_BIOS.rom", DT_REG),
		"BIOS example: upper-case BIOS file hidden");
	CHECK(!entry_visible(f2, "bios_CD_E.bin", DT_REG),
		"BIOS example: lower-case bios file hidden");
	CHECK(!entry_visible(f2, "wip", DT_DIR),
		"BIOS example: work folder hidden");
	CHECK(entry_visible(f2, "Comix Zone (USA).md", DT_REG),
		"BIOS example: ordinary game visible");

	reset_fixture();
	write_file("testdir/.hidelist",
		"_Utility\n"
		"_Console\n");
	dir_filters f3;
	read_filters("testdir", f3);
	CHECK(!entry_visible(f3, "_Utility", DT_DIR),
		"main-menu example: _Utility hidden");
	CHECK(!entry_visible(f3, "_Console", DT_DIR),
		"main-menu example: _Console hidden");
	CHECK(entry_visible(f3, "_Arcade", DT_DIR),
		"main-menu example: other sections visible");
	CHECK(entry_visible(f3, "..", DT_DIR),
		"main-menu example: parent entry visible");
	remove_fixture();
}

int main()
{
	test_trim();
	test_escape_literal();
	test_escape_regex_regions();
	test_literal_identity_property();
	test_read_list_format();
	test_read_list_abnormal();
	test_has_nomedia();
	test_entry_visible_showlist();
	test_entry_visible_hidelist();
	test_entry_visible_nomedia();
	test_entry_visible_no_filters();
	test_readme_examples();

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
