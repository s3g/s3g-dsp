#!/usr/bin/env python3

from __future__ import annotations

from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"
EXPECTED_TOP_NAV = [
    "index.html",
    "building-from-source.html",
    "installing-plugins.html",
    "multichannel.html",
    "3oafx.html",
    "instruments.html",
    "references.html",
    "https://github.com/s3g/s3g-dsp",
]


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

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {name: value or "" for name, value in attrs}
        classes = set(values.get("class", "").split())
        if tag == "main":
            self.main_depth += 1
        if tag == "aside":
            self.aside_depth += 1
        if tag == "nav" and "top-nav" in classes:
            self.top_nav_depth += 1
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


def local_target(source: Path, reference: str) -> tuple[Path, str] | None:
    parts = urlsplit(reference)
    if parts.scheme or parts.netloc or reference.startswith("//"):
        return None
    path_text = unquote(parts.path)
    target = source if not path_text else (source.parent / path_text)
    if target.is_dir():
        target = target / "index.html"
    return target.resolve(), unquote(parts.fragment)


def main() -> int:
    pages = {path.resolve(): parse_document(path) for path in sorted(DOCS.glob("*.html"))}
    errors: list[str] = []
    local_reference_count = 0

    for path, document in list(pages.items()):
        relative = path.relative_to(ROOT)
        errors.extend(f"{relative}: {message}" for message in document.errors)
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

    if errors:
        print("Documentation check failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"Documentation check passed: {len(pages)} HTML pages, {local_reference_count} local references")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
