#!/usr/bin/env python3
import argparse
import re
import xml.etree.ElementTree as ET
from pathlib import Path


def text(element):
    return ' '.join(''.join(element.itertext()).split()) if element is not None else ''


def verbatim(section):
    node = section.find('./para/verbatim')
    if node is None:
        raise ValueError('reference section has no verbatim content')
    lines = (node.text or '').splitlines()
    result = []
    for line in lines:
        if not line.strip():
            continue
        if not re.match(r'^\s*\*(?: |$)', line):
            raise ValueError('unexpected verbatim format')
        result.append(re.sub(r'^\s*\* ?', '', line))
    return '\n'.join(result).strip()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('xml', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    root = ET.parse(args.xml / 'classcbor_1_1sequence.xml').getroot().find('compounddef')
    if root.findtext('compoundname') != 'cbor::sequence':
        raise ValueError('unexpected class')
    metadata = {}
    for section in root.findall('./detaileddescription/para/simplesect'):
        title = text(section.find('title'))
        if title in metadata:
            raise ValueError('duplicate reference section: ' + title)
        if title not in ('Call sequence', 'Interactive example', 'See also'):
            raise ValueError('unsupported reference section: ' + title)
        metadata[title] = verbatim(section)
    if set(metadata) != {'Call sequence', 'Interactive example', 'See also'}:
        raise ValueError('incomplete reference metadata')
    example = metadata['Interactive example'].splitlines()
    if not example or not example[0].startswith(('bytes: ', 'message: ')):
        raise ValueError('example has no input')
    if not any(line.startswith('call: ') for line in example):
        raise ValueError('example has no call')
    details = [text(root.find('./briefdescription/para'))]
    for para in root.findall('./detaileddescription/para'):
        if not len(para):
            if text(para):
                details.append(text(para))
        elif all(child.tag in ('ref', 'computeroutput') for child in para):
            details.append(text(para))
        elif (para.text or '').strip() or any(child.tag != 'simplesect' or (child.tail or '').strip() for child in para):
            raise ValueError('unsupported mixed class description')
    if not details:
        raise ValueError('class has no description')
    comments = []
    for filename in ('classcbor_1_1sequence.xml', 'structcbor_1_1sequence_1_1iterator.xml'):
        cls = ET.parse(args.xml / filename).getroot().find('compounddef')
        if filename.startswith('struct'):
            iterator_text = [text(p) for block in ('briefdescription', 'detaileddescription')
                             for p in cls.findall('./' + block + '/para') if text(p)]
            if not iterator_text:
                raise ValueError('missing iterator description')
            comments.append('**`cbor::sequence::iterator`**\n\n' + ' '.join(iterator_text))
        for member in cls.findall('./sectiondef/memberdef[@kind="function"][@prot="public"]'):
            name = member.findtext('qualifiedname') + member.findtext('argsstring')
            paragraphs = member.findall('detaileddescription/para')
            if len(paragraphs) != 1:
                raise ValueError('expected exactly one function paragraph: ' + name)
            para = paragraphs[0]
            if para is None:
                raise ValueError('missing function description: ' + name)
            out = [para.text.strip()] if para.text and para.text.strip() else []
            for child in para:
                if child.tag == 'parameterlist':
                    for item in child.findall('parameteritem'):
                        out.append(text(item.find('parameternamelist')) + ': ' + text(item.find('parameterdescription')))
                elif child.tag == 'simplesect':
                    if child.get('kind') == 'return':
                        out.append('Returns: ' + text(child.find('para')))
                    elif child.get('kind') == 'par' and text(child.find('title')) == 'Complexity':
                        out.append('Complexity: ' + text(child.find('para')))
                    else:
                        raise ValueError('unsupported function section: ' + name)
                else:
                    raise ValueError('unsupported function element: ' + child.tag)
                if (child.tail or '').strip():
                    raise ValueError('unsupported function paragraph tail: ' + name)
            comments.append('**`' + name + '`**\n\n' + ' '.join(out))
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
