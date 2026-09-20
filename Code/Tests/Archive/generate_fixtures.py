#!/usr/bin/env python3
"""Deterministic ZIP32 fixtures. Python stdlib only; never extracts members.

Valid archives come from zipfile. Invalid archives are deliberate, documented
mutations of real ZIP headers. --large adds 50,000 empty files (~4.7 MiB).
"""
import argparse
import io
import json
from pathlib import Path
import stat
import struct
import warnings
import zipfile
import zlib

STAMP = (2024, 1, 2, 3, 4, 6)


class RawName(zipfile.ZipInfo):
    def __init__(self, raw, flags=0):
        super().__init__("placeholder", STAMP)
        self.raw = raw
        self.raw_flags = flags

    def _encodeFilenameFlags(self):
        return self.raw, self.flag_bits | self.raw_flags


class NonSeekable(io.BytesIO):
    def seekable(self):
        return False

    def seek(self, *args):
        raise io.UnsupportedOperation("fixture forces ZIP data descriptor")


def info(name, method=zipfile.ZIP_STORED, stamp=STAMP, extra=b"", attrs=None):
    value = name if isinstance(name, zipfile.ZipInfo) else zipfile.ZipInfo(name, stamp)
    value.compress_type = method
    value.create_system = 3
    value.extra = extra
    value.external_attr = attrs if attrs is not None else (
        ((stat.S_IFDIR | 0o755) << 16) | 0x10 if value.filename.endswith("/")
        else (stat.S_IFREG | 0o644) << 16)
    return value


def archive(entries, method=zipfile.ZIP_STORED, descriptor=False, comment=b""):
    stream = NonSeekable() if descriptor else io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(stream, "w", allowZip64=False) as output:
            output.comment = comment
            for name, payload in entries:
                output.writestr(info(name, method) if not isinstance(name, zipfile.ZipInfo) else name,
                                payload)
    return bytearray(stream.getvalue())


def eocd(data):
    return data.rfind(b"PK\x05\x06")


def central(data):
    end = eocd(data)
    count = struct.unpack_from("<H", data, end + 10)[0]
    offset = struct.unpack_from("<I", data, end + 16)[0]
    result = []
    for _ in range(count):
        assert data[offset:offset + 4] == b"PK\x01\x02"
        result.append(offset)
        name, extra, comment = struct.unpack_from("<HHH", data, offset + 28)
        offset += 46 + name + extra + comment
    return result


def u16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def local(data, entry=0):
    return struct.unpack_from("<I", data, central(data)[entry] + 42)[0]


def payload_start(data, entry=0):
    start = local(data, entry)
    name, extra = struct.unpack_from("<HH", data, start + 26)
    return start + 30 + name + extra


def unicode_path(raw, path, crc=None):
    encoded = path.encode("utf-8") if isinstance(path, str) else path
    body = b"\x01" + struct.pack("<I", zlib.crc32(raw) if crc is None else crc) + encoded
    return struct.pack("<HH", 0x7075, len(body)) + body


def generate(output, large=False):
    output.mkdir(parents=True, exist_ok=True)
    descriptions = {}

    def save(name, data, description):
        (output / name).write_bytes(data)
        descriptions[name] = description

    save("empty.zip", archive([]), "Valid empty ZIP32")
    basic = archive([("z.txt", b"last"), ("a/deep/readme.txt", b"hello\n"),
                     ("explicit/", b""), ("zero.bin", b"")])
    save("stored.zip", basic, "Four stored members; two synthesized parent directories")
    deflated = archive([("compressed.txt", b"compressible text\n" * 100)], zipfile.ZIP_DEFLATED)
    save("deflated.zip", deflated, "Real raw-deflate payload produced by zipfile")
    save("deflated-directory.zip", archive([("directory/", b"")], zipfile.ZIP_DEFLATED),
         "Legal empty directory has two compressed deflate bytes and zero uncompressed bytes")
    save("implicit-directory.zip", archive([("directory/file", b"data")]),
         "Parent directory exists implicitly without timestamp or compression metadata")
    save("utf8.zip", archive([("资料/测试.txt", "正文".encode()), ("cafe\u0301.txt", b"nfc"), ("😀.txt", b"emoji")]),
         "UTF-8 flag; Chinese paths, supplementary emoji, and decomposed accent normalized to NFC")
    save("cp437.zip", archive([(info(RawName(b"caf\x82.txt")), b"cp437")]),
         "CP437 byte 0x82 is e acute, without UTF-8 flag")
    legacy_name = b"legacy.txt"
    unicode_extra = unicode_path(legacy_name, "中文.txt")
    save("unicode-extra.zip", archive([(info(RawName(legacy_name), extra=unicode_extra), b"unicode")]),
         "Info-ZIP Unicode path extra 0x7075 overrides CP437 name when filename CRC matches")
    save("unicode-stale.zip", archive([(info(RawName(legacy_name), extra=unicode_path(legacy_name, "../ignored", 0)), b"unicode")]),
         "Stale Unicode path filename CRC is ignored; legacy name remains authoritative")
    save("unicode-unsafe-alias.zip", archive([(info(RawName(legacy_name), extra=unicode_path(legacy_name, "../escape")), b"evil")]),
         "Unsafe Unicode alias cannot bypass path validation")
    raw_unsafe = b"../escape"
    save("unicode-unsafe-raw.zip", archive([(info(RawName(raw_unsafe), extra=unicode_path(raw_unsafe, "safe.txt")), b"evil")]),
         "Safe Unicode alias cannot conceal unsafe legacy name")
    save("unicode-invalid.zip", archive([(info(RawName(legacy_name), extra=unicode_path(legacy_name, b"bad\xff")), b"x")]),
         "Matching Unicode path field contains invalid UTF-8")
    save("unicode-duplicate.zip", archive([(info(RawName(legacy_name), extra=unicode_extra * 2), b"x")]),
         "Repeated Unicode path fields are ambiguous")
    save("unicode-utf8-disagreement.zip", archive([(info(RawName(legacy_name, 0x800), extra=unicode_extra), b"x")]),
         "Flagged UTF-8 raw name disagrees with Unicode path extra")
    local_disagreement = archive([(info(RawName(legacy_name), extra=unicode_path(legacy_name, "alpha.txt")), b"x")])
    local_unicode_start = 30 + len(legacy_name) + 9
    local_disagreement[local_unicode_start:local_unicode_start + 9] = b"bravo.txt"
    save("unicode-local-disagreement.zip", local_disagreement, "Local and central effective Unicode paths disagree")
    local_type_disagreement = archive([(info(RawName(b"a"), extra=unicode_path(b"a", "a/")), b"x")])
    center = central(local_type_disagreement)[0]
    extra_start = center + 47
    extra_length = struct.unpack_from("<H", local_type_disagreement, center + 30)[0]
    del local_type_disagreement[extra_start:extra_start + extra_length]
    u16(local_type_disagreement, center + 30, 0)
    end = eocd(local_type_disagreement)
    old_size = struct.unpack_from("<I", local_type_disagreement, end + 12)[0]
    u32(local_type_disagreement, end + 12, old_size - extra_length)
    save("unicode-local-type-disagreement.zip", local_type_disagreement,
         "Local-only Unicode alias adds directory slash; normalized name matches central file but kind conflicts")
    save("normalized.zip", archive([("one\\two/./three.txt", b"normal"), ("Case", b"A"),
                                   ("case", b"a")]), "Slash/dot normalization; case-sensitive names")
    descriptor = archive([("stream.txt", b"streamed content" * 10)], zipfile.ZIP_DEFLATED, True)
    save("descriptor.zip", descriptor, "Non-seekable zipfile writer emits signed data descriptor")
    descriptor_at = payload_start(descriptor) + struct.unpack_from("<I", descriptor, central(descriptor)[0] + 20)[0]
    assert descriptor[descriptor_at:descriptor_at + 4] == b"PK\x07\x08"
    unsigned = descriptor[:descriptor_at] + descriptor[descriptor_at + 4:]
    u32(unsigned, eocd(unsigned) + 16, central(descriptor)[0] - 4)
    save("descriptor-unsigned.zip", unsigned, "Data descriptor without optional signature")
    bad = descriptor[:]
    u32(bad, descriptor_at + 4, 1234)
    save("descriptor-bad-crc.zip", bad, "Descriptor CRC disagrees with central directory")
    save("comment-signatures.zip", archive([("ok", b"yes")], comment=b"comment PK\x05\x06 fake"),
         "Valid EOCD comment includes a misleading EOCD signature")
    save("comment-full-eocd.zip", archive([("ok", b"yes")], comment=b"PK\x05\x06" + b"\0" * 18),
         "Archive comment contains a complete fake empty EOCD at end of file")
    locator_comment = info("ok")
    locator_comment.comment = b"PK\x06\x07" + b"x" * 16
    save("comment-fake-locator.zip", archive([(locator_comment, b"yes")]),
         "Last member comment begins with ZIP64 locator signature exactly 20 bytes before EOCD")
    save("signature.jar", basic, "ZIP signature accepted with JAR extension")
    save("signature.data", basic, "ZIP signature accepted without ZIP extension")
    save("nested.zip", archive([("inside.zip", bytes(archive([("inner.txt", b"inner")])))]),
         "Nested archive remains an ordinary member; no recursive payload parsing")

    left = [("same.txt", b"same"), ("size.txt", b"a"), ("crc.txt", b"aaa"),
            ("left-only.txt", b"left"), ("kind", b"file"), ("nested/path.txt", b"nested"),
            ("metadata.txt", b"same metadata content")]
    right = [("same.txt", b"same"), ("size.txt", b"three"), ("crc.txt", b"bbb"),
             ("right-only.txt", b"right"), ("kind/", b""), ("nested/path.txt", b"nested"),
             (info("metadata.txt", stamp=(2025, 1, 2, 3, 4, 6)), b"same metadata content")]
    save("compare-left.zip", archive(left), "Comparison fixture left: presence, kind, size, CRC, time")
    save("compare-right.zip", archive(right), "Comparison fixture right: all supported difference states")

    a = bytes.fromhex("425ab9e9e0aa2213b4c3e192")
    b = bytes.fromhex("be55214eb39dda0761897cc1")
    assert a != b and len(a) == len(b) and zlib.crc32(a) == zlib.crc32(b) == 0x8c58dcdc
    save("collision-left.zip", archive([("collision.bin", a)]), "Different bytes with CRC32 0x8c58dcdc")
    save("collision-right.zip", archive([("collision.bin", b)]), "Same size and CRC32, genuinely different bytes")
    original = archive([("payload.bin", b"original payload")])
    save("payload-original.zip", original, "Intact stored payload")
    changed = original[:]
    changed[payload_start(changed)] ^= 0xFF
    save("payload-corrupted.zip", changed, "Payload changed without changing either header; metadata reader does not validate CRC")
    changed = deflated[:]
    changed[payload_start(changed)] = 0xFF
    save("deflate-corrupted.zip", changed, "Broken deflate payload; metadata reader does not decompress")

    unsafe = {
        "traversal": "../escape.txt", "nested-traversal": "safe/../escape.txt",
        "backslash-traversal": "safe\\..\\escape.txt", "absolute": "/escape.txt",
        "drive": "C:/escape.txt", "drive-relative": "C:escape.txt",
        "unc": "\\\\server\\share\\escape.txt", "colon": "name:stream",
        "reserved": "folder/CON.txt", "reserved-number": "COM1",
        "trailing-dot": "folder/file.", "trailing-space": "folder/file ",
        "control": "bad\x01name", "only-dot": ".",
        "format-language-tag": "safe\U000e0001/file", "format-tag-space": "safe\U000e0020/file",
        "format-tag-delete": "safe\U000e007f/file",
    }
    for key, name in unsafe.items():
        save("unsafe-" + key + ".zip", archive([("accepted-before-error.txt", b"safe"), (name, b"evil")]),
             "Unsafe member path: " + repr(name))
    save("unsafe-nul.zip", archive([(info(RawName(b"bad\x00name")), b"evil")]), "NUL byte in raw filename")
    save("unsafe-empty.zip", archive([(info(RawName(b"")), b"evil")]), "Empty raw filename")
    save("unsafe-link.zip", archive([(info("link", attrs=(stat.S_IFLNK | 0o777) << 16), b"../../escape")]),
         "Unix symlink member pointing outside root, never followed or extracted")
    duplicate_sets = {
        "exact": ["file", "file"], "nfc": ["caf\u00e9", "cafe\u0301"],
        "slash": ["a/b", "a\\b"], "dot": ["a/b", "a/./b"],
        "file-directory": ["same", "same/"], "ancestor": ["a", "a/b"],
        "ancestor-reverse": ["a/b", "a"],
    }
    for key, names in duplicate_sets.items():
        save("duplicate-" + key + ".zip", archive([(name, b"" if name.endswith("/") else b"x") for name in names]),
             "Conflicting normalized paths: " + repr(names))
    save("explicit-parent-after-child.zip", archive([("parent/child", b"x"), ("parent/", b"")]),
         "Legal explicit directory after its child is not a duplicate")
    save("invalid-utf8.zip", archive([(info(RawName(b"bad\xff.txt", 0x800)), b"x")]),
         "UTF-8 flag with invalid sequence")

    single = archive([("file.txt", b"1234567890")])
    for key, flag in [("encrypted", 1), ("strong-encrypted", 0x40)]:
        mutated = single[:]
        u16(mutated, 6, flag)
        u16(mutated, central(mutated)[0] + 8, flag)
        save(key + ".zip", mutated, "Encrypted flag set in both headers; unsupported")
    for key, method in [("unknown-method", 99), ("bzip2-method", 12)]:
        mutated = single[:]
        u16(mutated, 8, method)
        u16(mutated, central(mutated)[0] + 10, method)
        save(key + ".zip", mutated, "Unsupported ZIP compression method " + str(method))
    for key, offset, value in [("zip64-count", 10, 0xffff), ("zip64-directory-size", 12, 0xffffffff),
                               ("zip64-directory-offset", 16, 0xffffffff)]:
        mutated = single[:]
        (u16 if offset == 10 else u32)(mutated, eocd(mutated) + offset, value)
        save(key + ".zip", mutated, "ZIP64 EOCD sentinel")
    save("zip64-extra.zip", archive([(info("file", extra=struct.pack("<HHQQ", 1, 16, 1, 1)), b"x")]),
         "ZIP64 extra field with ZIP32-sized member")
    split = single[:]
    u16(split, eocd(split) + 4, 1)
    save("split.zip", split, "Multi-disk ZIP is unsupported")
    save("unknown.zip", b"this is not a ZIP archive", "Unrecognized signature")
    save("truncated.zip", single[:-10], "ZIP truncated in EOCD")
    save("truncated-central.zip", single[:central(single)[0] + 12], "Truncated central header")
    for key, offset, width, value in [
        ("local-method", 8, 2, 8), ("local-flags", 6, 2, 0x800),
        ("local-crc", 14, 4, 0), ("local-size", 22, 4, 999),
        ("local-compressed-size", 18, 4, 999),
    ]:
        mutated = single[:]
        (u16 if width == 2 else u32)(mutated, offset, value)
        save("corrupt-" + key + ".zip", mutated, "Local header disagrees with central header: " + key)
    mutated = archive([("first", b"safe"), ("second", b"data")])
    u32(mutated, local(mutated, 1), 0)
    save("corrupt-local-signature.zip", mutated, "Second local signature damaged; first ZIP signature remains recognizable")
    mutated = single[:]
    mutated[30] = ord("X")
    save("corrupt-local-name.zip", mutated, "Local and central raw filenames disagree")
    mutated = single[:]
    u32(mutated, central(mutated)[0] + 42, central(mutated)[0])
    save("corrupt-local-offset.zip", mutated, "Local offset points into central directory")
    mutated = single[:]
    u32(mutated, central(mutated)[0] + 20, 0x10000000)
    u32(mutated, 18, 0x10000000)
    save("corrupt-data-bounds.zip", mutated, "Declared payload overlaps central directory")
    mutated = archive([("first", b"1234567890"), ("second", b"abcdefghij")])
    first, second = central(mutated)
    extent = payload_start(mutated, 1) + 1 - payload_start(mutated)
    u32(mutated, 18, extent)
    u32(mutated, 22, extent)
    u32(mutated, first + 20, extent)
    u32(mutated, first + 24, extent)
    save("corrupt-overlap.zip", mutated, "First payload extent overlaps the second local header")
    save("corrupt-directory-size.zip", archive([("directory/", b"not empty")]),
         "Directory entry claims nonzero stored payload")
    mutated = single[:]
    u32(mutated, central(mutated)[0] + 24, 11)
    u32(mutated, 22, 11)
    save("corrupt-stored-size.zip", mutated, "Stored size and uncompressed size disagree")
    mutated = single[:]
    u16(mutated, eocd(mutated) + 8, 2)
    u16(mutated, eocd(mutated) + 10, 2)
    save("corrupt-count.zip", mutated, "EOCD member count exceeds central records")
    mutated = single[:]
    u32(mutated, eocd(mutated) + 12, 1)
    save("corrupt-central-size.zip", mutated, "Central directory byte count disagrees with records")
    save("corrupt-extra.zip", archive([(info("file", extra=b"\x42\x42\xff\xff"), b"x")]),
         "Extra field length extends beyond containing extra area")
    save("ratio.zip", archive([("ratio", b"A" * 10000)], zipfile.ZIP_DEFLATED), "Small deflate stream for ratio budget")
    save("deep.zip", archive([("a/b/c/file", b"data")]), "Depth and synthesized-parent budget fixture")

    if large:
        save("large-50000.zip", archive((("f%05d" % n, b"") for n in range(50000))),
             "Exactly 50,000 ZIP32 members, no parent directories, no payload allocation")
    for valid in ["empty.zip", "stored.zip", "deflated.zip", "deflated-directory.zip", "utf8.zip",
                  "cp437.zip", "descriptor.zip", "descriptor-unsigned.zip", "signature.jar",
                  "nested.zip", "compare-left.zip", "compare-right.zip", "collision-left.zip",
                  "collision-right.zip", "payload-original.zip", "ratio.zip"]:
        with zipfile.ZipFile(output / valid) as verify:
            assert verify.testzip() is None, "Valid fixture payload failed CRC: " + valid
    (output / "manifest.json").write_text(json.dumps(descriptions, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return descriptions


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--large", action="store_true")
    arguments = parser.parse_args()
    generated = generate(arguments.output, arguments.large)
    print("Generated %d fixtures in %s; no members extracted." % (len(generated), arguments.output))
