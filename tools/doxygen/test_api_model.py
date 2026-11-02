"""Focused checks for semantic parsing of Doxygen compound XML."""
import unittest

from api_model import DoxygenError, parse_compound


class ApiModelTest(unittest.TestCase):
    def test_interleaved_inline_markup_and_verbatim_indent(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef><memberdef kind="function" id="f" prot="public"><type>void</type>
            <name>f</name><qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring>
            <briefdescription><para>Brief <emphasis>link</emphasis> tail.</para></briefdescription>
            <detaileddescription><para>Before <emphasis>linked words</emphasis>, after.
              <parameterlist kind="param"><parameteritem><parameternamelist>
                <parametername>x</parametername></parameternamelist><parameterdescription>
                <para>Value.</para></parameterdescription></parameteritem></parameterlist>
              More <emphasis>inline</emphasis> tail.</para>
              <simplesect kind="par"><title>Example</title><para><verbatim>*  first line
*    indented line
</verbatim></para></simplesect></detaileddescription>
          </memberdef></sectiondef></compounddef></doxygen>'''
        fn = parse_compound(xml).functions[0]
        self.assertEqual(fn.brief, 'Brief link tail.')
        prose = [section.text for section in fn.sections if section.kind == 'description']
        self.assertEqual(prose, ['Before linked words, after.', 'More inline tail.'])
        self.assertEqual(fn.sections[-1].verbatim, ' first line\n   indented line')

    def test_description_sections_follow_source_order(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>f</name>
          <qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring><detaileddescription>
          <para>Before.<parameterlist kind="param"><parameteritem><parameternamelist>
          <parametername>x</parametername></parameternamelist><parameterdescription><para>Value.</para>
          </parameterdescription></parameteritem></parameterlist>Between.<simplesect kind="return">
          <para>Result.</para></simplesect>After.</para></detaileddescription>
          </memberdef></sectiondef></compounddef></doxygen>'''
        sections = parse_compound(xml).functions[0].sections
        self.assertEqual([(section.kind, section.title or section.text) for section in sections],
                         [('description', 'Before.'), ('param', 'x'),
                          ('description', 'Between.'), ('return', 'Result.'),
                          ('description', 'After.')])

    def test_class_documentation_and_template_metadata(self):
        xml = '''<doxygen><compounddef kind="class"><compoundname>demo::Range</compoundname>
          <briefdescription><para>A range.</para></briefdescription>
          <detaileddescription><para>Class detail.</para><simplesect kind="par">
            <title>Complexity</title><para>Constant setup.</para></simplesect></detaileddescription>
          <sectiondef><memberdef kind="function" id="templated" prot="public"><templateparamlist>
            <param><type>typename</type><declname>T</declname></param></templateparamlist>
            <type>bool</type><name>contains</name><qualifiedname>demo::Range::contains</qualifiedname>
            <requiresclause>std::equality_comparable&lt;T&gt;</requiresclause><argsstring>(T const&amp; value) const</argsstring>
            <briefdescription><para>Check a value.</para></briefdescription></memberdef>
          </sectiondef></compounddef></doxygen>'''
        model = parse_compound(xml)
        self.assertEqual(model.brief, 'A range.')
        self.assertEqual(model.sections[0].text, 'Class detail.')
        self.assertEqual(model.sections[1].title, 'Complexity')
        fn = model.functions[0]
        self.assertEqual((fn.template_parameters[0].type, fn.template_parameters[0].name),
                         ('typename', 'T'))
        self.assertEqual(fn.requires, 'std::equality_comparable<T>')

    def test_namespace_overloads_and_detail_filter(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef>
            <memberdef kind="function" id="one" prot="public"><type>int</type><name>read</name>
              <qualifiedname>demo::read</qualifiedname><argsstring>(int)</argsstring></memberdef>
            <memberdef kind="function" id="two" prot="public"><type>int</type><name>read</name>
              <qualifiedname>demo::read</qualifiedname><argsstring>(char const*)</argsstring></memberdef>
            <memberdef kind="function" id="hidden" prot="public"><type>void</type><name>helper</name>
              <qualifiedname>cbor::detail::helper</qualifiedname><argsstring>()</argsstring></memberdef>
          </sectiondef></compounddef></doxygen>'''
        model = parse_compound(xml)
        self.assertEqual([fn.refid for fn in model.functions], ['one', 'two'])
        self.assertEqual([fn.signature for fn in model.functions],
                         ['int demo::read(int)', 'int demo::read(char const*)'])

    def test_unknown_markup_is_rejected(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <briefdescription><para>Unrecognized <mystery>content</mystery>.</para></briefdescription>
          </compounddef></doxygen>'''
        with self.assertRaisesRegex(DoxygenError, 'mystery'):
            parse_compound(xml)

    def test_private_functions_are_excluded(self):
        xml = '''<doxygen><compounddef kind="class"><compoundname>demo::C</compoundname>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>shown</name>
            <qualifiedname>demo::C::shown</qualifiedname><argsstring>()</argsstring></memberdef>
            <memberdef kind="function" prot="private"><type>void</type><name>hidden</name>
            <qualifiedname>demo::C::hidden</qualifiedname><argsstring>()</argsstring></memberdef>
            <memberdef kind="function" prot="protected"><type>void</type><name>also_hidden</name>
            <qualifiedname>demo::C::also_hidden</qualifiedname><argsstring>()</argsstring></memberdef>
          </sectiondef></compounddef></doxygen>'''
        self.assertEqual([f.name for f in parse_compound(xml).functions], ['shown'])

    def test_links_keep_targets_and_description_paragraphs_are_separate(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <detaileddescription><para>First.</para><para>Second.</para></detaileddescription>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>f</name>
          <qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring></memberdef>
          </sectiondef></compounddef></doxygen>'''
        model = parse_compound(xml)
        self.assertEqual([s.text for s in model.sections], ['First.', 'Second.'])
        linked = xml.replace('<para>First.</para>',
                             '<para>See <ref refid="target">target</ref> and '
                             '<ulink url="https://example.invalid">web</ulink>.</para>')
        model = parse_compound(linked)
        first = model.sections[0]
        self.assertEqual(first.text, 'See target and web.')
        self.assertEqual([(span.kind, span.target, span.target_kind) for span in first.inline if span.kind != 'text'],
                         [('ref', 'target', ''), ('ulink', 'https://example.invalid', '')])

    def test_link_kindref_and_paragraph_breaks(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <briefdescription><para>One.</para><para>Read <ref refid="r" kindref="member">this</ref>.</para></briefdescription>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>f</name>
          <qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring></memberdef></sectiondef>
          </compounddef></doxygen>'''
        compound = parse_compound(xml)
        self.assertEqual(compound.brief_inline[1].kind, 'paragraph_break')
        link = next(span for span in compound.brief_inline if span.kind == 'ref')
        self.assertEqual((link.target, link.target_kind), ('r', 'member'))

    def test_programlisting_spaces_and_explicit_visibility(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>f</name>
          <qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring><detaileddescription>
          <simplesect kind="par"><title>Example</title><para><verbatim>* x
</verbatim><programlisting><codeline><highlight class="normal">a</highlight><sp/><sp/><highlight class="normal">b</highlight></codeline>
          <codeline><highlight class="normal">c</highlight></codeline></programlisting></para></simplesect>
          </detaileddescription></memberdef></sectiondef></compounddef></doxygen>'''
        section = parse_compound(xml).functions[0].sections[0]
        self.assertEqual(section.code, 'a  b\nc')
        missing = xml.replace(' prot="public"', '')
        with self.assertRaisesRegex(DoxygenError, 'visibility'):
            parse_compound(missing)

    def test_declaration_type_and_signature_links_are_preserved(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef><memberdef kind="function" prot="public"><type><ref refid="ret" kindref="compound">result</ref></type>
          <name>convert</name><qualifiedname>demo::convert</qualifiedname>
          <argsstring>(<ref refid="arg" kindref="compound">iterator</ref> const&amp; value)</argsstring>
          <param><type><ref refid="arg" kindref="compound">iterator</ref> const&amp;</type><declname>value</declname></param>
          </memberdef></sectiondef></compounddef></doxygen>'''
        fn = parse_compound(xml).functions[0]
        ret = next(item for item in fn.return_type_inline if item.kind == 'ref')
        arg = next(item for item in fn.args_inline if item.kind == 'ref')
        param = next(item for item in fn.parameters[0].type_inline if item.kind == 'ref')
        self.assertEqual((ret.target, ret.target_kind), ('ret', 'compound'))
        self.assertEqual((arg.target, arg.target_kind), ('arg', 'compound'))
        self.assertEqual((param.target, param.target_kind), ('arg', 'compound'))

    def test_detail_compounds_are_rejected(self):
        xml = '''<doxygen><compounddef kind="namespace"><compoundname>cbor::detail::hidden</compoundname>
          </compounddef></doxygen>'''
        with self.assertRaisesRegex(DoxygenError, 'detail'):
            parse_compound(xml)

    def test_structural_errors_and_missing_signature_are_rejected(self):
        base = '''<doxygen><compounddef kind="namespace"><compoundname>demo</compoundname>
          <sectiondef><memberdef kind="function" prot="public"><type>void</type><name>f</name>
          <qualifiedname>demo::f</qualifiedname><argsstring>()</argsstring>
          <detaileddescription><para>text{extra}</para></detaileddescription>
          </memberdef></sectiondef></compounddef></doxygen>'''
        with self.assertRaisesRegex(DoxygenError, 'unsupported paragraph'):
            parse_compound(base.format(extra='<unknown/>'))
        malformed_params = base.format(extra='<parameterlist kind="param"><unexpected/></parameterlist>')
        with self.assertRaisesRegex(DoxygenError, 'parameterlist'):
            parse_compound(malformed_params)
        multi_para = base.format(extra='<simplesect kind="return"><para>a</para><para>b</para></simplesect>')
        with self.assertRaises(DoxygenError):
            parse_compound(multi_para)
        missing = base.replace('<argsstring>()</argsstring>', '')
        with self.assertRaisesRegex(DoxygenError, 'signature'):
            parse_compound(missing)


if __name__ == '__main__':
    unittest.main()
