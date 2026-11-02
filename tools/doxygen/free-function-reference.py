#!/usr/bin/env python3
"""Render a documented free function from Doxygen XML."""
import argparse
import re
from pathlib import Path
import xml.etree.ElementTree as ET

from api_model import parse_compound


def _inline_markdown(records):
    """Render model inline records without dropping link destinations."""
    chunks = []
    for record in records:
        if record.kind in ('text', 'paragraph_break'):
            chunks.append(record.text)
        elif record.kind == 'ref':
            raise ValueError('internal Doxygen references need a verified website anchor mapping')
        elif record.kind == 'ulink':
            if not record.target:
                raise ValueError('external link has no destination')
            chunks.append(f'[{record.text}]({record.target})')
        else:
            raise ValueError('unsupported inline content: ' + record.kind)
    return ''.join(chunks).strip()


def _section_text(section):
    return _inline_markdown(section.inline) if section.inline else section.text


def _validate_interactive_example(text, title):
    """Apply the website's message/bytes + call/result grammar strictly."""
    inputs = []
    calls = 0
    result_pending = False
    for line in text.splitlines():
        if not line:
            continue
        match = re.fullmatch(r'(\w+):\s?(.*)', line)
        if match is None:
            raise ValueError(f'{title}: invalid example line: {line}')
        key, _value = match.groups()
        if key in ('message', 'bytes'):
            inputs.append(key)
        elif key == 'call':
            if result_pending:
                raise ValueError(f'{title}: call has no result')
            calls += 1
            result_pending = True
        elif key == 'result':
            if not result_pending:
                raise ValueError(f'{title}: result does not follow a call')
            result_pending = False
        else:
            raise ValueError(f'{title}: unsupported example line: {line}')
    if len(inputs) != 1:
        raise ValueError(f'{title}: expected exactly one message or bytes line')
    if calls == 0:
        raise ValueError(f'{title}: expected at least one call')
    if result_pending:
        raise ValueError(f'{title}: call has no result')


def _reject_unmodeled_formatting(source_member):
    if source_member is None:
        return
    containers = [source_member.find('briefdescription'),
                  source_member.find('detaileddescription')]
    unsupported = {'bold', 'emphasis', 'computeroutput', 'type', 'argsstring'}
    for container in containers:
        if container is not None:
            found = next((node.tag for node in container.iter() if node.tag in unsupported), None)
            if found:
                raise ValueError(f'inline formatting <{found}> is not preserved by the API model')


def render(xml_dir, qualified_symbol):
    """Return a Markdown reference for one fully-qualified free function."""
    parts = qualified_symbol.split('::')
    if len(parts) < 2 or any(not part for part in parts):
        raise ValueError('expected a fully qualified function symbol')
    namespace = '::'.join(parts[:-1])
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*(?:::[A-Za-z_][A-Za-z_0-9]*)*', namespace):
        raise ValueError('namespace contains characters without a verified Doxygen filename encoding')
    filename_namespace = namespace.replace('_', '__').replace('::', '_1_1')
    xml_path = Path(xml_dir) / ('namespace' + filename_namespace + '.xml')
    compound = parse_compound(xml_path,
                              kind='namespace', name=namespace)
    xml_root = ET.parse(xml_path).getroot()
    source_members = {member.get('id'): member for member in xml_root.findall('.//memberdef')
                      if member.get('id')}
    functions = [fn for fn in compound.functions if fn.qualified_name == qualified_symbol]
    if not functions:
        raise ValueError(f'no declaration of {qualified_symbol}')
    rendered = []
    for function in functions:
        source_member = source_members.get(function.refid)
        if source_member is None:
            raise ValueError('cannot inspect declaration markup: missing Doxygen member id')
        rendered.append(_render_function(function, qualified_symbol, source_member))
    if len(rendered) == 1:
        return f'# {qualified_symbol}\n\n' + rendered[0]
    return f'# {qualified_symbol}\n\n' + ''.join(
        f'## Overload {index}\n\n' + body
        for index, body in enumerate(rendered, 1))


def _render_function(function, qualified_symbol, source_member=None):
    _reject_unmodeled_formatting(source_member)
    named = {}
    descriptions, documented_parameters, returns, exceptions = [], {}, [], []
    interactive = {}
    for section in function.sections:
        if section.kind == 'description':
            descriptions.append(_section_text(section))
        elif section.kind == 'par':
            title = section.title
            match = re.fullmatch(r'Interactive example(?: ([1-9][0-9]*))?', title)
            if match:
                number = int(match.group(1) or 1)
                if number in interactive:
                    raise ValueError(f'duplicate reference section: {title}')
                interactive[number] = section
            elif title in named:
                raise ValueError('duplicate reference section: ' + title)
            elif title not in {'Limits and errors', 'Complexity', 'Example', 'See also'}:
                raise ValueError('unknown reference section: ' + title)
            else:
                named[title] = section
        elif section.kind == 'param':
            for parameter in section.parameters:
                if parameter in documented_parameters:
                    raise ValueError('duplicate parameter documentation: ' + parameter)
                documented_parameters[parameter] = _section_text(section)
        elif section.kind == 'return':
            returns.append(_section_text(section))
        elif section.kind == 'throws':
            exceptions.extend((name, _section_text(section)) for name in section.parameters)
        else:
            raise ValueError('unsupported reference section: ' + section.kind)

    if not interactive:
        raise ValueError('at least one Interactive example section is required')
    if sorted(interactive) != list(range(1, len(interactive) + 1)):
        raise ValueError('Interactive example sections must be numbered consecutively from 1')
    for title, section in named.items():
        if title in ('Example', 'See also'):
            if section.verbatim is None:
                raise ValueError('missing verbatim content: ' + title)
    for number, section in interactive.items():
        if section.verbatim is None:
            raise ValueError(f'missing verbatim content: Interactive example {number}')
        _validate_interactive_example(section.verbatim, f'Interactive example {number}')
    parameter_names = [p.name or f'arg{index}' for index, p in enumerate(function.parameters, 1)]
    unknown_params = set(documented_parameters) - set(parameter_names)
    if unknown_params:
        raise ValueError('documentation for unknown parameters: ' + ', '.join(sorted(unknown_params)))
    if len(returns) > 1:
        raise ValueError('duplicate return documentation')

    prefix, found, suffix = function.signature.partition(function.qualified_name)
    if not found:
        raise ValueError('unexpected function signature')
    if function.template_parameters:
        template_args = []
        for parameter in function.template_parameters:
            declaration = ' '.join(x for x in (parameter.type, parameter.name) if x)
            if parameter.default:
                declaration += ' = ' + parameter.default
            template_args.append(declaration)
        prefix = 'template <' + ', '.join(template_args) + '>\n' + prefix
    if function.requires:
        suffix += ' requires ' + function.requires
    output = ''
    output += '```cpp\n' + prefix + function.name + suffix + ';\n```\n\n'
    output += ('```call-seq\n' + qualified_symbol + '(' + ', '.join(parameter_names) + ') → '
               + function.return_type + '\n```\n\n')
    for number in sorted(interactive):
        output += '```example\n' + interactive[number].verbatim.strip() + '\n```\n\n'
    brief = _inline_markdown(function.brief_inline) if function.brief_inline else function.brief
    description_text = '\n\n'.join(_section_text(section)
                                    for section in function.sections if section.kind == 'description')
    if brief or description_text:
        output += brief + (' ' if brief and description_text else '') + description_text + '\n\n'
    if documented_parameters:
        output += '## Parameters\n\n' + '\n'.join(
            f'- `{name}`: {documented_parameters[name]}' for name in parameter_names
            if name in documented_parameters) + '\n\n'
    if returns:
        output += '## Return value\n\n' + returns[0] + '\n\n'
    if exceptions:
        output += '## Exceptions\n\n' + '\n'.join(
            f'- `{name}`: {message}' for name, message in exceptions) + '\n\n'
    if 'Limits and errors' in named:
        error_section = named['Limits and errors']
        error_text = _section_text(error_section)
        errors = re.findall(r'(?<![\w`])([A-Za-z_][A-Za-z_0-9]*):\s*(.*?)(?=(?<![\w`])[A-Za-z_][A-Za-z_0-9]*:\s*|$)', error_text)
        if not errors:
            raise ValueError('Limits and errors section contains no named errors')
        output += '## Errors\n\n' + '\n'.join(
            '- `' + name + '`: ' + message.strip() for name, message in errors) + '\n\n'
    if 'Complexity' in named:
        cost_section = named['Complexity']
        output += '## Cost\n\n' + _section_text(cost_section) + '\n\n'
    if 'Example' in named:
        output += '## Example\n\n```cpp\n' + named['Example'].verbatim.strip() + '\n```\n\n'
    if 'See also' in named:
        output += '## See also\n\n' + named['See also'].verbatim.strip() + '\n'
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('xml_dir', type=Path)
    parser.add_argument('qualified_symbol')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = render(args.xml_dir, args.qualified_symbol)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)


if __name__ == '__main__':
    main()
