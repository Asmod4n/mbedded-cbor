#!/usr/bin/env python3
"""Generate a classic section-3 manual page for one documented free function."""
import argparse
import datetime
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from api_model import parse_compound
from doc_grammar import named_errors


def _inline(records, xrefs):
    out = []
    for item in records:
        if item.kind in ('text', 'paragraph_break'):
            out.append(item.text)
        elif item.kind == 'ref':
            if item.target not in xrefs:
                raise ValueError(f'unresolved Doxygen reference {item.target!r}; supply --xref {item.target}=MANPAGE')
            out.append(f'[{item.text}]({xrefs[item.target]})')
        elif item.kind == 'ulink':
            out.append(f'[{item.text}]({item.target})')
        else:
            raise ValueError(f'unsupported inline content {item.kind!r}')
    return ''.join(out).strip()


def _md(text):
    """Escape prose for Markdown while preserving paragraph boundaries."""
    return '\n\n'.join(re.sub(r'([\\`*_{}\[\]<>])', r'\\\1', p)
                       for p in text.split('\n\n'))


def _section_text(section, xrefs):
    return _inline(section.inline, xrefs) if section.inline else _md(section.text)


def _signature(fn):
    prefix, sep, suffix = fn.signature.partition(fn.qualified_name)
    if not sep:
        raise ValueError('function signature does not contain its qualified name')
    if fn.template_parameters:
        params = []
        for p in fn.template_parameters:
            value = ' '.join(x for x in (p.type, p.name) if x)
            if p.default:
                value += ' = ' + p.default
            params.append(value)
        prefix = 'template <' + ', '.join(params) + '>\n' + prefix
    if fn.requires:
        suffix += ' requires ' + fn.requires
    return prefix + fn.name + suffix + ';'


def _collect_function(fn, xrefs):
    desc, params, returns, throws, complexity, examples, see = [], {}, [], [], [], [], []
    error_sections = []
    for section in fn.sections:
        text = _section_text(section, xrefs)
        if section.kind == 'description':
            if text:
                desc.append(text)
        elif section.kind == 'param':
            for name in section.parameters:
                if name in params:
                    raise ValueError(f'duplicate documentation for parameter {name!r}')
                params[name] = text
        elif section.kind == 'return':
            returns.append(text)
        elif section.kind == 'throws':
            throws.extend((name, text) for name in section.parameters)
        elif section.kind == 'par':
            if section.title in ('Complexity',):
                complexity.append(text)
            elif re.fullmatch(r'Interactive example(?: [1-9][0-9]*)?', section.title):
                # Website-only material remains parsed and validated by the
                # shared model, but does not belong on the classic manual page.
                if section.verbatim is None:
                    raise ValueError('Interactive example section must be verbatim')
            elif section.title in ('Example', 'Examples'):
                if section.verbatim is None:
                    raise ValueError('Example section must be verbatim code')
                examples.append(section.verbatim)
            elif section.title == 'See also':
                if section.verbatim is None:
                    raise ValueError('See also section must be verbatim text')
                # The classic docs use Markdown links here. Render mapped
                # targets once, and retain any separate prose such as a section
                # number after the link.
                raw = section.verbatim
                link_matches = list(re.finditer(r'\[([^\]]+)\]\(([^)]+)\)', raw))
                links = [match.group(2) for match in link_matches]
                residue = re.sub(r'\[[^\]]+\]\([^)]+\)', '', raw)
                if '[' in residue or '](' in residue:
                    raise ValueError('malformed See also link markup')
                for match in link_matches:
                    label, target = match.groups()
                    if re.match(r'https?://', target):
                        see.append(f'[{label}]({target})')
                    elif target not in xrefs:
                        raise ValueError(f'unresolved See also target {target!r}; supply --xref {target}=MANPAGE')
                    else:
                        see.append(xrefs[target])
                residue = re.sub(r'(?m)^\s*\*\s*$', '', residue)
                residue = re.sub(r'\s*[·,]\s*', ' ', residue)
                residue = re.sub(r'\s+', ' ', residue).strip()
                if residue and not any(residue in xrefs[t] for t in links if t in xrefs):
                    see.append(residue)
            elif section.title in ('Limits and errors', 'Errors'):
                error_sections.append(text)
            else:
                raise ValueError(f'unsupported @par section {section.title!r}')
        else:
            raise ValueError(f'unsupported documentation section {section.kind!r}')
    if len(returns) > 1:
        raise ValueError('duplicate @return documentation')
    expected = [p.name for p in fn.parameters if p.name]
    unknown = set(params) - set(expected)
    if unknown:
        raise ValueError('documentation for unknown parameters: ' + ', '.join(sorted(unknown)))

    brief = _inline(fn.brief_inline, xrefs) if fn.brief_inline else _md(fn.brief)
    return {'brief': brief, 'signature': _signature(fn), 'description': desc,
            'parameters': [(name, params[name]) for name in expected if name in params],
            'returns': returns, 'throws': throws, 'errors': error_sections,
            'complexity': complexity, 'examples': examples, 'see': see}


def _render_page(qualified_symbol, records, date):
    namespace_parts = qualified_symbol.split('::')[:-1]
    md = f'% {qualified_symbol}(3)\n%\n% {date}\n\n'
    md += f'# NAME\n\n{qualified_symbol} - {records[0]["brief"]}\n'
    declarations = '\n\n'.join(r['signature'] for r in records)
    for part in reversed(namespace_parts):
        declarations = f'namespace {part} {{\n{declarations}\n}}'
    md += '\n# SYNOPSIS\n\n```cpp\n#include <cbor/cbor.hpp>\n\n' + declarations + '\n```\n'

    def grouped(key, formatter):
        values = []
        for record in records:
            value = formatter(record[key])
            if value:
                values.append(value)
        return '\n\n'.join(values)

    def label(index, value):
        return value if len(records) == 1 else f'**Overload {index}:** {value}'

    descriptions = [label(i, '\n\n'.join(r['description'])) for i, r in enumerate(records, 1) if r['description']]
    if descriptions:
        md += '\n# DESCRIPTION\n\n' + '\n\n'.join(descriptions) + '\n'
    params = []
    for i, r in enumerate(records, 1):
        if r['parameters']:
            prefix = f'**Overload {i}:**\n' if len(records) > 1 else ''
            params.extend([prefix + f'- `{name}`: {text}' for name, text in r['parameters']])
    if params:
        md += '\n# PARAMETERS\n\n' + '\n'.join(params) + '\n'
    returns = grouped('returns', lambda values: '\n\n'.join(values))
    if len(records) > 1:
        returns = '\n\n'.join(f'**Overload {i}:** {" ".join(r["returns"])}'
                                for i, r in enumerate(records, 1) if r['returns'])
    if returns:
        md += '\n# RETURN VALUE\n\n' + returns + '\n'
    errors = []
    for i, r in enumerate(records, 1):
        body = [f'- `{name}`: {text}' for name, text in r['throws']]
        for value in r['errors']:
            body.extend(f'- `{name}`: {message}' for name, message in named_errors(value))
        if body:
            errors.append((f'**Overload {i}:**\n' if len(records) > 1 else '') + '\n'.join(body))
    if errors:
        md += '\n# ERRORS\n\n' + '\n\n'.join(errors) + '\n'
    complexity = grouped('complexity', lambda values: '\n\n'.join(values))
    if len(records) > 1:
        complexity = '\n\n'.join(f'**Overload {i}:** {" ".join(r["complexity"])}'
                                   for i, r in enumerate(records, 1) if r['complexity'])
    if complexity:
        md += '\n# NOTES\n\n' + complexity + '\n'
    examples = grouped('examples', lambda values: '\n\n'.join('```cpp\n' + code + '\n```' for code in values))
    if len(records) > 1:
        examples = '\n\n'.join(f'**Overload {i}:**\n' + '\n\n'.join('```cpp\n' + code + '\n```' for code in r['examples'])
                                for i, r in enumerate(records, 1) if r['examples'])
    if examples:
        md += '\n# EXAMPLES\n\n' + examples + '\n'
    see = list(dict.fromkeys(item for r in records for item in r['see']))
    if see:
        md += '\n# SEE ALSO\n\n' + ', '.join(see) + '\n'
    return md


def render(xml_dir, qualified_symbol, *, version, date, xrefs=()):
    parts = qualified_symbol.split('::')
    if len(parts) < 2 or any(not p for p in parts):
        raise ValueError('expected a fully qualified free-function symbol')
    namespace = '::'.join(parts[:-1])
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*', namespace):
        raise ValueError('unsupported namespace spelling for Doxygen XML filename')
    filename = 'namespace' + namespace.replace('_', '__').replace('::', '_1_1') + '.xml'
    path = Path(xml_dir) / filename
    compound = parse_compound(path, kind='namespace', name=namespace)
    root = ET.parse(path).getroot()
    members = {m.get('id'): m for m in root.findall('.//memberdef') if m.get('id')}
    functions = [f for f in compound.functions if f.qualified_name == qualified_symbol]
    if not functions:
        raise ValueError(f'no declaration of {qualified_symbol}')
    mapping = dict(xrefs)
    today = datetime.date.fromisoformat(date).strftime('%B %d, %Y')
    records = []
    for fn in functions:
        member = members.get(fn.refid)
        if member is None:
            raise ValueError(f'missing source declaration for {fn.refid!r}')
        for block in (member.find('briefdescription'), member.find('detaileddescription')):
            if block is not None:
                unsupported = next((n.tag for n in block.iter()
                                    if n.tag in {'bold', 'emphasis', 'computeroutput', 'type', 'argsstring'}), None)
                if unsupported:
                    raise ValueError(f'inline formatting <{unsupported}> is not preserved by the API model')
        # The shared model intentionally normalizes text. Ensure every Doxygen
        # documentation node was consumed by the strict parser.
        records.append(_collect_function(fn, mapping))
    md = _render_page(qualified_symbol, records, today)
    result = subprocess.run(['pandoc', '-s', '-f', 'markdown', '-t', 'man',
                             '-V', f'footer=mbedded-cbor {version}',
                             '-V', 'header=mbedded-cbor'], input=md, text=True,
                            capture_output=True, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xml_dir', type=Path)
    parser.add_argument('qualified_symbol')
    parser.add_argument('output', type=Path)
    parser.add_argument('--version', default='unknown')
    parser.add_argument('--date', default=datetime.datetime.now(datetime.timezone.utc).date().isoformat())
    parser.add_argument('--xref', action='append', default=[], metavar='TARGET=MANPAGE',
                        help='explicit mapping for a Doxygen refid or See also link target')
    args = parser.parse_args()
    xrefs = {}
    for entry in args.xref:
        key, sep, value = entry.partition('=')
        if not sep or not key or not value or key in xrefs:
            parser.error(f'invalid or duplicate --xref mapping: {entry!r}')
        xrefs[key] = value
    output = render(args.xml_dir, args.qualified_symbol, version=args.version,
                    date=args.date, xrefs=xrefs.items())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
