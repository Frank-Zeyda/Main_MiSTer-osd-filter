/* Host-side unit tests for the .showlist/.hidelist/.nomedia helper logic.
 * The tested functions are included VERBATIM from ../file_io.cpp via
 * helpers.inc (extracted by the accompanying Makefile), so this exercises
 * the shipped code — including the real entry_visible() used by
 * ScanDirectory().
 *
 * The house I/O facilities (fileTextReader et al.) are stubbed below with
 * the same semantics as the originals in file_io.cpp; FileReadLine() is a
 * verbatim copy of the upstream implementation. */
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

#define IS_NEWLINE(c) (((c) == '\r') || ((c) == '\n'))
#define IS_WHITESPACE(c) (IS_NEWLINE(c) || ((c) == ' ') || ((c) == '\t'))

/* Verbatim copy of FileReadLine() from upstream file_io.cpp. */
static const char *FileReadLine(fileTextReader *reader)
{
	const char *end = reader->buffer + reader->size;
	while (reader->pos < end)
	{
		char *st = reader->pos;
		while ((reader->pos < end) && *reader->pos && !IS_NEWLINE(*reader->pos))
			reader->pos++;
		*reader->pos = 0;
		while (IS_WHITESPACE(*st)) st++;
		if (*st == '#' || *st == ';' || !*st)
		{
			reader->pos++;
		}
		else
		{
			return st;
		}
	}
	return nullptr;
}

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

static void write_file(const std::string& path, const std::string& content)
{
	FILE *f = fopen(path.c_str(), "wb");
	fwrite(content.data(), 1, content.size(), f);
	fclose(f);
}

int main()
{
	/* ---- trim() ---- */
	{
		CHECK(trim("  hello  ") == "hello", "trim strips spaces both sides");
		CHECK(trim("hello") == "hello", "trim leaves clean string alone");
		CHECK(trim("") == "", "trim handles empty string");
		CHECK(trim("   \t ") == "", "trim handles all-whitespace string");
		CHECK(trim("name\r") == "name", "trim strips CR");
		CHECK(trim("\ta b\t") == "a b", "trim keeps interior whitespace");
	}

	/* ---- escape_special(): literal-by-default semantics ---- */
	{
		CHECK(matches("Sonic (USA) [!].bin", "Sonic (USA) [!].bin"),
			"literal name with ()[]. matches itself");
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
	}

	/* ---- escape_special(): backtick regex regions ---- */
	{
		CHECK(matches("`.*`.bin", "foo.bin"),
			"backtick region passes .* through as regex");
		CHECK(!matches("`.*`.bin", "foo_bin"),
			"escaped dot outside region still literal");
		CHECK(matches("Super `.*`", "Super Mario World.sfc"),
			"literal prefix plus regex suffix");
		CHECK(!matches("Super `.*`", "Duper Mario"),
			"literal prefix must match");
		CHECK(matches("`(foo|bar)` fighters", "bar fighters"),
			"alternation inside region");
		CHECK(escape_special("a`b`c") == "abc",
			"backticks themselves are dropped from output");
		std::string longline(10000, 'x');
		CHECK(escape_special(longline).length() == 10000,
			"no length limit on patterns anymore");
	}

	/* ---- read_list(): house-reader semantics ---- */
	{
		system("rm -rf testdir && mkdir -p testdir");
		write_file("testdir/.showlist",
			"\xEF\xBB\xBF"                /* UTF-8 BOM (Windows editors) */
			"Sonic (USA).bin\r\n"         /* CRLF line */
			"\n"                          /* empty line: skipped */
			"   \n"                       /* whitespace-only: skipped */
			"# a comment line\n"          /* comment: skipped */
			"  ; another comment\n"       /* comment after whitespace */
			"`[`\n"                       /* invalid regex: skipped, warns */
			"`.*\\.(gg|sms)`\n"           /* raw regex line */
			"  Mario  \n");               /* padded literal */
		std::vector<std::regex> v;
		bool present = read_showlist("testdir", v);
		CHECK(present, "read_showlist finds the file");
		CHECK(v.size() == 3, "3 valid patterns (BOM/comments/empty/invalid skipped)");
		CHECK(std::regex_match("Sonic (USA).bin", v[0]),
			"BOM stripped from first pattern");

		std::vector<std::regex> h;
		CHECK(!read_hidelist("testdir", h), "absent .hidelist reported not present");
		CHECK(h.empty(), "absent list yields no patterns");

		/* an overlong line no longer discards the rest of the file */
		std::string longname(8000, 'x');
		write_file("testdir/.hidelist", longname + "\nafterwards\n");
		CHECK(read_hidelist("testdir", h) && h.size() == 2,
			"8000-char line parsed, following line still read");

		/* empty list file still counts as present (FileExists fallback) */
		write_file("testdir/.hidelist", "");
		CHECK(read_hidelist("testdir", h), "empty list file counts as present");
		CHECK(h.empty(), "empty list file yields no patterns");

		system("rm -f testdir/.hidelist");
	}

	/* ---- entry_visible(): the real ScanDirectory decision function ---- */
	{
		dir_filters f;
		read_filters("testdir", f);   /* .showlist from previous block */
		CHECK(f.showlist_present && !f.hidelist_present && !f.nomedia_present,
			"read_filters reflects present files");
		CHECK(entry_visible(f, "Sonic (USA).bin", DT_REG), "showlist: listed literal visible");
		CHECK(entry_visible(f, "game.gg", DT_REG), "showlist: regex-matched file visible");
		CHECK(entry_visible(f, "Mario", DT_REG), "showlist: padded literal visible");
		CHECK(!entry_visible(f, "Zelda.n64", DT_REG), "showlist: unlisted file hidden");
		CHECK(!entry_visible(f, "unlisted_dir", DT_DIR), "showlist: unlisted folder hidden");
		CHECK(entry_visible(f, "..", DT_DIR), "showlist: parent dir ('..') never hidden");
	}

	/* ---- hidelist overrides showlist; hidelist alone ---- */
	{
		write_file("testdir/.hidelist",
			"`.*\\.(gg|sms)`\n"
			"secret folder\n");
		dir_filters f;
		read_filters("testdir", f);
		CHECK(f.showlist_present && f.hidelist_present, "both lists found");
		CHECK(!entry_visible(f, "game.gg", DT_REG),
			"hidelist overrides showlist match");
		CHECK(entry_visible(f, "Sonic (USA).bin", DT_REG),
			"shown file not affected by unrelated hide patterns");

		system("rm -f testdir/.showlist");
		dir_filters f2;
		read_filters("testdir", f2);
		CHECK(!f2.showlist_present && f2.hidelist_present, "only .hidelist present");
		CHECK(entry_visible(f2, "anything.rom", DT_REG),
			"hidelist-only: unlisted entries visible by default");
		CHECK(!entry_visible(f2, "secret folder", DT_DIR),
			"hidelist-only: listed folder hidden");
		system("rm -f testdir/.hidelist");
	}

	/* ---- .nomedia marker ---- */
	{
		CHECK(!has_nomedia("testdir"), "no marker: has_nomedia is false");
		write_file("testdir/.nomedia", "");
		CHECK(has_nomedia("testdir"), "empty .nomedia marker detected");
		write_file("testdir/.nomedia", "arbitrary content\n");
		CHECK(has_nomedia("testdir"), "marker content is ignored, existence matters");
		CHECK(!has_nomedia("no_such_dir_xyz"), "missing dir has no marker");

		dir_filters f;
		read_filters("testdir", f);
		CHECK(!f.showlist_present && f.nomedia_present, "only .nomedia present");
		CHECK(!entry_visible(f, "any.rom", DT_REG),
			".nomedia: regular files hidden");
		CHECK(entry_visible(f, "subfolder", DT_DIR),
			".nomedia: subfolders remain visible");
		CHECK(entry_visible(f, "..", DT_DIR),
			".nomedia: parent entry remains visible");

		/* precedence: .showlist cannot resurrect files hidden by .nomedia */
		write_file("testdir/.showlist", "keepme.rom\n");
		dir_filters f2;
		read_filters("testdir", f2);
		CHECK(f2.showlist_present && f2.nomedia_present, "marker and showlist both present");
		CHECK(!entry_visible(f2, "keepme.rom", DT_REG),
			".nomedia beats a matching .showlist entry for files");
		CHECK(!entry_visible(f2, "unlisted_dir", DT_DIR),
			"directories still subject to .showlist filtering");
		system("rm -rf testdir");
	}

	/* ---- neither list nor marker present ---- */
	{
		dir_filters f;
		read_filters("no_such_dir_xyz", f);
		CHECK(!f.showlist_present && !f.hidelist_present && !f.nomedia_present,
			"missing dir: no filters present");
		CHECK(entry_visible(f, "whatever", DT_REG),
			"no filters: everything visible (stock behaviour)");
	}

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
