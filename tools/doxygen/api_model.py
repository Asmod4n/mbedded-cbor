"""Strict semantic reader for the supported subset of Doxygen compound XML.

The public entry point is :func:`parse_compound` (also accepts a path). It
returns immutable dataclasses and fails closed on documentation markup outside
the explicitly supported subset.
"""
from dataclasses import dataclass
from pathlib import Path
import xml.etree.ElementTree as ET


class DoxygenError(ValueError):
    """Malformed XML or unsupported Doxygen structure/markup."""


@dataclass(frozen=True)
class Parameter:
    type: str
    declaration: str
    name: str
    type_inline: tuple = ()


@dataclass(frozen=True)
class TemplateParameter:
    type: str
    name: str
    default: str = ''


@dataclass(frozen=True)
class DocSection:
    kind: str
    title: str
    text: str = ''
    parameters: tuple = ()
    verbatim: str | None = None
    inline: tuple = ()
    code: str = ''
    code_markup: tuple = ()


@dataclass(frozen=True)
class InlineContent:
    kind: str
    text: str
    target: str = ''
    target_kind: str = ''


@dataclass(frozen=True)
class Function:
    name: str
    qualified_name: str
    return_type: str
    signature: str
    parameters: tuple
    brief: str
    sections: tuple
    refid: str
    template_parameters: tuple[TemplateParameter, ...] = ()
    requires: str = ''
    brief_inline: tuple = ()
    return_type_inline: tuple = ()
    args_inline: tuple = ()


@dataclass(frozen=True)
class Compound:
    kind: str
    name: str
    functions: tuple
    brief: str = ''
    sections: tuple = ()
    brief_inline: tuple = ()


def _plain(node):
    if node is None:
        return ''
    _validate_markup(node, {'para', 'bold', 'emphasis', 'computeroutput', 'ref', 'ulink', 'sp', 'ndash', 'mdash', 'linebreak'})
    return ' '.join(''.join(node.itertext()).split())


def _validate_markup(node, allowed):
    for child in node.iter():
        if child is node:
            continue
        if child.tag not in allowed:
            raise DoxygenError(f'unsupported documentation markup <{child.tag}>')


def _verbatim(node):
    verb_nodes = node.findall('verbatim')
    if len(verb_nodes) != 1:
        raise DoxygenError('verbatim section must contain exactly one verbatim node')
    lines, result = (verb_nodes[0].text or '').splitlines(), []
    for line in lines:
        if not line.strip():
            result.append('')
            continue
        import re
        match = re.match(r'^\s*\*(?: ?)(.*)$', line)
        if match is None:
            raise DoxygenError('unexpected Doxygen verbatim line format')
        result.append(match.group(1))
    return '\n'.join(result).strip('\n')


def _inline_text(node):
    """Collect prose from inline markup, preserving text and tails in order."""
    allowed = {'para', 'bold', 'emphasis', 'computeroutput', 'ref', 'ulink',
               'sp', 'ndash', 'mdash', 'linebreak', 'type', 'argsstring'}
    pieces = []
    if node.tag not in allowed:
        raise DoxygenError(f'unsupported paragraph markup <{node.tag}>')

    def visit(part):
        if part.tag not in allowed:
            raise DoxygenError(f'unsupported paragraph markup <{part.tag}>')
        if part.tag == 'sp':
            pieces.append(' ')
        elif part.tag == 'ndash':
            pieces.append('–')
        elif part.tag == 'mdash':
            pieces.append('—')
        elif part.tag == 'linebreak':
            pieces.append('\n')
        elif part.text:
            pieces.append(part.text)
        for nested in part:
            visit(nested)
            if nested.tail:
                pieces.append(nested.tail)

    if node.text:
        pieces.append(node.text)
    for child in node:
        visit(child)
        if child.tail:
            pieces.append(child.tail)
    return ''.join(pieces)


def _inline_content(node):
    """Return ordered text/link records plus their backward-compatible text."""
    allowed = {'para', 'bold', 'emphasis', 'computeroutput', 'ref', 'ulink',
               'sp', 'ndash', 'mdash', 'linebreak', 'type', 'argsstring'}
    result = []

    def add_text(value):
        if value:
            result.append(InlineContent('text', value))

    def visit(part):
        if part.tag not in allowed:
            raise DoxygenError(f'unsupported paragraph markup <{part.tag}>')
        if part.tag in ('ref', 'ulink'):
            target = part.get('refid' if part.tag == 'ref' else 'url', '')
            if not target:
                raise DoxygenError(f'<{part.tag}> has no target')
            target_kind = part.get('kindref', '') if part.tag == 'ref' else ''
            result.append(InlineContent(part.tag, ''.join(part.itertext()), target, target_kind))
            return
        if part.tag == 'sp':
            add_text(' ')
        elif part.tag == 'ndash':
            add_text('–')
        elif part.tag == 'mdash':
            add_text('—')
        elif part.tag == 'linebreak':
            add_text('\n')
        else:
            add_text(part.text or '')
            for child in part:
                visit(child)
                add_text(child.tail or '')

    visit(node)
    plain = ''.join(item.text for item in result)
    return tuple(result), plain


def _description(node):
    if node is None:
        return '', ()
    paragraphs, sections = [], []
    for child in node:
        if child.tag == 'para':
            inline_records = []
            if child.text:
                inline_records.append(InlineContent('text', child.text))
            for part in child:
                if part.tag == 'parameterlist':
                    if part.get('kind') not in ('param', 'exception'):
                        raise DoxygenError(f"unsupported parameter list kind {part.get('kind')!r}")
                    if part.attrib.keys() != {'kind'} or any(c.tag != 'parameteritem' for c in part):
                        raise DoxygenError('unexpected parameterlist attributes or children')
                    for item in part:
                        if any(c.tag not in ('parameternamelist', 'parameterdescription') for c in item) or len(item) != 2:
                            raise DoxygenError('unexpected parameteritem structure')
                        name_list = item.find('parameternamelist')
                        if name_list is None or any(c.tag != 'parametername' for c in name_list):
                            raise DoxygenError('unexpected parameter name list structure')
                        names = [n.text or '' for n in name_list]
                        if not names:
                            raise DoxygenError('parameter documentation has no parameter name')
                        desc, desc_inline = _paragraph_content(item.find('parameterdescription'))
                        section_kind = 'param' if part.get('kind') == 'param' else 'throws'
                        sections.append(DocSection(section_kind, ', '.join(names), desc,
                                                   tuple(names), inline=desc_inline))
                elif part.tag == 'simplesect':
                    sections.append(_simple_section(part))
                else:
                    inline_records.extend(_inline_content(part)[0])
                if part.tail:
                    inline_records.append(InlineContent('text', part.tail))
            inline = ''.join(record.text for record in inline_records).strip()
            if inline:
                sections.append(DocSection('description', '', inline, inline=tuple(inline_records)))
        elif child.tag == 'simplesect':
            sections.append(_simple_section(child))
        else:
            raise DoxygenError(f'unsupported description element <{child.tag}>')
    sections = tuple(sections)
    return '', sections


def _paragraph_text(node):
    if node is None:
        return ''
    paras = node.findall('para')
    if len(paras) != len(node):
        raise DoxygenError('unexpected parameter description structure')
    values = []
    for para in paras:
        if para.tag != 'para':
            raise DoxygenError(f'unexpected paragraph element <{para.tag}>')
        values.append(_inline_text(para).strip())
    return '\n\n'.join(filter(None, values))


def _paragraph_content(node):
    if node is None:
        return '', ()
    paras = node.findall('para')
    if len(paras) != len(node):
        raise DoxygenError('unexpected parameter description structure')
    contents = [_inline_content(para) for para in paras]
    nonempty = [(records, text.strip()) for records, text in contents if text.strip()]
    plain = '\n\n'.join(text for _, text in nonempty)
    joined = []
    for index, (records, _) in enumerate(nonempty):
        if index:
            joined.append(InlineContent('paragraph_break', '\n\n'))
        joined.extend(records)
    return plain, tuple(joined)


def _inline_paragraphs(node):
    if node is None:
        return ()
    paras = node.findall('para')
    if len(paras) != len(node):
        raise DoxygenError('unexpected paragraph description structure')
    result = []
    for para in paras:
        records, text = _inline_content(para)
        if not text.strip():
            continue
        if result:
            result.append(InlineContent('paragraph_break', '\n\n'))
        result.extend(records)
    return tuple(result)


def _render_code(node):
    """Render Doxygen listing whitespace, including explicit <sp/> nodes."""
    parts = []

    def walk(part):
        if part.tag == 'sp':
            parts.append(' ')
        elif part.text:
            parts.append(part.text)
        for child in part:
            walk(child)
            if child.tail:
                parts.append(child.tail)

    for index, line in enumerate(node.findall('codeline')):
        if index:
            parts.append('\n')
        walk(line)
    return ''.join(parts)


def _simple_section(node):
    kind = node.get('kind') or ''
    title = _plain(node.find('title'))
    paras = node.findall('para')
    allowed_children = {'title', 'para'}
    if any(c.tag not in allowed_children for c in node) or len(paras) != 1:
        raise DoxygenError(f'{kind!r} section has no paragraph')
    para = paras[0]
    verb = para.find('verbatim')
    if verb is not None:
        unknown = [c.tag for c in para if c.tag not in ('verbatim', 'programlisting')]
        if unknown:
            raise DoxygenError(f'unsupported verbatim section markup <{unknown[0]}>')
        code_nodes = para.findall('programlisting')
        for listing in code_nodes:
            if any(child.tag not in ('codeline', 'highlight', 'sp', 'ref') for child in listing.iter() if child is not listing):
                raise DoxygenError('unsupported programlisting markup')
        code = '\n'.join(_render_code(n) for n in code_nodes)
        code_markup = tuple(ET.tostring(n, encoding='unicode') for n in code_nodes)
        return DocSection(kind, title, verbatim=_verbatim(para), code=code,
                          code_markup=code_markup)
    inline, plain = _inline_content(para)
    return DocSection(kind, title, plain.strip(), inline=inline)


def _function(node):
    if node.get('kind') != 'function':
        return None
    name_node = node.find('name')
    qualified_node = node.find('qualifiedname')
    args_node = node.find('argsstring')
    if name_node is None or not _plain(name_node):
        raise DoxygenError('function has no name')
    if qualified_node is None or not _plain(qualified_node):
        raise DoxygenError(f"function {_plain(name_node)!r} has no qualified name")
    if args_node is None or not (args_node.text or '').strip():
        raise DoxygenError(f"function {_plain(name_node)!r} has no signature")
    name = _plain(name_node)
    qualified = _plain(qualified_node)
    if qualified.split('::')[:2] == ['cbor', 'detail'] or qualified.startswith('cbor::detail::'):
        return None
    params = []
    args = []
    for p in node.findall('param'):
        type_node = p.find('type')
        typ = _plain(type_node)
        type_inline = _inline_content(type_node)[0] if type_node is not None else ()
        decl = _plain(p.find('declname'))
        default = _plain(p.find('defval'))
        array = _plain(p.find('array'))
        arg = ' '.join(x for x in (typ, decl + array if decl else array) if x)
        if default:
            arg += ' = ' + default
        args.append(arg)
        params.append(Parameter(typ, arg, decl, type_inline))
    argstr = _plain(args_node)
    return_type_node = node.find('type')
    return_type = _plain(return_type_node)
    return_type_inline = _inline_content(return_type_node)[0] if return_type_node is not None else ()
    args_inline = _inline_content(args_node)[0]
    signature = f"{return_type} {qualified}{argstr or '(' + ', '.join(args) + ')'}".strip()
    brief_node = node.find('briefdescription')
    brief = _paragraph_text(brief_node)
    brief_inline = _inline_paragraphs(brief_node)
    detail, sections = _description(node.find('detaileddescription'))
    if detail:
        sections = (DocSection('description', '', detail),) + sections
    template_parameters = []
    for template in node.findall('templateparamlist/param'):
        typ = _plain(template.find('type'))
        decl = _plain(template.find('declname'))
        default = _plain(template.find('defval'))
        if not typ:
            raise DoxygenError('template parameter has no type')
        template_parameters.append(TemplateParameter(typ, decl, default))
    requires_node = node.find('requiresclause')
    requires = _plain(requires_node)
    return Function(name, qualified, return_type, signature,
                    tuple(params), brief, sections, node.get('id') or '',
                    tuple(template_parameters), requires, brief_inline,
                    return_type_inline, args_inline)


def parse_compound(source, *, kind=None, name=None):
    """Parse a Doxygen compound XML path, file object, bytes, or XML string.

    Optional *kind* and *name* select/validate the compound. Function
    overloads remain separate records in source order; cbor::detail is omitted.
    """
    try:
        if hasattr(source, 'read'):
            root = ET.parse(source).getroot()
        elif isinstance(source, (str, Path)) and not str(source).lstrip().startswith('<'):
            root = ET.parse(source).getroot()
        else:
            root = ET.fromstring(source)
    except (ET.ParseError, OSError, TypeError) as exc:
        raise DoxygenError(str(exc)) from exc
    compound = root.find('compounddef')
    if compound is None:
        raise DoxygenError('missing compounddef')
    actual_kind = compound.get('kind') or ''
    actual_name = _plain(compound.find('compoundname'))
    if actual_kind not in ('class', 'struct', 'namespace'):
        raise DoxygenError(f'unsupported compound kind {actual_kind!r}')
    if actual_name.split('::')[:2] == ['cbor', 'detail']:
        raise DoxygenError('cbor::detail compounds are excluded')
    if kind is not None and actual_kind != kind:
        raise DoxygenError(f'expected {kind!r} compound, got {actual_kind!r}')
    if name is not None and actual_name != name:
        raise DoxygenError(f'expected compound {name!r}, got {actual_name!r}')
    compound_brief = _paragraph_text(compound.find('briefdescription'))
    compound_brief_node = compound.find('briefdescription')
    compound_brief_inline = _inline_paragraphs(compound_brief_node)
    compound_detail, compound_sections = _description(compound.find('detaileddescription'))
    if compound_detail:
        compound_sections = (DocSection('description', '', compound_detail),) + compound_sections
    functions = []
    for section in compound.findall('sectiondef'):
        for member in section.findall('memberdef'):
            visibility = member.get('prot')
            if visibility is None:
                raise DoxygenError('member declaration has no visibility')
            if visibility != 'public':
                continue
            fn = _function(member)
            if fn is not None:
                functions.append(fn)
    return Compound(actual_kind, actual_name, tuple(functions), compound_brief, compound_sections,
                    compound_brief_inline)


__all__ = ['Compound', 'DocSection', 'DoxygenError', 'Function', 'InlineContent', 'Parameter', 'TemplateParameter', 'parse_compound']
