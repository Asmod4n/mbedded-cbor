#!/usr/bin/env python3
import argparse
import datetime
import html
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


def text(node):
    return ' '.join(''.join(node.itertext()).split()) if node is not None else ''


def listing(node):
    def collect(element):
        result = element.text or ''
        for child in element:
            result += ' ' if child.tag == 'sp' else collect(child)
            result += child.tail or ''
        return result
    return '\n'.join(collect(line) for line in node.findall('codeline'))


def output_page(source, target, related, version, today):
    root = ET.parse(source).getroot().find('compounddef')
    supported = {'briefdescription', 'detaileddescription', 'para', 'simplesect',
                 'title', 'codeline', 'computeroutput', 'highlight',
                 'parameterdescription', 'parameteritem', 'parameterlist',
                 'parametername', 'parameternamelist', 'programlisting', 'ref', 'sp', 'verbatim'}
    for section in root.findall('.//briefdescription') + root.findall('.//detaileddescription'):
        for node in section.iter():
            if node.tag not in supported:
                raise ValueError('unsupported documentation element: ' + node.tag)
    name = root.findtext('compoundname')
    members = root.findall('./sectiondef/memberdef[@kind="function"][@prot="public"]')
    brief = root.find('briefdescription')
    brief_paras = brief.findall('para')
    intro = text(brief_paras[0]) if brief_paras else ''
    desc = root.find('detaileddescription')
    description = [text(p) for p in brief_paras[1:] if text(p)]
    example = []
    for para in desc.findall('para'):
        if not len(para):
            if text(para):
                description.append(text(para))
            continue
        if all(child.tag in ('ref', 'computeroutput') for child in para):
            description.append(text(para))
            continue
        if (para.text or '').strip():
            raise ValueError('unsupported mixed class paragraph')
        for child in para:
            if child.tag == 'programlisting':
                example.append(listing(child))
            elif child.tag == 'simplesect':
                if child.get('kind') != 'par' or text(child.find('title')) not in ('Call sequence', 'Interactive example', 'See also'):
                    raise ValueError('unsupported class section')
                for program in child.iter('programlisting'):
                    example.append(listing(program))
            else:
                raise ValueError('unsupported class element: ' + child.tag)
            if (child.tail or '').strip():
                raise ValueError('unsupported class paragraph tail')
    if not description:
        description.append(intro)
    kind = 'struct sequence::iterator' if name.endswith('::iterator') else 'class sequence'
    synopsis = ['#include <cbor/sequence.hpp>', '', 'namespace cbor {', kind + ' {', 'public:']
    for nested in root.findall('innerclass'):
        synopsis.append('    struct ' + text(nested).split('::')[-1] + ';')
    for alias in root.findall('./sectiondef/memberdef[@kind="typedef"][@prot="public"]'):
        synopsis.append('    using ' + alias.findtext('name') + ' = ' + text(alias.find('type')) + ';')
    results, notes, details = [], [], []
    for m in members:
        result_type = text(m.find('type'))
        decl = ('    ' + ('explicit ' if m.get('explicit') == 'yes' else '') + (result_type + ' ' if result_type else '') + m.findtext('name')
                + text(m.find('argsstring')) + ';')
        template = m.find('templateparamlist')
        if template is not None:
            params = [text(p.find('type')) + ' ' + p.findtext('declname', '') for p in template.findall('param')]
            synopsis.append('    template <' + ', '.join(params) + '>')
        synopsis.append(decl)
        args = text(m.find('argsstring'))
        label = name + '::' + m.findtext('name') + args
        d = m.find('detaileddescription')
        paras = d.findall('para')
        own = []
        for para in paras:
            prefix = (para.text or '').strip()
            if prefix:
                own.append(prefix)
            for child in para:
                if child.tag == 'parameterlist':
                    for item in child.findall('parameteritem'):
                        own.append(text(item.find('parameternamelist')) + ': ' + text(item.find('parameterdescription')))
                elif child.tag == 'simplesect':
                    kind = child.get('kind')
                    if len(child.findall('para')) != 1:
                        raise ValueError('expected exactly one section paragraph')
                    if kind == 'return':
                        results.append((label, text(child.find('para'))))
                    elif kind == 'par' and text(child.find('title')) == 'Complexity':
                        notes.append((label, text(child.find('para'))))
                    elif kind in ('pre', 'exception', 'note'):
                        own.append(text(child))
                    else:
                        raise ValueError('unsupported section: ' + str(kind))
                else:
                    raise ValueError('unsupported member element: ' + child.tag)
        if own:
            details.append((label, ' '.join(own)))
    synopsis.extend(['};', '}'])
    def section(title, content):
        return '\n# ' + title + '\n\n' + content + '\n'
    def pairs(items):
        if any('`' in k for k, _ in items):
            raise ValueError('backtick in signature')
        return '\n\n'.join('`' + k + '`  \n' + html.escape(v) for k, v in items)
    date = datetime.date.fromisoformat(today).strftime('%B %d, %Y')
    md = '% ' + name + '(3)\n%\n% ' + date + '\n\n'
    md += section('NAME', html.escape(name) + ' - ' + html.escape(intro))
    md += section('SYNOPSIS', '```cpp\n' + '\n'.join(synopsis) + '\n```')
    md += section('DESCRIPTION', '\n\n'.join(html.escape(p) for p in description) + ('\n\n' + pairs(details) if details else ''))
    if results:
        md += section('RETURN VALUE', pairs(results))
    if name == 'cbor::sequence':
        errors = [sentence for p in description for sentence in re.split(r'(?<=\.) +', p) if 'Invalid ownership' in sentence or 'size error' in sentence]
        if errors:
            md += section('ERRORS', html.escape(' '.join(errors)))
    if notes:
        md += section('NOTES', pairs(notes))
    if example:
        if any('```' in e for e in example):
            raise ValueError('code fence in example')
        md += section('EXAMPLES', '\n\n'.join('```cpp\n' + e + '\n```' for e in example))
    links = [related + '(3)']
    for node in root.findall('./detaileddescription/para/simplesect'):
        if text(node.find('title')) != 'See also':
            continue
        value = node.find('./para/verbatim')
        if value is None:
            raise ValueError('See also has no verbatim content')
        targets = re.findall(r'\[[^\]]+\]\(([^)]+)\)', value.text or '')
        mapping = {'item_size.md': 'cbor::item_size(3)', '../lazy/index.md': 'cbor::lazy(3)',
                   '../../advanced/basics.md': 'advanced/basics.md, section 8'}
        for target_name in targets:
            if target_name not in mapping:
                raise ValueError('unsupported cross-reference: ' + target_name)
            links.append(mapping[target_name])
    md += section('SEE ALSO', ', '.join(links))
    result = subprocess.run(['pandoc', '-s', '-f', 'markdown', '-t', 'man', '-V', 'footer=mbedded-cbor ' + version, '-V', 'header=mbedded-cbor'], input=md, text=True, capture_output=True, check=True)
    target.write_text(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xml', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--version', default='sequence-pilot')
    parser.add_argument('--date', default=datetime.datetime.now(datetime.timezone.utc).date().isoformat())
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    pages = [('classcbor_1_1sequence.xml', 'cbor::sequence.3', 'cbor::sequence::iterator'),
             ('structcbor_1_1sequence_1_1iterator.xml', 'cbor::sequence::iterator.3', 'cbor::sequence')]
    with tempfile.TemporaryDirectory(dir=args.output) as tmp:
        for xml, name, other in pages:
            output_page(args.xml / xml, Path(tmp) / name, other, args.version, args.date)
        for _, name, _ in pages:
            (Path(tmp) / name).replace(args.output / name)


if __name__ == '__main__':
    main()
