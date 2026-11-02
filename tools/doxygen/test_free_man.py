"""Focused tests for classic free-function man page grouping."""
import importlib.util
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
SPEC = importlib.util.spec_from_file_location('free_man', HERE / 'free-man.py')
free_man = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(free_man)


class FreeManTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.xml_dir = Path(self.temp.name)
        shutil.copyfile(HERE / 'testdata/item_size_namespace.xml',
                        self.xml_dir / 'namespacecbor.xml')

    def tearDown(self):
        self.temp.cleanup()

    def xrefs(self):
        return {
            'sequence.md': 'cbor::sequence(3)',
            '../errors/limits.md': 'cbor::error(3)',
            '../../advanced/basics.md': 'advanced/basics.md, section 5',
        }

    def test_item_size_xml_keeps_manual_sections_and_skips_interactive_examples(self):
        rendered = free_man.render(self.xml_dir, 'cbor::item_size', version='9.8',
                                   date='2026-11-02', xrefs=self.xrefs().items())
        for heading in ('NAME', 'SYNOPSIS', 'DESCRIPTION', 'PARAMETERS',
                        'RETURN VALUE', 'ERRORS', 'NOTES', 'EXAMPLES', 'SEE ALSO'):
            self.assertEqual(rendered.count(f'.SH {heading}'), 1, heading)
        self.assertIn('.TH "cbor::item_size" "3" "November 02, 2026" "mbedded-cbor 9.8"', rendered)
        self.assertIn('too_little_data', rendered)
        self.assertIn('container_elements_exceeded', rendered)
        self.assertIn('cbor::sequence(3)', rendered)
        self.assertIn('cbor::error(3)', rendered)
        self.assertIn('advanced/basics.md, section 5', rendered)
        self.assertEqual(rendered.count('cbor::sequence(3)'), 1)
        self.assertEqual(rendered.count('cbor::error(3)'), 1)
        self.assertEqual(rendered.count('section 5'), 1)
        self.assertNotIn('sequence limits basics section 5', rendered)
        self.assertIn('\\f[CR]too_little_data\\f[R]', rendered)
        synopsis = rendered.split('.SH SYNOPSIS', 1)[1].split('.SH DESCRIPTION', 1)[0]
        synopsis = re.sub(r'\\f\[[^]]+\]', '', synopsis)
        self.assertIn('#include <cbor/cbor.hpp>', synopsis)
        self.assertIn('namespace cbor {', synopsis)
        self.assertIn('std::expected< std::size_t, error > item_size(std::string_view const encoded);', synopsis)
        self.assertNotIn('cbor::item_size(', synopsis)
        self.assertNotIn('Interactive example', rendered)
        self.assertNotIn('call: cbor::item_size', rendered)

    def test_malformed_see_also_link_fails_closed(self):
        path = self.xml_dir / 'namespacecbor.xml'
        tree = ET.parse(path)
        block = next(s for s in tree.findall('.//simplesect')
                     if s.findtext('title') == 'See also')
        block.find('.//verbatim').text = '* [sequence](sequence.md\n*'
        tree.write(path, encoding='utf-8', xml_declaration=True)
        with self.assertRaisesRegex(ValueError, 'malformed See also link'):
            free_man.render(self.xml_dir, 'cbor::item_size', version='9.8',
                            date='2026-11-02', xrefs=self.xrefs().items())

    def test_external_reference_keeps_label_and_url(self):
        path = self.xml_dir / 'namespacecbor.xml'
        tree = ET.parse(path)
        block = next(s for s in tree.findall('.//simplesect')
                     if s.findtext('title') == 'See also')
        block.find('.//verbatim').text = '* [RFC 8949](https://example.org/rfc)\n*'
        tree.write(path, encoding='utf-8', xml_declaration=True)
        rendered = free_man.render(self.xml_dir, 'cbor::item_size', version='9.8',
                                   date='2026-11-02', xrefs=self.xrefs().items())
        self.assertIn('RFC 8949', rendered)
        self.assertIn('https://example.org/rfc', rendered)

    def test_error_description_keeps_cpp_qualified_name(self):
        path = self.xml_dir / 'namespacecbor.xml'
        tree = ET.parse(path)
        block = next(s for s in tree.findall('.//simplesect')
                     if s.findtext('title') == 'Limits and errors')
        block.find('para').text = 'nesting_depth_exceeded: exceeds limits::nesting_depth. syntax_error: invalid input.'
        tree.write(path, encoding='utf-8', xml_declaration=True)
        rendered = free_man.render(self.xml_dir, 'cbor::item_size', version='9.8',
                                   date='2026-11-02', xrefs=self.xrefs().items())
        self.assertIn('limits::nesting_depth', rendered)
        self.assertNotIn('\\f[CR]limits\\f[R]', rendered)

    def test_overloads_share_one_page_header_and_keep_both_signatures(self):
        path = self.xml_dir / 'namespacecbor.xml'
        tree = ET.parse(path)
        section = tree.find('.//sectiondef')
        original = next(m for m in section.findall('memberdef')
                        if m.findtext('qualifiedname') == 'cbor::item_size')
        overload = ET.fromstring(ET.tostring(original))
        overload.set('id', 'item_size_second_overload')
        overload.find('argsstring').text = '(std::span<std::byte const> encoded)'
        overload.find('param/type').text = 'std::span<std::byte const>'
        overload.find('param/declname').text = 'encoded'
        overload.find('definition').text = 'std::expected< std::size_t, error > cbor::item_size'
        overload.find('.//parameterdescription/para').text = 'Second overload parameter details. '
        overload.find('.//simplesect[@kind="return"]/para').text = 'Second overload return details. '
        overload.find('.//simplesect[title="Limits and errors"]/para').text = 'second_error: Second overload error details. '
        section.append(overload)
        tree.write(path, encoding='utf-8', xml_declaration=True)

        rendered = free_man.render(self.xml_dir, 'cbor::item_size', version='9.8',
                                   date='2026-11-02', xrefs=self.xrefs().items())
        for heading in ('NAME', 'SYNOPSIS', 'DESCRIPTION', 'PARAMETERS',
                        'RETURN VALUE', 'ERRORS', 'NOTES', 'EXAMPLES', 'SEE ALSO'):
            self.assertEqual(rendered.count(f'.SH {heading}'), 1, heading)
        self.assertEqual(rendered.count('.TH "cbor::item_size"'), 1)
        self.assertIn('std::string_view const encoded', rendered)
        self.assertIn('std::span<std::byte const> encoded', rendered)
        self.assertIn('Overload 1', rendered)
        self.assertIn('Overload 2', rendered)
        self.assertIn('Second overload parameter', rendered)
        self.assertIn('details.', rendered)
        self.assertIn('Second overload return details', rendered)
        self.assertIn('second_error', rendered)
        self.assertIn('Second overload error', rendered)
        param_section = rendered.index('.SH PARAMETERS')
        return_section = rendered.index('.SH RETURN VALUE')
        error_section = rendered.index('.SH ERRORS')
        self.assertLess(rendered.index('Overload 2', param_section),
                        rendered.index('Second overload parameter', param_section))
        self.assertLess(rendered.index('Overload 2', return_section),
                        rendered.index('Second overload return details', return_section))
        self.assertLess(rendered.index('Overload 2', error_section),
                        rendered.index('Second overload error', error_section))

    def test_nested_namespace_synopsis_has_nested_context(self):
        source = self.xml_dir / 'namespacecbor.xml'
        tree = ET.parse(source)
        root = tree.getroot()
        root.find('.//compoundname').text = 'outer::inner'
        member = next(m for m in root.findall('.//memberdef')
                      if m.findtext('qualifiedname') == 'cbor::item_size')
        member.find('qualifiedname').text = 'outer::inner::item_size'
        member.find('definition').text = 'std::expected< std::size_t, error > outer::inner::item_size'
        (self.xml_dir / 'namespaceouter_1_1inner.xml').write_bytes(
            ET.tostring(root, encoding='utf-8', xml_declaration=True))
        rendered = free_man.render(self.xml_dir, 'outer::inner::item_size', version='9.8',
                                   date='2026-11-02', xrefs=self.xrefs().items())
        synopsis = rendered.split('.SH SYNOPSIS', 1)[1].split('.SH DESCRIPTION', 1)[0]
        synopsis = re.sub(r'\\f\[[^]]+\]', '', synopsis)
        self.assertIn('namespace outer {', synopsis)
        self.assertIn('namespace inner {', synopsis)
        self.assertIn('std::expected< std::size_t, error > item_size(', synopsis)
        self.assertNotIn('outer::inner::item_size(', synopsis)


if __name__ == '__main__':
    unittest.main()
