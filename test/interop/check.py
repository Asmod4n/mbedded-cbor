# Compares the results of run.sh in three parts, and prints every finding with its bytes. Exits 1 when a finding
# exists.
# 1. Others read what the library writes: for each decoder, the decoded value of the bytes that the library wrote
#    against the decoded value of the expected bytes. A finding is a written item that a decoder refuses for a
#    reason that UNSUPPORTED does not list, a written item that reads as another value, or a value that a writer of
#    the library refuses.
# 2. The library reads what others write: each library wrote the value that it decoded from the input of a case
#    again with its own encoder. cbor::decode of those bytes is compared with the dump that the library itself gives
#    of the same bytes, and with the dump that the library gives of the source, which is the value that it wrote. A
#    finding is a refusal of the library or another value. TinyCBOR reads the same bytes as a
#    witness, and the side whose dump differs from that of the witness is the side at fault. A difference between
#    the bytes of a library and the input of the case is a note on the data model of that library, never a finding.
# 3. Cycles: cbor2, CBOR::XS and cbor::decode_cycles read every line of cycles.tsv, and the graph of each is
#    compared with the expected graph. A tag 29 to a value whose read ended is value sharing, which the library does
#    not read by design: cbor::decode_cycles gives sharedref_not_cyclic for it.
# usage: check.py <directory of run.sh> <library>...
import collections
import os
import re
import sys

shown_characters = 160
shown_bytes = 600

out = sys.argv[1]
libraries = sys.argv[2:]


def rows(name):
    for line in open(os.path.join(out, name)):
        yield line.rstrip("\n").split("\t")


# The library refuses these inputs on purpose: indefinite length is never written, and the nesting depth has a
# limit. A re-encode of such an input has nothing to write.
REFUSED = ("!indefinite length", "!nesting depth exceeded")
SHARING_REFUSED = "!sharedref not cyclic"
SHARING = "value sharing, not supported by design (owner 2026-11-27)"

cases = {}
for name, section, written, expected in rows("cases.tsv"):
    cases[name] = (section, written, expected)
versions = dict(row for row in (line.rstrip("\n").split("\t", 1) for line in open(os.path.join(out, "versions.tsv"))))
ours = "mbedded-cbor"

# What each decoder does not support. RFC 8949 5.2 lets a decoder refuse what it does not want to
# understand. Each entry comes from refusals that also happen on the original RFC or cbor-wg bytes, except the
# tag 6 of libcbor and the undefined of nlohmann/json, which only Packed CBOR output showed.
# Each entry: decoder, feature, RFC section, a pattern of the message of the decoder.
UNSUPPORTED = [
    ("libcbor", "tag numbers 6 to 20 (tag 6 of Packed CBOR, tag 18 of COSE), simple values other than 20 to 23",
     "RFC 8949 3.3, 3.4", r"^!cbor_load error 3 "),
    ("jsoncons", "simple values other than 20 to 23", "RFC 8949 3.3", r"unknown type was found"),
    ("nlohmann", "tags on any item but a byte string (handler store)", "RFC 8949 3.4",
     r"invalid byte: 0x(C[0-9A-F]|D[0-9A-B])$|CBOR binary: expected length"),
    ("nlohmann", "undefined and simple values other than 20 to 22", "RFC 8949 3.3",
     r"invalid byte: 0x(E[0-9A-F]|F[0-8])$"),
    ("nlohmann", "map keys that are not text", "RFC 8949 3.1, 5.6", r"CBOR string: expected length"),
    ("cbor_xs", "simple values other than 20 to 23", "RFC 8949 3.3", r"unexpected major 7 value"),
    ("cbor_xs", "undefined as a map key", "RFC 8949 3.3, 5.6", r"Types::Serialiser::error"),
]

# RFC 8949 2 and 3.4.3: the library writes values, and a bignum is the integer of the same numeric value. The
# decoders show a bignum in their own way: 2(h'..') and 3(h'..'), and the decimal of jsoncons and of Math::BigInt.
# Each is read as the decimal integer, so that a written integer and an expected bignum of the same value agree.
BIGNUM_BYTES = re.compile(r"\b([23])\(h'([0-9a-f]*)'\)")


def numeric(dump):
    def from_bytes(m):
        magnitude = int(m.group(2) or "0", 16)
        return str(magnitude if m.group(1) == "2" else -1 - magnitude)

    return BIGNUM_BYTES.sub(from_bytes, dump)


# PROVISIONAL, the owner decides: decoders that read a well-formed value wrong. The written bytes are the
# preferred serialization of RFC 8949 4.2.1 and 3.4.3. Each entry: decoder, fault, RFC section, a pattern of the
# written hex, a pattern of the expected hex. CBOR::XS gives 3(h'01') as -1 and not -2, 2(h'') as NaN and not 0,
# and a major type 1 argument of 2^63 or more as another number; jsoncons reads that argument wrong too. In part 2
# the written hex is the hex that the decoder wrote itself, and the expected hex is the input of the case.
MISREAD = [
    ("cbor_xs", "tag 2 and 3 bignums: -n for 3(n) and NaN for an empty magnitude; major type 1 arguments of 2^63 "
     "or more", "RFC 8949 3.1, 3.4.3", r"", r"^c[23]"),
    ("jsoncons", "major type 1 arguments of 2^63 or more", "RFC 8949 3.1", r"^3b[89a-f]", r""),
]

# How each library wrote and read again in part 2, and one line that does the same in the language of the library.
# SRC stands for the hex of the source bytes.
CALLS = {
    "tinycbor": ("cbor_parser_init and cbor_value_validate(CborValidateBasic), then the walk value_copy of "
                 "test/interop/tinycbor.cpp: one cbor_encode_* call per item of the same type and float width",
                 "printf 'case\\t-\\t-\\tSRC\\n' | tinycbor reencode   (test/interop/tinycbor.cpp)"),
    "libcbor": ("cbor_load, then cbor_serialize_alloc; read again with cbor_load",
                "cbor_serialize_alloc(cbor_load(src, size, &result), &buffer, &buffer_size);"),
    "jsoncons": ("jsoncons::cbor::decode_cbor<jsoncons::ojson>, then jsoncons::cbor::encode_cbor into a "
                 "std::vector<std::uint8_t>; read again with decode_cbor<ojson>",
                 "jsoncons::cbor::encode_cbor(jsoncons::cbor::decode_cbor<jsoncons::ojson>(src), out);"),
    "nlohmann": ("nlohmann::ordered_json::from_cbor(bytes, true, true, cbor_tag_handler_t::store), then "
                 "nlohmann::ordered_json::to_cbor; read again with from_cbor",
                 "auto out = nlohmann::ordered_json::to_cbor(nlohmann::ordered_json::from_cbor(src, true, true, "
                 "nlohmann::json::cbor_tag_handler_t::store));"),
    "cbor2": ("cbor2.loads, then cbor2.dumps with its defaults; read again with cbor2.loads",
              "python3 -c 'import cbor2; print(cbor2.dumps(cbor2.loads(bytes.fromhex(\"SRC\"))).hex())'"),
    "cbor_xs": ("CBOR::XS->new->allow_sharing->allow_cycles->decode, then CBOR::XS->new->encode; read again with "
                "the same decoder",
                "perl -MCBOR::XS -e 'print unpack(\"H*\", CBOR::XS->new->encode(CBOR::XS->new->decode("
                "pack(\"H*\", \"SRC\"))))'"),
}


def shown(hex_text):
    if len(hex_text) <= shown_bytes:
        return hex_text
    return hex_text[:shown_bytes] + "... (%d hex digits, whole in the file of run.sh)" % len(hex_text)


def version(library):
    return versions.get(library, "?")


TAG = re.compile(r"(?:^|[\[,:{(])(\d+)\(")


def note_reasons(source, written):
    s = collections.Counter(TAG.findall(source))
    w = collections.Counter(TAG.findall(written))
    reasons = ["drops tag %s" % t for t in sorted(s - w, key=int)] + \
              ["writes tag %s" % t for t in sorted(w - s, key=int)]
    return reasons or ["writes another value with the same tags (map keys, integers, strings)"]


findings = []
lines = []

# Part 1.
misread = collections.Counter()
unsupported = collections.Counter()
notes = collections.Counter()
limits = collections.defaultdict(list)
for name, (section, written, expected) in cases.items():
    if written.startswith("!"):
        if written in REFUSED:
            notes["input refused by the library: " + written[1:]] += 1
        else:
            findings.append("FINDING library %s [%s]\n  writer refuses %s\n  expected %s"
                            % (name, section, written, expected))
counts = {}
for decoder in libraries:
    ok = 0
    for name, got, want in rows(decoder + ".tsv"):
        section, written, expected = cases[name]
        if got.startswith("!"):
            entry = next((u for u in UNSUPPORTED if u[0] == decoder and re.search(u[3], got)), None)
            if entry:
                unsupported[entry] += 1
            elif got == want:
                limits[decoder].append((name, "refused as the expected bytes: " + want))
            else:
                findings.append("FINDING %s (%s) %s [%s]\n  written  %s\n  expected %s\n  decoded  %s\n  from exp %s"
                                % (decoder, version(decoder), name, section, written, expected, got[:300],
                                   want[:300]))
        elif want.startswith("!"):
            limits[decoder].append((name, "expected only: " + want))
        elif want != "-" and numeric(got) != numeric(want):
            entry = next((m for m in MISREAD if m[0] == decoder and re.search(m[3], written) and
                          re.search(m[4], expected)), None)
            if entry:
                misread[entry] += 1
            else:
                findings.append("FINDING %s (%s) %s [%s]\n  written  %s\n  expected %s\n  decoded  %s\n  from exp %s"
                                % (decoder, version(decoder), name, section, written, expected, got[:300],
                                   want[:300]))
        else:
            ok += 1
    counts[decoder] = ok

lines.append("part 1: others read what %s (%s) writes" % (ours, version(ours)))
for d, n in counts.items():
    lines.append("%-10s %-28s agree %d" % (d, version(d), n))
for (d, feature, section, _), n in unsupported.items():
    lines.append("not supported by %s (%s): %s [%s], %d cases" % (d, version(d), feature, section, n))
for (d, fault, section, _, _), n in misread.items():
    lines.append("misread by %s (%s): %s [%s], %d cases" % (d, version(d), fault, section, n))
for k, n in notes.items():
    lines.append("note: %d cases: %s" % (n, k))
for d, items in limits.items():
    for name, why in items:
        lines.append("limit %s %s: %s" % (d, name, why[:shown_characters]))

# Part 2. A finding is one fault: the cases with the same library, kind and side. The first cases of each stand
# whole, the others by their names.
shown_examples = 3
faults = collections.defaultdict(list)
lines.append("part 2: %s (%s) reads what others write" % (ours, version(ours)))
for library in libraries:
    read = {name: (source, written) for name, source, written in rows(library + "_read.tsv")}
    held = {name: want if cases[name][2] != "-" else got for name, got, want in rows(library + ".tsv")}
    witness = {name: got for name, got, _ in rows(library + "_witness.tsv")}
    count = collections.Counter()
    grouped = collections.defaultdict(list)
    for name, source_hex, written_hex, theirs in rows(library + "_others.tsv"):
        section = cases[name][0]
        if written_hex.startswith("!"):
            count["cannot write"] += 1
            grouped["cannot write: " + written_hex[1:shown_characters]].append(name)
            continue
        source, mine = read[name]
        seen = witness.get(name, "-")
        kind = None
        if mine.startswith("!") and mine in REFUSED:
            count["notes"] += 1
            grouped["note: written bytes refused by %s: %s" % (ours, mine[1:])].append(name)
            continue
        if mine.startswith("!"):
            kind = "%s refuses the bytes" % ours
            side = library if seen.startswith("!") else ours
        elif theirs.startswith("!"):
            kind = "%s does not read the bytes it wrote" % library
            side = library
        elif not held[name].startswith("!") and "<" not in held[name] and numeric(held[name]) != numeric(mine):
            kind = "the bytes that %s wrote carry another value than %s decoded from the source" % (library, library)
            side = library if numeric(seen) == numeric(mine) else ours
        elif "<" in theirs:
            count["not comparable"] += 1
            grouped["not comparable: %s dump stays %s" % (library, re.search(r"<[^:>]*", theirs).group(0) + ">")]\
                .append(name)
            continue
        elif numeric(theirs) != numeric(mine):
            kind = "another value"
            if numeric(seen) == numeric(mine):
                side = library
            elif numeric(seen) == numeric(theirs):
                side = ours
            else:
                side = "unknown: the witness tinycbor reads a third value"
            entry = next((m for m in MISREAD if m[0] == library and side == library and
                          re.search(m[3], written_hex) and re.search(m[4], source_hex)), None)
            if entry:
                count["misread"] += 1
                grouped["misread by %s of the bytes it wrote: %s [%s]" % (library, entry[1], entry[2])].append(name)
                continue
        if kind:
            count["findings"] += 1
            write_call, reproduce = CALLS[library]
            faults[(library, kind, side)].append(
                "  case     %s [%s]\n  source   %s\n  %s wrote %s\n  call     %s\n  %s holds %s\n  %s reads %s\n"
                "  %s reads %s\n  witness  tinycbor reads %s\n  side     %s\n  repeat   %s"
                % (name, section, shown(source_hex), library, shown(written_hex), write_call, library,
                   held[name][:300], library, theirs[:300], ours, mine[:300], seen[:300], side,
                   reproduce.replace("SRC", source_hex if len(source_hex) <= shown_bytes else "<source>")))
            continue
        if source.startswith("!"):
            count["notes"] += 1
            grouped["note: the source is refused by %s (%s), the bytes of %s are read" % (ours, source[1:], library)]\
                .append(name)
        elif numeric(source) != numeric(mine):
            count["notes"] += 1
            for reason in note_reasons(source, mine):
                grouped["note: %s %s" % (library, reason)].append(name)
        else:
            count["agree"] += 1
            if written_hex == source_hex:
                count["no new input"] += 1
    lines.append("written by %-9s %-28s agree %d (no new input %d), not comparable %d, notes %d, cannot write %d, "
                 "misread %d, findings %d"
                 % (library, version(library), count["agree"], count["no new input"], count["not comparable"],
                    count["notes"], count["cannot write"], count["misread"], count["findings"]))
    for what, names in grouped.items():
        lines.append("  %s: %d cases, e.g. %s" % (what, len(names), names[0]))

for (library, kind, side), examples in faults.items():
    findings.append("FINDING part 2, written by %s (%s), read by %s (%s): %s; side %s; %d cases\n%s%s"
                    % (library, version(library), ours, version(ours), kind, side, len(examples),
                       "\n".join(examples[:shown_examples]),
                       "" if len(examples) <= shown_examples else "\n  more     " +
                       " ".join(e.split()[1] for e in examples[shown_examples:])))

# Part 3.
lines.append("part 3: cycles, written by %s (cbor::encode_cycles), cbor2 and CBOR::XS" % ours)
cycles = list(rows("cycles.tsv"))
for name, written, expected, kind in cycles:
    if written.startswith("!"):
        findings.append("FINDING cycles: cbor::encode_cycles refuses %s: %s" % (name, written))
for reader in (ours, "cbor2", "cbor_xs"):
    got = dict(rows("cycles_%s.tsv" % reader))
    count = collections.Counter()
    for name, written, expected, kind in cycles:
        if written.startswith("!"):
            continue
        g = got.get(name, "!no line")
        if reader == ours and kind == "sharing":
            if g == SHARING_REFUSED:
                count["sharing"] += 1
                continue
            want = SHARING_REFUSED
        elif g == expected:
            count["agree"] += 1
            continue
        else:
            want = expected
        count["findings"] += 1
        findings.append("FINDING part 3, cycles read by %s (%s): %s\n  written  %s\n  expected %s\n  read     %s"
                        % (reader, version(reader), name, written, want, g))
    lines.append("read by %-11s %-28s agree %d, %s: %d cases, findings %d"
                 % (reader, version(reader), count["agree"], SHARING, count["sharing"], count["findings"]))

print("\n".join(lines))
for f in findings:
    print(f)
print("findings: %d" % len(findings))
sys.exit(1 if findings else 0)
