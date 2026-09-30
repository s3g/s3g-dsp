"""Source-level guard, not a substitute for the CLAP snapshot integration tests.

Follow locally defined helpers from every Sample-family state saver, including
shared variants and .inc files. No sample-file read, copy, hash or export belongs
on that synchronous call path. Worker queueing is allowed; the worker itself
must not be invoked synchronously by the saver.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRODUCTS = {
    name: f"clap_sample_{name}"
    for name in ("player", "doubles", "motion", "wavesets", "kit", "lanes",
                 "grains", "cutups", "rings", "neon", "decks")
}
PRODUCTS.update(slicer="clap_breakbeat_slicer", circulator="clap_crcltr",
                ambi_grain="clap_ambi_grain_processor")
FORBIDDEN = {
    "copyFileIntoProject", "hashFile", "writePlanarFloatWaveAtomically",
    "writeRenderedWaveFile", "decodeSampleFile", "decodeWaveFile",
    "readSampleFromPath", "readAudioFile", "fopen", "fread", "ifstream",
    "ofstream", "copy_file",
}


def code_only(text):
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: "\n" * m[0].count("\n") + " ", text)


def bodies(text):
    text = code_only(text)
    # Repository free-function definitions, including multiline return types.
    pattern = r"^\s*(?:[\w:<>,*&]+\s+)+(?P<name>\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{"
    result = {}
    for match in re.finditer(pattern, text, re.MULTILINE):
        if match["name"] in {"if", "for", "while", "switch", "catch"}:
            continue
        start = match.end(); end = start; depth = 1
        while end < len(text) and depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        result.setdefault(match["name"], []).append(text[start:end - 1])
    return result


def forbidden_path(functions, entry):
    pending = [(entry, [entry])]; seen = set()
    while pending:
        name, path = pending.pop()
        if name in FORBIDDEN:
            return path
        if name in seen:
            continue
        seen.add(name)
        for body in functions.get(name, []):
            # Only a forced asynchronous launch is a boundary. Deferred tasks,
            # direct helper calls and disk work before/after launch stay visible.
            launch = r"std::async\s*\(\s*std::launch::async\s*,\s*\[[^\]]*\]\s*\([^)]*\)\s*(?:mutable\s*)?\{"
            while match := re.search(launch, body):
                end = match.end(); depth = 1
                while end < len(body) and depth:
                    depth += (body[end] == "{") - (body[end] == "}"); end += 1
                body = body[:match.start()] + "worker_boundary()" + body[end:]
            calls = re.findall(r"(?<![\w.>])\b(\w+)\s*\(", body)
            # Selected member APIs with unambiguous names. Treating every
            # atomic.load() as the plug-in's load(files) gives false positives.
            calls += re.findall(r"(?:\.|->)\s*(reference)\s*\(", body)
            for call in calls:
                pending.append((call, path + [call]))
    return None


class SampleSnapshotContract(unittest.TestCase):
    def test_all_family_save_paths(self):
        for name, folder in PRODUCTS.items():
            with self.subTest(product=name):
                folders = [ROOT / "plugins" / folder]
                if name in ("grains", "cutups"):
                    folders.append(ROOT / "plugins/clap_sample_lanes")
                files = [p for d in folders for p in d.iterdir()
                         if p.suffix in (".cpp", ".inc", ".h")]
                files.append(ROOT / "plugins/common/s3g_sample_storage.h")
                files.append(ROOT / "plugins/common/s3g_generated_sample_media.h")
                functions = bodies("\n".join(p.read_text() for p in files))
                entry = "saveState" if name == "decks" else "stateSave"
                self.assertIn(entry, functions, "audit must never silently skip a saver")
                self.assertIsNone(forbidden_path(functions, entry),
                                  f"{name}: synchronous file work in state snapshot")

    def test_catches_original_decks_regression_through_helper(self):
        fixture = """
bool saveState(const void* p) { return collect(p); }
bool collect(const void* p) { return storage::copyFileIntoProject(p); }
"""
        self.assertEqual(forbidden_path(bodies(fixture), "saveState"),
                         ["saveState", "collect", "copyFileIntoProject"])

    def test_comments_do_not_create_a_false_pass_or_failure(self):
        fixture = """
bool stateSave(const void* p) {
    // Never call hashFile(p); it can freeze the host.
    return writeAll(p, "hashFile() }", 12);
}
"""
        self.assertIsNone(forbidden_path(bodies(fixture), "stateSave"))

    def test_generated_reference_lookup_cannot_hide_io_in_member(self):
        fixture = """
class Media {
    std::string reference(const void* p) const { return hashFile(p); }
};
bool stateSave(const void* p) { return p->media.reference(p); }
"""
        self.assertEqual(forbidden_path(bodies(fixture), "stateSave"),
                         ["stateSave", "reference", "hashFile"])

    def test_only_forced_async_is_a_boundary(self):
        fixture = """
bool stateSave(const void* p) {
    auto task = std::async(std::launch::async, [p]() mutable { hashFile(p); });
    return true;
}
"""
        self.assertIsNone(forbidden_path(bodies(fixture), "stateSave"))
        self.assertIsNotNone(forbidden_path(bodies(fixture.replace("launch::async", "launch::deferred")), "stateSave"))
        self.assertIsNotNone(forbidden_path(bodies(fixture.replace("return true;", "hashFile(p); return true;")), "stateSave"))


if __name__ == "__main__":
    unittest.main()
