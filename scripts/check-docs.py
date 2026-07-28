#!/usr/bin/env python3

from __future__ import annotations

from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
SCREENSHOT_MANIFEST = ROOT / "scripts" / "doc-screenshot-manifest.tsv"
PLUGIN_GUI_ASSETS = DOCS / "assets" / "plugin-guis"
PLUGIN_GUI_MASTERS = PLUGIN_GUI_ASSETS / "masters"
LIGHTBOX_SCRIPT = DOCS / "lightbox.js"
MANIFEST_TOKEN_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
EXPECTED_TOP_NAV = [
    "index.html",
    "building-from-source.html",
    "installing-plugins.html",
    "multichannel.html",
    "ambisonics.html",
    "instruments.html",
    "references.html",
    "https://github.com/s3g/s3g-dsp",
]

DOC_SEQUENCE = [
    "index.html",
    "building-from-source.html",
    "installing-plugins.html",
    "stereo-listening.html",
    "multichannel.html",
    "multichannel-effects.html",
    "topology-processors.html",
    "topology-framework.html",
    "delay-processor.html",
    "wave-geometry-processor.html",
    "spectral-topology-processor.html",
    "macro-processors.html",
    "macro-shred.html",
    "spectral-buffer-processors.html",
    "distributed-output-processors.html",
    "crcltr.html",
    "monitoring-fold-down.html",
    "mc-to-stereo-autogain.html",
    "mc-to-quad-autogain.html",
    "multichannel-meter.html",
    "direct-panning.html",
    "layout-panner.html",
    "bus-mixing-routing.html",
    "speaker-array-utilities.html",
    "ambisonics.html",
    "interpreting-color.html",
    "listener-mode.html",
    "parameter-surface.html",
    "ambisonic-encoders.html",
    "ambi-point-encoder.html",
    "ambi-cloud-encoder.html",
    "ambi-terrain-navigator.html",
    "ambi-path-encoder.html",
    "ambi-ray-encoder.html",
    "ambi-ray-bilocation-encoder.html",
    "accelerometer-field-encoder.html",
    "ambi-vot-encoder.html",
    "ambi-vox-encoder.html",
    "ambi-wave-terrain-encoder.html",
    "ambi-wind-encoder.html",
    "ambi-water-encoder.html",
    "ambi-pyrosphere-encoder.html",
    "ambi-cryosphere-encoder.html",
    "ambi-insect-encoder.html",
    "ambi-pulsar-encoder.html",
    "ambi-neural-ecology.html",
    "ambi-stochastic-encoder.html",
    "ambi-wrangler-encoder.html",
    "ambisonic-decoders.html",
    "ambi-speaker-decoder.html",
    "ambisonic-stereo-decoder.html",
    "ambisonic-head-decoder.html",
    "ambisonic-effects.html",
    "ambi-effect-dj-filter.html",
    "ambi-effect-delay.html",
    "ambi-effect-pitch.html",
    "ambi-effect-gain.html",
    "ambi-effect-resonance-print.html",
    "ambi-effect-partial-trace.html",
    "ambi-effect-response-trace.html",
    "ambi-effect-displacement.html",
    "ambi-imprint.html",
    "ambisonic-rotate.html",
    "ambisonic-bus-processors.html",
    "ambi-grain-processor.html",
    "ambisonic-utilities.html",
    "ambisonic-order-band-tool.html",
    "ambisonic-energy-visualizer.html",
    "s3gimprint-format.html",
    "s3gray-format.html",
    "instruments.html",
    "generative-instruments.html",
    "no-input-mixer.html",
    "fault.html",
    "sample-instruments.html",
    "loop-processor.html",
    "multi-loop-processor.html",
    "voice-instruments.html",
    "vox-builder.html",
    "references.html",
]

REDIRECT_PAGES = {
    "effects.html",
    "mix-pan.html",
    "processors.html",
}


class Document(HTMLParser):
    def __init__(self, path: Path) -> None:
        super().__init__(convert_charrefs=True)
        self.path = path
        self.ids: set[str] = set()
        self.references: list[tuple[str, str]] = []
        self.errors: list[str] = []
        self.has_title = False
        self.has_description = False
        self.has_references_heading = False
        self.bibliography_depth = 0
        self.bibliography_count = 0
        self.bib_entry_depth = 0
        self.current_bib_entry: list[str] = []
        self.bib_entries: list[str] = []
        self.current_bib_group: list[str] = []
        self.bib_groups: list[list[str]] = []
        self.bibliography_links: list[str] = []
        self.main_depth = 0
        self.aside_depth = 0
        self.content_section_ids: list[str] = []
        self.current_content_section = ""
        self.has_order_routing_heading = False
        self.routing_heading_depth = 0
        self.current_routing_heading: list[str] = []
        self.routing_table_count = 0
        self.has_controls_heading = False
        self.controls_table_count = 0
        self.has_signal_flow_heading = False
        self.signal_flow_ordered_list_count = 0
        self.toc_fragment_ids: list[str] = []
        self.top_nav_depth = 0
        self.top_nav_links: list[str] = []
        self.page_nav_depth = 0
        self.page_nav_links: list[tuple[str, str]] = []
        self.images: list[dict[str, str]] = []
        self.lightbox_triggers: list[dict[str, str]] = []
        self.script_sources: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {name: value or "" for name, value in attrs}
        classes = set(values.get("class", "").split())
        if tag == "main":
            self.main_depth += 1
        if tag == "aside":
            self.aside_depth += 1
        if tag == "nav" and "top-nav" in classes:
            self.top_nav_depth += 1
        if tag == "nav" and "page-nav" in classes:
            self.page_nav_depth += 1
        if values.get("id"):
            self.ids.add(values["id"])
        if self.main_depth and tag in {"h2", "h3"} and values.get("id"):
            self.content_section_ids.append(values["id"])
            self.current_content_section = values["id"]
            if values["id"] == "routing":
                self.routing_heading_depth = 1
                self.current_routing_heading = []
            if values["id"] == "controls":
                self.has_controls_heading = True
            if values["id"] == "signal-flow":
                self.has_signal_flow_heading = True
        if self.aside_depth and tag == "a" and values.get("href", "").startswith("#"):
            self.toc_fragment_ids.append(values["href"][1:])
        if self.top_nav_depth and tag == "a" and values.get("href"):
            self.top_nav_links.append(values["href"])
        if self.page_nav_depth and tag == "a" and values.get("href"):
            direction = "next" if "next" in classes else "previous"
            self.page_nav_links.append((direction, values["href"]))
        if values.get("name") and tag == "a":
            self.ids.add(values["name"])
        if tag == "title":
            self.has_title = True
        if tag == "meta" and values.get("name", "").lower() == "description" and values.get("content"):
            self.has_description = True
        if tag == "h2" and values.get("id") == "references":
            self.has_references_heading = True
        if tag == "div" and "bibliography" in classes:
            self.bibliography_depth += 1
            self.bibliography_count += 1
            self.current_bib_group = []
        if self.main_depth and tag == "table" and self.current_content_section == "routing" and "readout-table" in classes:
            self.routing_table_count += 1
        if self.main_depth and tag == "table" and self.current_content_section == "controls" and "readout-table" in classes:
            self.controls_table_count += 1
        if self.main_depth and tag == "ol" and self.current_content_section == "signal-flow":
            self.signal_flow_ordered_list_count += 1
        if self.bibliography_depth and tag == "p" and "bib-entry" in classes:
            self.bib_entry_depth += 1
            self.current_bib_entry = []
        if tag in {"a", "link"} and values.get("href"):
            self.references.append((tag, values["href"]))
            if self.bibliography_depth and tag == "a":
                self.bibliography_links.append(values["href"])
        if tag in {"img", "script"} and values.get("src"):
            self.references.append((tag, values["src"]))
        if tag == "img" and values.get("src"):
            self.images.append(values)
        if tag == "button" and "data-lightbox-image" in values:
            self.lightbox_triggers.append(values)
        if tag == "script" and values.get("src"):
            self.script_sources.append(values["src"])
        if tag == "img" and "alt" not in values:
            self.errors.append("image is missing alt text")
        if tag == "a" and values.get("target") == "_blank":
            rel = set(values.get("rel", "").split())
            if "noopener" not in rel:
                self.errors.append(f'external link is missing rel="noopener": {values.get("href", "")}')

    def handle_data(self, data: str) -> None:
        if self.routing_heading_depth:
            self.current_routing_heading.append(data)
        if self.bib_entry_depth:
            self.current_bib_entry.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag in {"h2", "h3"} and self.routing_heading_depth:
            heading = " ".join("".join(self.current_routing_heading).split())
            if heading == "Order and Routing":
                self.has_order_routing_heading = True
            self.current_routing_heading = []
            self.routing_heading_depth = 0
        if tag == "p" and self.bib_entry_depth:
            entry = " ".join("".join(self.current_bib_entry).split())
            self.bib_entries.append(entry)
            self.current_bib_group.append(entry)
            self.current_bib_entry = []
            self.bib_entry_depth -= 1
        if tag == "div" and self.bibliography_depth:
            self.bib_groups.append(self.current_bib_group)
            self.current_bib_group = []
            self.bibliography_depth -= 1
        if tag == "main" and self.main_depth:
            self.main_depth -= 1
        if tag == "aside" and self.aside_depth:
            self.aside_depth -= 1
        if tag == "nav" and self.top_nav_depth:
            self.top_nav_depth -= 1
        if tag == "nav" and self.page_nav_depth:
            self.page_nav_depth -= 1


def parse_document(path: Path) -> Document:
    document = Document(path)
    document.feed(path.read_text(encoding="utf-8"))
    document.close()
    if not document.has_title:
        document.errors.append("missing title element")
    if not document.has_description:
        document.errors.append("missing meta description")
    is_references_page = path.name == "references.html"
    if is_references_page:
        if not document.bibliography_count:
            document.errors.append("central References page has no bibliography groups")
        if not document.bib_entries:
            document.errors.append("central References page has no entries")
        for group in document.bib_groups:
            if not group:
                document.errors.append("central References page has an empty bibliography group")
    else:
        if document.has_references_heading:
            document.errors.append("page-level References sections belong on references.html")
        if document.bibliography_count:
            document.errors.append("bibliography blocks belong on references.html")
    for entry in document.bib_entries:
        if not re.search(r"(?:n\.d\.|(?:19|20)\d{2}\.)", entry[:160]):
            document.errors.append(f"bibliography entry is not author-date: {entry[:80]}")
    for group in document.bib_groups:
        sort_keys: list[str] = []
        for entry in group:
            match = re.match(r"^(.+?)\.\s+(?:n\.d\.|(?:19|20)\d{2}\.)", entry)
            if match:
                sort_keys.append(match.group(1).casefold())
        if len(sort_keys) == len(group) and sort_keys != sorted(sort_keys):
            document.errors.append("bibliography group is not alphabetized by author")
    if len(document.bib_entries) != len(set(document.bib_entries)):
        document.errors.append("central References page contains duplicate entries")
    for reference in document.bibliography_links:
        parts = urlsplit(reference)
        if parts.scheme not in {"http", "https"} or not parts.netloc:
            document.errors.append(f"bibliography contains an internal link: {reference}")
    for section_id in document.content_section_ids:
        if section_id not in document.toc_fragment_ids:
            document.errors.append(f"On This Page is missing #{section_id}")
    for fragment_id in document.toc_fragment_ids:
        if fragment_id not in document.content_section_ids:
            document.errors.append(f"On This Page contains unknown section #{fragment_id}")
    if document.has_order_routing_heading and document.routing_table_count != 1:
        document.errors.append("Order and Routing must contain exactly one readout table")
    if document.has_controls_heading and document.controls_table_count != 1:
        document.errors.append("Controls must contain exactly one readout table")
    if document.has_signal_flow_heading and document.signal_flow_ordered_list_count != 1:
        document.errors.append("Signal Flow must contain exactly one ordered list")
    if document.top_nav_links != EXPECTED_TOP_NAV:
        document.errors.append("top navigation does not match the documentation baseline")
    return document


def parse_screenshot_manifest(path: Path) -> tuple[dict[str, str], list[str]]:
    entries: dict[str, str] = {}
    errors: list[str] = []
    id_lines: dict[str, int] = {}
    stem_lines: dict[str, int] = {}
    label = path.relative_to(ROOT)

    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        return {}, [f"{label}: cannot read screenshot manifest: {error}"]

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        fields = raw_line.split("\t")
        if len(fields) != 2 or any(field != field.strip() for field in fields):
            errors.append(f"{label}:{line_number}: expected exactly two tab-separated fields")
            continue

        plugin_id, stem = fields
        malformed_fields: list[str] = []
        if not MANIFEST_TOKEN_PATTERN.fullmatch(plugin_id):
            malformed_fields.append("plugin ID")
        if not MANIFEST_TOKEN_PATTERN.fullmatch(stem):
            malformed_fields.append("asset stem")
        if malformed_fields:
            errors.append(f"{label}:{line_number}: malformed {' and '.join(malformed_fields)}")
            continue

        duplicate = False
        stem_key = stem.casefold()
        if plugin_id in id_lines:
            errors.append(
                f"{label}:{line_number}: duplicate plugin ID {plugin_id!r} "
                f"(first seen on line {id_lines[plugin_id]})"
            )
            duplicate = True
        else:
            id_lines[plugin_id] = line_number
        if stem_key in stem_lines:
            errors.append(
                f"{label}:{line_number}: duplicate asset stem {stem!r} "
                f"(first seen on line {stem_lines[stem_key]})"
            )
            duplicate = True
        else:
            stem_lines[stem_key] = line_number
        if duplicate:
            continue

        entries[plugin_id] = stem

    if not entries:
        errors.append(f"{label}: screenshot manifest contains no valid entries")
    return entries, errors


def local_target(source: Path, reference: str) -> tuple[Path, str] | None:
    parts = urlsplit(reference)
    if parts.scheme or parts.netloc or reference.startswith("//"):
        return None
    path_text = unquote(parts.path)
    target = source if not path_text else (source.parent / path_text)
    if target.is_dir():
        target = target / "index.html"
    return target.resolve(), unquote(parts.fragment)


def plugin_gui_target(source: Path, reference: str) -> Path | None:
    target_info = local_target(source, reference)
    if target_info is None:
        return None
    target, _ = target_info
    if target.parent != PLUGIN_GUI_ASSETS.resolve() or target.suffix.lower() != ".png":
        return None
    return target


def belongs_to_manifest_base(stem: str, manifest_stems: set[str]) -> bool:
    return stem in manifest_stems or any(
        stem.startswith(f"{base}.") and len(stem) > len(base) + 1
        for base in manifest_stems
    )


def format_reference_counts(counts: Counter[Path]) -> str:
    if not counts:
        return "none"
    return ", ".join(
        f"{path.relative_to(DOCS)} ({count})"
        for path, count in sorted(counts.items(), key=lambda item: str(item[0]))
    )


def main() -> int:
    pages = {path.resolve(): parse_document(path) for path in sorted(DOCS.glob("*.html"))}
    errors: list[str] = []
    local_reference_count = 0
    manifest, manifest_errors = parse_screenshot_manifest(SCREENSHOT_MANIFEST)
    errors.extend(manifest_errors)
    manifest_stems = set(manifest.values())
    referenced_plugin_gui_images: set[Path] = set()

    for stem in sorted(manifest_stems):
        base_png = PLUGIN_GUI_ASSETS / f"{stem}.png"
        master_pdf = PLUGIN_GUI_MASTERS / f"{stem}.pdf"
        if not base_png.is_file():
            errors.append(
                f"{SCREENSHOT_MANIFEST.relative_to(ROOT)}: missing base PNG "
                f"{base_png.relative_to(ROOT)}"
            )
        if not master_pdf.is_file():
            errors.append(
                f"{SCREENSHOT_MANIFEST.relative_to(ROOT)}: missing PDF master "
                f"{master_pdf.relative_to(ROOT)}"
            )

    page_names = {path.name for path in pages}
    sequenced_pages = set(DOC_SEQUENCE)
    expected_pages = sequenced_pages | REDIRECT_PAGES
    for missing_page in sorted(expected_pages - page_names):
        errors.append(f"documentation sequence references missing page docs/{missing_page}")
    for unsequenced_page in sorted(page_names - expected_pages):
        errors.append(f"docs/{unsequenced_page}: page is not in DOC_SEQUENCE or REDIRECT_PAGES")

    for index, page_name in enumerate(DOC_SEQUENCE):
        path = (DOCS / page_name).resolve()
        document = pages.get(path)
        if document is None:
            continue
        expected_page_nav: list[tuple[str, str]] = []
        if index:
            expected_page_nav.append(("previous", DOC_SEQUENCE[index - 1]))
        if index + 1 < len(DOC_SEQUENCE):
            expected_page_nav.append(("next", DOC_SEQUENCE[index + 1]))
        if document.page_nav_links != expected_page_nav:
            errors.append(
                f"docs/{page_name}: page navigation is {document.page_nav_links}, "
                f"expected {expected_page_nav}"
            )

    for path, document in list(pages.items()):
        relative = path.relative_to(ROOT)
        errors.extend(f"{relative}: {message}" for message in document.errors)
        page_images: list[Path] = []
        page_triggers: list[Path] = []

        for image in document.images:
            source = image["src"]
            target = plugin_gui_target(path, source)
            if target is None:
                continue
            page_images.append(target)
            referenced_plugin_gui_images.add(target)

            if not image.get("alt", "").strip():
                errors.append(f"{relative}: generated screenshot has empty alt text: {source}")
            if image.get("loading", "").casefold() != "lazy":
                errors.append(f'{relative}: generated screenshot is missing loading="lazy": {source}')
            if image.get("decoding", "").casefold() != "async":
                errors.append(f'{relative}: generated screenshot is missing decoding="async": {source}')

            stem = target.stem
            if not belongs_to_manifest_base(stem, manifest_stems):
                errors.append(
                    f"{relative}: generated screenshot stem {stem!r} does not belong "
                    "to a screenshot manifest base"
                )
            master_pdf = PLUGIN_GUI_MASTERS / f"{stem}.pdf"
            if not master_pdf.is_file():
                errors.append(
                    f"{relative}: generated screenshot has no matching PDF master: "
                    f"{master_pdf.relative_to(ROOT)}"
                )

        for trigger in document.lightbox_triggers:
            source = trigger.get("data-lightbox-image", "")
            target = plugin_gui_target(path, source)
            if target is None:
                continue
            page_triggers.append(target)
            if trigger.get("type", "").casefold() != "button":
                errors.append(f'{relative}: screenshot trigger must have type="button": {source}')
            if not trigger.get("aria-label", "").strip():
                errors.append(f"{relative}: screenshot trigger has empty aria-label: {source}")

        if page_images or page_triggers:
            image_counts = Counter(page_images)
            trigger_counts = Counter(page_triggers)
            if image_counts != trigger_counts:
                errors.append(
                    f"{relative}: generated screenshot/lightbox paths do not match "
                    f"(img: {format_reference_counts(image_counts)}; "
                    f"button: {format_reference_counts(trigger_counts)})"
                )

            lightbox_script_count = 0
            for source in document.script_sources:
                target_info = local_target(path, source)
                if target_info is not None and target_info[0] == LIGHTBOX_SCRIPT.resolve():
                    lightbox_script_count += 1
            if lightbox_script_count != 1:
                errors.append(
                    f"{relative}: page with generated screenshots must include lightbox.js "
                    f"exactly once (found {lightbox_script_count})"
                )

        for _, reference in document.references:
            target_info = local_target(path, reference)
            if target_info is None:
                continue
            local_reference_count += 1
            target, fragment = target_info
            if not target.exists():
                errors.append(f"{relative}: missing local target {reference}")
                continue
            if fragment and target.suffix.lower() == ".html":
                target_document = pages.get(target)
                if target_document is None:
                    target_document = parse_document(target)
                    pages[target] = target_document
                if fragment not in target_document.ids:
                    errors.append(f"{relative}: missing fragment #{fragment} in {target.relative_to(ROOT)}")

    for stem in sorted(manifest_stems):
        base_png = (PLUGIN_GUI_ASSETS / f"{stem}.png").resolve()
        if base_png not in referenced_plugin_gui_images:
            errors.append(
                f"{SCREENSHOT_MANIFEST.relative_to(ROOT)}: base PNG is not referenced "
                f"by any docs HTML image: {base_png.relative_to(ROOT)}"
            )

    if errors:
        print("Documentation check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"Documentation check passed: {len(pages)} HTML pages, {local_reference_count} local references")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
