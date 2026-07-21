#!/usr/bin/env python3

from __future__ import annotations

from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "docs"


class Document(HTMLParser):
    def __init__(self, path: Path) -> None:
        super().__init__(convert_charrefs=True)
        self.path = path
        self.ids: set[str] = set()
        self.references: list[tuple[str, str]] = []
        self.errors: list[str] = []
        self.has_title = False
        self.has_description = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {name: value or "" for name, value in attrs}
        if values.get("id"):
            self.ids.add(values["id"])
        if values.get("name") and tag == "a":
            self.ids.add(values["name"])
        if tag == "title":
            self.has_title = True
        if tag == "meta" and values.get("name", "").lower() == "description" and values.get("content"):
            self.has_description = True
        if tag in {"a", "link"} and values.get("href"):
            self.references.append((tag, values["href"]))
        if tag in {"img", "script"} and values.get("src"):
            self.references.append((tag, values["src"]))
        if tag == "img" and "alt" not in values:
            self.errors.append("image is missing alt text")
        if tag == "a" and values.get("target") == "_blank":
            rel = set(values.get("rel", "").split())
            if "noopener" not in rel:
                self.errors.append(f'external link is missing rel="noopener": {values.get("href", "")}')


def parse_document(path: Path) -> Document:
    document = Document(path)
    document.feed(path.read_text(encoding="utf-8"))
    document.close()
    if not document.has_title:
        document.errors.append("missing title element")
    if not document.has_description:
        document.errors.append("missing meta description")
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
