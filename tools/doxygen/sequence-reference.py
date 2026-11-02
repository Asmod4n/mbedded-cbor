#!/usr/bin/env python3
"""Render the cbor::sequence reference from the shared Doxygen model."""
import argparse
from pathlib import Path

from api_model import parse_compound


def _reference_sections(compound):
    metadata = {}
    details = []
    for section in compound.sections:
        if section.kind == 'description':
            details.append(' '.join(section.text.split()))
        elif section.kind == 'par':
            if section.title in metadata:
                raise ValueError('duplicate reference section: ' + section.title)
            if section.title not in ('Call sequence', 'Interactive example', 'See also'):
                raise ValueError('unsupported reference section: ' + section.title)
            if section.verbatim is None:
                raise ValueError('reference section has no verbatim content')
            metadata[section.title] = section.verbatim
        else:
            raise ValueError('unsupported class description section: ' + section.kind)
    if set(metadata) != {'Call sequence', 'Interactive example', 'See also'}:
        raise ValueError('incomplete reference metadata')
    example = metadata['Interactive example'].splitlines()
    if not example or not example[0].startswith(('bytes: ', 'message: ')):
        raise ValueError('example has no input')
    if not any(line.startswith('call: ') for line in example):
        raise ValueError('example has no call')
    if not compound.brief:
        raise ValueError('class has no description')
    return metadata, [compound.brief] + details


def _function_comment(function):
    name = function.qualified_name + function.signature.partition(function.qualified_name)[2]
    out = []
    for section in function.sections:
        if section.kind == 'description':
            out.append(section.text)
        elif section.kind == 'param':
            out.append(section.title + ': ' + section.text)
        elif section.kind == 'return':
            out.append('Returns: ' + section.text)
        elif section.kind == 'par' and section.title == 'Complexity':
            out.append('Complexity: ' + section.text)
        else:
            raise ValueError('unsupported function section: ' + name)
    if not out:
        raise ValueError('missing function description: ' + name)
    return '**`' + name + '`**\n\n' + ' '.join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xml', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    sequence = parse_compound(args.xml / 'classcbor_1_1sequence.xml',
                              kind='class', name='cbor::sequence')
    iterator = parse_compound(args.xml / 'structcbor_1_1sequence_1_1iterator.xml',
                              kind='struct', name='cbor::sequence::iterator')
    metadata, details = _reference_sections(sequence)
    iterator_text = [c.brief for c in (iterator,) if c.brief]
    iterator_text.extend(s.text for s in iterator.sections if s.kind == 'description' and s.text)
    if not iterator_text:
        raise ValueError('missing iterator description')
    comments = [_function_comment(fn) for fn in sequence.functions]
    comments.append('**`cbor::sequence::iterator`**\n\n' + ' '.join(iterator_text))
    comments.extend(_function_comment(fn) for fn in iterator.functions)
    if len(comments) != 10:
        raise ValueError('unexpected number of sequence operations')
    output = '# cbor::sequence\n\n'
    output += '```call-seq\n' + metadata['Call sequence'] + '\n```\n\n'
    output += '```example\n' + metadata['Interactive example'] + '\n```\n\n'
    output += '\n\n'.join(details) + '\n\n' + '\n\n'.join(comments)
    output += '\n\n## See also\n\n' + metadata['See also'] + '\n'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)


if __name__ == '__main__':
    main()
