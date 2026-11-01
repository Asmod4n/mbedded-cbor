#!/usr/bin/env python3
"""Render the item_size website reference from the shared Doxygen model."""
import argparse
import re
from pathlib import Path

from api_model import parse_compound


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xml', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    compound = parse_compound(args.xml / 'namespacecbor.xml', kind='namespace', name='cbor')
    functions = [f for f in compound.functions if f.qualified_name == 'cbor::item_size']
    if len(functions) != 1:
        raise ValueError('expected exactly one cbor::item_size declaration')
    function = functions[0]
    named = {}
    descriptions = []
    for section in function.sections:
        if section.kind == 'description':
            descriptions.append(' '.join(section.text.split()))
        elif section.kind == 'par':
            if section.title in named:
                raise ValueError('duplicate reference section: ' + section.title)
            named[section.title] = section
        elif section.kind not in ('param', 'return'):
            raise ValueError('unsupported reference section: ' + section.kind)
    required = {'Limits and errors', 'Complexity', 'Interactive example',
                'Interactive example 2', 'Example', 'See also'}
    if set(named) != required or not function.brief or not descriptions:
        raise ValueError('incomplete item_size documentation')
    limits = re.findall(r'(too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):\s*(.*?)(?=(?:too_little_data|syntax_error|indefinite_length|string_length_exceeded|container_elements_exceeded):|$)', named['Limits and errors'].text)
    if len(limits) != 5:
        raise ValueError('incomplete limits and errors')
    for title in ('Interactive example', 'Interactive example 2', 'Example', 'See also'):
        if named[title].verbatim is None:
            raise ValueError('missing verbatim content: ' + title)
    for title in ('Interactive example', 'Interactive example 2'):
        if not named[title].verbatim.startswith('bytes: '):
            raise ValueError('interactive example has no input')
    if len(function.parameters) != 1 or not function.parameters[0].name:
        raise ValueError('unexpected item_size parameter list')
    prefix, found, suffix = function.signature.partition(function.qualified_name)
    if not found:
        raise ValueError('unexpected signature')
    output = '# cbor::item_size\n\n'
    output += '```cpp\n' + prefix + function.name + suffix + ';\n```\n\n'
    output += ('```call-seq\n' + function.qualified_name + '(' + function.parameters[0].name + ') → '
               + function.return_type + '\n```\n\n')
    for title in ('Interactive example', 'Interactive example 2'):
        output += '```example\n' + named[title].verbatim.strip() + '\n```\n\n'
    output += function.brief + ' ' + '\n\n'.join(descriptions) + '\n\n'
    output += '## Errors\n\n' + '\n'.join('- `' + name + '`: ' + message.strip() for name, message in limits) + '\n\n'
    output += '## Cost\n\n' + named['Complexity'].text + '\n\n'
    output += '## Example\n\n```cpp\n' + named['Example'].verbatim.strip() + '\n```\n\n'
    output += '## See also\n\n' + named['See also'].verbatim.strip() + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
