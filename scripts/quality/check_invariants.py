#!/usr/bin/env python3
"""Check the product invariants that otherwise live only in prose.

AGENTS.md states the rules the application has to keep. Most of them are
behaviour, and the test programs check that behaviour. A few are about the
shape of the source tree and the exact wording on screen, and nothing else
would notice them breaking: a file that grows past the size limit, a badge
that is drawn in the wrong order, a verdict that starts saying "safe", a
provider that is asked without a click.

This script checks those. It reads the tree and nothing else: no network, no
build, no display. It exits 0 when every rule holds and 1 when one does not,
printing what broke and where.

    python3 scripts/quality/check_invariants.py [--root .]

What it checks, and what it deliberately does not:

  * the source tree stays flat and small,
  * the wording the product promises is present, exactly once,
  * the shield is on every surface the product says it is on,
  * the content badges keep their order and their assets,
  * the providers are only reachable from a click, and only the two the
    product names,
  * a whole file is never hashed just to hash it,
  * the README documents the formats the archive check supports.

It does not check the behaviour of the application: that is what the test
programs under tests/ are for.
"""

import argparse
import os
import re
import sys

# ---------------------------------------------------------------------------
# The rules, as the product states them.
# ---------------------------------------------------------------------------

# No file in the tree grows past this. A file that outgrew it is split by
# topic, moving whole functions unchanged into a new file.
MAX_FILE_LINES = 800

# The exact sentence the product requires when there is no whole-file hash
# before the download. It is product copy and it is not paraphrased.
NO_HASH_SENTENCE = "VT no disponible antes de descargar el archivo completo."

# A clean result says "no known indicators", never "safe". These are the
# words a verdict must not use, in either language the product ships.
FORBIDDEN_VERDICT_WORDS = (
    "safe",
    "seguro",
    "segura",
    "seguros",
    "seguras",
    "limpio",
    "limpia",
    "limpios",
    "limpias",
)

# The content badges, in the order the product shows them: compressed,
# encrypted lock, ISO, video, executable.
BADGE_ORDER = ("Archive", "Encrypted", "DiscImage", "Video", "NativeExecutable")

# The asset each badge draws.
BADGE_ASSETS = {
    "Archive": "compressed.png",
    "Encrypted": "encrypted.png",
    "DiscImage": "iso.png",
    "Video": "video.png",
    "NativeExecutable": "executable.png",
}

# The surfaces that carry the shield next to the torrent name.
SHIELD_SURFACES = (
    ("src/SearchResultsWidget.cpp", "SecurityBadge::drawShield"),
    ("src/AddTorrentDialog.cpp", "SecurityShieldBox"),
    ("src/TorrentListWidget.cpp", "SecurityBadge::drawShield"),
    ("src/TorrentDetailsDialog.cpp", "SecurityShieldBox"),
)

# The provider query is started from the surfaces where the user clicks a
# shield. A fourth caller would mean a provider asked without a click.
REPUTATION_CALLERS = (
    "src/AddTorrentDialog.cpp",
    "src/SearchResultsWidgetBadges.cpp",
    "src/TorrentDetailsDialog.cpp",
)

# The two providers the product names, and nothing else.
PROVIDER_HOSTS = ("www.virustotal.com", "mb-api.abuse.ch")

# The formats the archive check supports, as README.md names them.
README_FORMATS = ("| ZIP |", "| RAR5", "| RAR 1.5")


# ---------------------------------------------------------------------------
# Reading the tree.
# ---------------------------------------------------------------------------

SOURCE_SUFFIXES = (".cpp", ".h")


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def source_files(root):
    """Every C++ source and header under src/ and tests/, relative to root."""
    found = []
    for area in ("src", "tests"):
        base = os.path.join(root, area)
        for dirpath, _dirnames, filenames in os.walk(base):
            for name in filenames:
                if name.endswith(SOURCE_SUFFIXES):
                    full = os.path.join(dirpath, name)
                    found.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return sorted(found)


def shipping_files(files):
    """The files that end up in the binary. The tests are not shipped."""
    return [rel for rel in files if rel.startswith("src/")]


def strip_comments(text):
    """The text of a C++ file with its comments removed."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def string_literals(text):
    """Every string literal in a C++ file, comments excluded.

    A hand-written little lexer rather than a regular expression: a quote
    inside a character literal, or a raw string, would otherwise be read as
    the start of a literal that never ends.
    """
    literals = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            i = n if end < 0 else end
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if ch == "'":
            # A character literal, so a quote inside it is not a string.
            j = i + 1
            while j < n:
                if text[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if text[j] == "'":
                    break
                j += 1
            i = j + 1
            continue
        if ch == "R" and i + 1 < n and text[i + 1] == '"':
            match = re.compile(r'R"([^()\\ \t\n]{0,16})\(').match(text, i)
            if match:
                terminator = ")" + match.group(1) + '"'
                end = text.find(terminator, match.end())
                literals.append(
                    text[match.end():end if end >= 0 else n])
                i = (end + len(terminator)) if end >= 0 else n
                continue
        if ch == '"':
            j = i + 1
            buf = []
            while j < n:
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j + 1])
                    j += 2
                    continue
                if text[j] == '"' or text[j] == "\n":
                    break
                buf.append(text[j])
                j += 1
            literals.append("".join(buf))
            i = j + 1
            continue
        i += 1
    return literals


def braced_block(text, marker):
    """The text between the braces of the block that starts at `marker`."""
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:i]
    return text[opening + 1:]


def enum_members(text, marker):
    """The enumerators of an enum, in the order they are declared."""
    block = strip_comments(braced_block(text, marker))
    names = []
    for part in block.split(","):
        part = part.strip()
        if not part:
            continue
        match = re.match(r"([A-Za-z_]\w*)", part)
        if match:
            names.append(match.group(1))
    return names


# ---------------------------------------------------------------------------
# The checks.
# ---------------------------------------------------------------------------


class Report:
    def __init__(self):
        self.failures = []
        self.checks = 0

    def check(self, ok, what, detail=""):
        self.checks += 1
        if ok:
            print("  ok   %s" % what)
        else:
            print("  FAIL %s" % what)
            if detail:
                for line in detail.splitlines():
                    print("         %s" % line)
            self.failures.append(what)
        return ok


def check_flat_tree(report, root):
    src = os.path.join(root, "src")
    subdirs = sorted(
        name for name in os.listdir(src)
        if os.path.isdir(os.path.join(src, name))
    )
    # assets/ is where the images live, not a piece of the code.
    unexpected = [name for name in subdirs if name != "assets"]
    report.check(
        not unexpected,
        "the code in src/ is flat: no subdirectories but assets/",
        "subdirectories: %s" % ", ".join(unexpected),
    )


def check_file_sizes(report, root, files):
    over = []
    for rel in files:
        lines = len(read_text(os.path.join(root, rel)).splitlines())
        if lines > MAX_FILE_LINES:
            over.append("%s: %d lines" % (rel, lines))
    report.check(
        not over,
        "no source file is over %d lines" % MAX_FILE_LINES,
        "\n".join(over),
    )


def check_no_hash_sentence(report, root, files):
    hits = []
    for rel in files:
        if NO_HASH_SENTENCE in read_text(os.path.join(root, rel)):
            hits.append(rel)
    report.check(
        len(hits) == 1,
        "the no-hash sentence is in the shipping code exactly once",
        "found in: %s" % ", ".join(hits) if hits else "not found anywhere",
    )


def check_no_safe_verdict(report, root, files):
    pattern = re.compile(
        r"\b(%s)\b" % "|".join(FORBIDDEN_VERDICT_WORDS), re.IGNORECASE)
    offenders = []
    for rel in files:
        text = read_text(os.path.join(root, rel))
        for literal in string_literals(text):
            if pattern.search(literal):
                offenders.append("%s: %r" % (rel, literal))
    report.check(
        not offenders,
        "no user-facing string claims the file is safe",
        "\n".join(offenders),
    )


def check_play_unused(report, root, files):
    hits = []
    for rel in files:
        if "play.png" in read_text(os.path.join(root, rel)):
            hits.append(rel)
    report.check(
        not hits,
        "play.png is not referenced: the app does not stream",
        "referenced by: %s" % ", ".join(hits),
    )


def check_badge_assets(report, root):
    assets = os.path.join(root, "src", "assets")
    missing = [
        "%s (%s)" % (name, asset)
        for name, asset in sorted(BADGE_ASSETS.items())
        if not os.path.isfile(os.path.join(assets, asset))
    ]
    report.check(
        not missing,
        "every content badge has its asset",
        "missing: %s" % ", ".join(missing),
    )


def check_badge_order(report, root):
    header = read_text(
        os.path.join(root, "src", "SearchResultsWidget.h"))
    declared = [n for n in enum_members(header, "enum class ContentBadge")
                if n != "Count"]

    drawing = read_text(
        os.path.join(root, "src", "SearchResultsWidget.cpp"))
    block = braced_block(drawing, "const BadgePlan plan[]")
    drawn = re.findall(r"ContentBadge::(\w+)", block)

    expected = list(BADGE_ORDER)
    report.check(
        declared == expected,
        "the badge enum is in the order the product shows",
        "declared: %s" % ", ".join(declared),
    )
    report.check(
        drawn == expected,
        "the badges are drawn in the order the product shows",
        "drawn: %s" % ", ".join(drawn),
    )


def check_shield_surfaces(report, root):
    missing = []
    for rel, marker in SHIELD_SURFACES:
        text = read_text(os.path.join(root, rel))
        if marker not in text:
            missing.append("%s (%s)" % (rel, marker))
    report.check(
        not missing,
        "the shield is on every surface the product names",
        "missing: %s" % ", ".join(missing),
    )


def check_hash_only_on_disk(report, root, files):
    definition = re.compile(r"bool\s+FileHash::sha256File\s*\(")
    callers = []
    for rel in files:
        text = read_text(os.path.join(root, rel))
        if "FileHash::sha256File(" not in text:
            continue
        if definition.search(text):
            continue
        callers.append(rel)
    expected = ["src/TorrentDetailsDialog.cpp"]
    report.check(
        callers == expected,
        "a whole file is only hashed where it is already on disk",
        "called from: %s" % ", ".join(callers),
    )


def check_reputation_callers(report, root, files):
    # The service owns the request; the owner is not a caller. A caller is a
    # file that holds a ReputationService and asks it for something, which is
    # what a provider query looks like from the outside.
    callers = []
    for rel in files:
        if rel == "src/ReputationService.cpp":
            continue
        text = read_text(os.path.join(root, rel))
        if "ReputationService" not in text:
            continue
        if re.search(r"\.request\s*\(", text):
            callers.append(rel)
    expected = sorted(REPUTATION_CALLERS)
    report.check(
        sorted(callers) == expected,
        "the providers are asked only from the surfaces the user clicks",
        "asked from: %s" % ", ".join(sorted(callers)),
    )


def check_provider_endpoints(report, root, files):
    url = re.compile(r"https?://[A-Za-z0-9._-]+/[^\s\"']*")
    holders = set()
    hosts = set()
    for rel in files:
        text = read_text(os.path.join(root, rel))
        for literal in string_literals(text):
            for found in url.findall(literal):
                host = found.split("//", 1)[1].split("/", 1)[0]
                if "/api/" not in found:
                    continue
                if any(host.endswith(h) for h in PROVIDER_HOSTS):
                    holders.add(rel)
                    hosts.add(host)
    report.check(
        holders == {"src/Reputation.cpp"},
        "the provider endpoints live in one file",
        "found in: %s" % ", ".join(sorted(holders)),
    )
    report.check(
        hosts == set(PROVIDER_HOSTS),
        "the only providers asked are the two the product names",
        "hosts: %s" % ", ".join(sorted(hosts)),
    )


def check_no_key_literal(report, root, files):
    key = re.compile(r"\A[0-9a-fA-F]{32,}\Z")
    offenders = []
    for rel in files:
        text = read_text(os.path.join(root, rel))
        for literal in string_literals(text):
            if key.match(literal):
                offenders.append("%s: %r" % (rel, literal))
    report.check(
        not offenders,
        "no API key is compiled into the binary",
        "\n".join(offenders),
    )


def check_bounded_archive_walk(report, root):
    text = read_text(os.path.join(root, "src", "ArchivePreflight.cpp"))
    has_budget = "setPieceBudget(" in text
    has_cap = re.search(r"std::min\(\s*64\s*,", text) is not None
    has_note = "partial list" in text
    report.check(
        has_budget and has_cap,
        "the old-RAR header walk is bounded",
        "setPieceBudget: %s, cap: %s" % (has_budget, has_cap),
    )
    report.check(
        has_note,
        "a partial list says so",
        "the note does not say 'partial list'",
    )


def check_readme_formats(report, root):
    text = read_text(os.path.join(root, "README.md"))
    missing = [name for name in README_FORMATS if name not in text]
    report.check(
        not missing,
        "the README documents the formats the archive check supports",
        "missing: %s" % ", ".join(missing),
    )


# ---------------------------------------------------------------------------


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=os.path.dirname(os.path.dirname(
            os.path.dirname(os.path.abspath(__file__)))),
        help="the root of the repository",
    )
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root)

    if not os.path.isdir(os.path.join(root, "src")):
        print("not a FTorrent checkout: %s" % root, file=sys.stderr)
        return 2

    files = source_files(root)
    shipping = shipping_files(files)
    report = Report()

    print("product invariants, %s" % root)
    check_flat_tree(report, root)
    check_file_sizes(report, root, files)
    check_no_hash_sentence(report, root, shipping)
    check_no_safe_verdict(report, root, shipping)
    check_play_unused(report, root, files)
    check_badge_assets(report, root)
    check_badge_order(report, root)
    check_shield_surfaces(report, root)
    check_hash_only_on_disk(report, root, shipping)
    check_reputation_callers(report, root, shipping)
    check_provider_endpoints(report, root, shipping)
    check_no_key_literal(report, root, shipping)
    check_bounded_archive_walk(report, root)
    check_readme_formats(report, root)

    print("")
    if report.failures:
        print("%d of %d checks failed:" % (len(report.failures),
                                            report.checks))
        for what in report.failures:
            print("  - %s" % what)
        return 1
    print("all %d checks passed" % report.checks)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
