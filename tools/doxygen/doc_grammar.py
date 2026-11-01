"""Small shared readers for the Doxygen XML emitted by our reference pages."""
from dataclasses import dataclass
import re


@dataclass(frozen=True)
class ParSection:
    kind: str
    title: str
    paragraph: str
    verbatim: str | None


def text(node):
    """Return normalized descendant text, or an empty string for no node."""
    return ' '.join(''.join(node.itertext()).split()) if node is not None else ''


def verbatim(section, *, preserve_blank_lines=False):
    node = section.find('./para/verbatim')
    if node is None:
        raise ValueError('reference section has no verbatim content')
    lines = (node.text or '').splitlines()
    result = []
    for line in lines:
        if not line.strip():
            if preserve_blank_lines:
                result.append('')
            continue
        if not re.match(r'^\s*\*(?: |$)', line):
            raise ValueError('unexpected verbatim format')
        content = re.sub(r'^\s*\* ?', '', line)
        if content or preserve_blank_lines:
            result.append(content)
    value = '\n'.join(result)
    return value.strip('\n') if preserve_blank_lines else value.strip()


def par_sections(nodes):
    """Parse Doxygen @par sections into typed records without interpreting titles."""
    result = []
    for node in nodes:
        para = node.find('para')
        verb = node.find('./para/verbatim')
        result.append(ParSection(
            kind=node.get('kind') or '',
            title=text(node.find('title')),
            paragraph=text(para) if verb is None else '',
            verbatim=verbatim(node) if verb is not None else None,
        ))
    return result
