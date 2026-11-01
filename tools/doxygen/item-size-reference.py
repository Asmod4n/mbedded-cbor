#!/usr/bin/env python3
"""Render the item_size reference page from Doxygen XML."""
import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path
from doc_grammar import par_sections, text


def direct_text(element):
    """Read only paragraph text, excluding nested Doxygen sections."""
    if element is None:
        return ''
    parts = [element.text or '']
    parts.extend(child.tail or '' for child in element)
    return ' '.join(' '.join(parts).split())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xml', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = ET.parse(args.xml / 'namespacecbor.xml').getroot().find('compounddef')
    if root is None or root.findtext('compoundname') != 'cbor':
        raise ValueError('unexpected namespace XML')
    functions = [m for m in root.findall('./sectiondef/memberdef[@kind="function"]')
                 if m.findtext('qualifiedname') == 'cbor::item_size']
    if len(functions) != 1:
        raise ValueError('missing cbor::item_size documentation')
    function = functions[0]
    description = function.find('detaileddescription')
    sections = {}
    examples = {}
    for section in par_sections(description.findall('./para/simplesect')):
        if section.kind != 'par':
            continue
        title = section.title
        if title.startswith('Interactive example'):
            key = title
            if key in examples:
                raise ValueError('duplicate example: ' + key)
            if section.verbatim is None:
                raise ValueError('reference section has no verbatim content')
            examples[key] = section.verbatim
        elif title in ('Limits and errors', 'Example', 'See also'):
            if title in sections:
                raise ValueError('duplicate reference section: ' + title)
            if title in ('Example', 'See also'):
                if section.verbatim is None:
                    raise ValueError('reference section has no verbatim content')
                sections[title] = section.verbatim
            else:
                sections[title] = section.paragraph
    required = {'Limits and errors', 'Example', 'See also'}
    if set(sections) != required or set(examples) != {'Interactive example', 'Interactive example 2'}:
        raise ValueError('incomplete item_size reference metadata')
    if not examples['Interactive example'].startswith('bytes: ') or not examples['Interactive example 2'].startswith('bytes: '):
        raise ValueError('interactive example has no input')
    brief = text(function.find('./briefdescription/para'))
    if not brief:
        raise ValueError('missing item_size brief description')
    details = direct_text(function.find('./detaileddescription/para'))
    if not details:
        raise ValueError('missing item_size description')
    limits = re.findall(r'(too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):\s*(.*?)(?=(?:too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):|$)', sections['Limits and errors'])
    if len(limits) != 5:
        raise ValueError('incomplete limits and errors')
    complexity = next((text(s.find('para')) for s in description.findall('./para/simplesect')
                       if s.get('kind') == 'par' and text(s.find('title')) == 'Complexity'), None)
    if not complexity:
        raise ValueError('missing complexity')
    errors = '\n'.join('- `' + name + '`: ' + description.strip() for name, description in limits)
    output = '# cbor::item_size\n\n'
    output += '```cpp\n' + text(function.find('type')) + ' ' + function.findtext('name') + function.findtext('argsstring') + ';\n```\n\n'
    params = function.find('param')
    param_name = params.findtext('declname') if params is not None else None
    if not param_name:
        raise ValueError('missing function parameter name')
    output += ('```call-seq\n' + function.findtext('qualifiedname') + '(' + param_name + ') → '
               + text(function.find('type')) + '\n```\n\n')
    for key in ('Interactive example', 'Interactive example 2'):
        output += '```example\n' + examples[key] + '\n```\n\n'
    output += brief + ' ' + details + '\n\n'
    output += '## Errors\n\n' + errors + '\n\n'
    output += '## Cost\n\n' + complexity + '\n\n'
    output += '## Example\n\n```cpp\n' + sections['Example'] + '\n```\n\n'
    output += '## See also\n\n' + sections['See also'] + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
