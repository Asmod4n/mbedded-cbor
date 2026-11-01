#!/usr/bin/env python3
import argparse
import datetime
import html
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from doc_grammar import text, verbatim


def listing(node):
    def collect(element):
        result = element.text or ''
        for child in element:
            result += ' ' if child.tag == 'sp' else collect(child)
            result += child.tail or ''
        return result
    return '\n'.join(collect(line) for line in node.findall('codeline'))


def output_item_size(source, target, version, today):
    root = ET.parse(source).getroot().find('compounddef')
    member = next((m for m in root.findall('.//memberdef[@kind="function"]')
                   if m.findtext('qualifiedname') == 'cbor::item_size'), None)
    if member is None:
        raise ValueError('missing cbor::item_size documentation')
    brief = text(member.find('briefdescription'))
    params = member.findall('./param')
    args = []
    for p in params:
        arg = text(p.find('type'))
        if p.findtext('declname'):
            arg += ' ' + p.findtext('declname')
        args.append(arg)
    signature = text(member.find('type')) + ' item_size(' + ', '.join(args) + ');'
    description, errors, notes, examples, see_also = [], [], [], [], []
    required = {'RETURN VALUE': False, 'ERRORS': False, 'NOTES': False,
                'EXAMPLES': False, 'SEE ALSO': False}
    detail = member.find('detaileddescription')
    returns = ''
    for para in detail.findall('para'):
        # Only paragraph text before nested Doxygen blocks is DESCRIPTION.
        direct = ' '.join((para.text or '').split())
        if direct:
            description.append(direct)
        for simple in para.findall('simplesect'):
            title = text(simple.find('title'))
            body = simple.find('para')
            value = text(body)
            if simple.get('kind') == 'return':
                returns = value
                required['RETURN VALUE'] = bool(returns)
            elif title == 'Limits and errors':
                errors.extend(re.findall(r'(too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):\s*(.*?)(?=(?:too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):|$)', value))
                required['ERRORS'] = len(errors) == 5
            elif title == 'Complexity':
                notes.append(value)
                required['NOTES'] = bool(value)
            elif title == 'Example':
                code = verbatim(simple, preserve_blank_lines=True)
                if code:
                    examples.append(code)
                    required['EXAMPLES'] = True
            elif title == 'See also':
                for path in re.findall(r'\[[^\]]+\]\(([^)]+)\)', verbatim(simple)):
                    mapping = {'sequence.md': 'cbor::sequence(3)', '../errors/limits.md': 'cbor::error(3)',
                               '../../advanced/basics.md': 'advanced/basics.md, section 5'}
                    if path not in mapping:
                        raise ValueError('unsupported cross-reference: ' + path)
                    see_also.append(mapping[path])
                required['SEE ALSO'] = bool(see_also)
    missing = [name for name, found in required.items() if not found]
    if not description:
        missing.insert(0, 'DESCRIPTION')
    if missing:
        raise ValueError('missing required item_size documentation sections: ' + ', '.join(missing))
    date = datetime.date.fromisoformat(today).strftime('%B %d, %Y')
    md = '% cbor::item_size(3)\n%\n% ' + date + '\n\n'
    def section(title, content):
        return '\n# ' + title + '\n\n' + content + '\n'
    md += section('NAME', html.escape('cbor::item_size - ' + brief))
    md += section('SYNOPSIS', '```cpp\n#include <cbor/item_size.hpp>\n\nnamespace cbor {\n' + signature + '\n}\n```')
    md += section('DESCRIPTION', '\n\n'.join(html.escape(p) for p in description))
    if returns:
        md += section('RETURN VALUE', html.escape(returns))
    if errors:
        md += section('ERRORS', '\n'.join('- `' + name + '`: ' + html.escape(message.strip()) for name, message in errors))
    if notes:
        md += section('NOTES', '\n\n'.join(html.escape(n) for n in notes))
    if examples:
        md += section('EXAMPLES', '\n\n'.join('```cpp\n' + e + '\n```' for e in examples))
    links = ['mbedded-cbor(3)', 'cbor::lazy(3)', 'cbor::at_path(3)', 'cbor::item(3)',
             'cbor::structs(3)', 'cbor::schema(3)', 'cbor::ref(3)'] + see_also + [
             'cbor::sequence(3)', 'cbor::sequence_reader(3)', 'cbor::diagnostic_notation(3)', 'cbor::error(3)']
    md += section('SEE ALSO', ', '.join(dict.fromkeys(links)))
    result = subprocess.run(['pandoc', '-s', '-f', 'markdown', '-t', 'man', '-V', 'footer=mbedded-cbor ' + version, '-V', 'header=mbedded-cbor'], input=md, text=True, capture_output=True, check=True)
    target.write_text(result.stdout)


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
        targets = re.findall(r'\[[^\]]+\]\(([^)]+)\)', verbatim(node))
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
             ('structcbor_1_1sequence_1_1iterator.xml', 'cbor::sequence::iterator.3', 'cbor::sequence'),
             ('namespacecbor.xml', 'cbor::item_size.3', None)]
    with tempfile.TemporaryDirectory(dir=args.output) as tmp:
        for xml, name, other in pages:
            if xml == 'namespacecbor.xml':
                output_item_size(args.xml / xml, Path(tmp) / name, args.version, args.date)
            else:
                output_page(args.xml / xml, Path(tmp) / name, other, args.version, args.date)
        for _, name, _ in pages:
            (Path(tmp) / name).replace(args.output / name)


if __name__ == '__main__':
    main()
