"""Self-contained checks for the generic website free-function renderer."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location(
    'free_function_reference', TOOLS / 'free-function-reference.py')
renderer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(renderer)


def function(name='f', *, refid='f', result='void', args='()', params='',
             template='', requires='', brief='', detail=''):
    return f'''<memberdef kind="function" id="{refid}" prot="public">
      {template}<type>{result}</type><name>{name}</name>
      <qualifiedname>demo::{name}</qualifiedname>{requires}<argsstring>{args}</argsstring>
      {params}<briefdescription>{brief}</briefdescription>
      <detaileddescription>{detail}</detaileddescription>
    </memberdef>'''


def interactive(title='Interactive example', lines='bytes: 00\ncall: demo::f()\nresult: ok'):
    return f'''<simplesect kind="par"><title>{title}</title><para><verbatim>
      {''.join('* ' + line + chr(10) for line in lines.splitlines())}</verbatim></para></simplesect>'''


def xml(functions, namespace='demo'):
    return ('<doxygen><compounddef kind="namespace"><compoundname>' + namespace
            + '</compoundname><sectiondef>' + ''.join(functions)
            + '</sectiondef></compounddef></doxygen>')


class FreeFunctionReferenceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write_namespace(self, text, namespace='demo'):
        encoded = namespace.replace('_', '__').replace('::', '_1_1')
        (self.directory / f'namespace{encoded}.xml').write_text(text)

    def render(self, symbol='demo::f'):
        return renderer.render(self.directory, symbol)

    def test_brief_only_and_unnamed_parameter(self):
        fn = function(args='(int)', params='<param><type>int</type></param>',
                      brief='<para>A brief.</para>',
                      detail='<para>' + interactive() + '</para>')
        self.write_namespace(xml([fn]))
        output = self.render()
        self.assertIn('A brief.', output)
        self.assertIn('demo::f(arg1)', output)
        self.assertIn('bytes: 00', output)

    def test_multiple_overloads_template_requires_and_exception(self):
        first = function(refid='one', result='T', args='(T const&amp;)',
                         params='<param><type>T const&amp;</type></param>',
                         template='<templateparamlist><param><type>typename</type><declname>T</declname><defval>int</defval></param></templateparamlist>',
                         requires='<requiresclause>std::copyable&lt;T&gt;</requiresclause>',
                         brief='<para>Use <ulink url="https://example.invalid">this</ulink>.</para>',
                         detail='<para>' + interactive() + '</para>')
        second = function(refid='two', args='()', detail='<para>' + interactive() +
                          '<parameterlist kind="exception"><parameteritem><parameternamelist>'
                          '<parametername>failure</parametername></parameternamelist>'
                          '<parameterdescription><para>Failure text.</para></parameterdescription>'
                          '</parameteritem></parameterlist></para>')
        self.write_namespace(xml([first, second]))
        output = self.render()
        self.assertIn('## Overload 1', output)
        self.assertIn('## Overload 2', output)
        self.assertIn('template <typename T = int>', output)
        self.assertIn('requires std::copyable<T>', output)
        self.assertIn('[this](https://example.invalid)', output)
        self.assertIn('## Exceptions', output)
        self.assertIn('`failure`: Failure text.', output)

    def test_requires_at_least_one_example_starting_at_one(self):
        fn = function(detail='<para><simplesect kind="par"><title>Complexity</title>'
                      '<para>O(1).</para></simplesect></para>')
        self.write_namespace(xml([fn]))
        with self.assertRaisesRegex(ValueError, 'at least one'):
            self.render()
        fn = function(detail='<para>' + interactive('Interactive example 2') + '</para>')
        self.write_namespace(xml([fn]))
        with self.assertRaisesRegex(ValueError, 'consecutively'):
            self.render()

    def test_interactive_example_pairs_calls_and_results_and_rejects_extra_lines(self):
        for lines in (
            'bytes: 00\ncall: f()\nresult: one\ncall: g()\nresult: two',
            'message: hello\ncall: f()\nresult: ok',
        ):
            with self.subTest(lines=lines):
                self.write_namespace(xml([function(detail='<para>' +
                    interactive(lines=lines) + '</para>')]))
                self.assertIn('```example', self.render())
        invalid = (
            'bytes: 00\ncall: f()\nresult: ok\nextra: line',
            'bytes: 00\ncall: f()\ncall: g()\nresult: ok',
            'bytes: 00\nresult: orphan\ncall: f()\nresult: ok',
            'bytes: 00\nmessage: duplicate\ncall: f()\nresult: ok',
            'bytes: 00\ncall: f()',
        )
        for lines in invalid:
            with self.subTest(lines=lines):
                self.write_namespace(xml([function(detail='<para>' +
                    interactive(lines=lines) + '</para>')]))
                with self.assertRaises(ValueError):
                    self.render()

    def test_formatted_prose_and_internal_refs_fail_closed(self):
        for markup, message in (('<bold>words</bold>', 'inline formatting'),
                                ('<ref refid="other">words</ref>', 'verified website anchor')):
            with self.subTest(markup=markup):
                fn = function(brief=f'<para>Brief {markup}.</para>',
                              detail='<para>' + interactive() + '</para>')
                self.write_namespace(xml([fn]))
                with self.assertRaisesRegex(ValueError, message):
                    self.render()

    def test_static_cpp_example_remains_in_reference(self):
        example = ('<simplesect kind="par"><title>Example</title><para>'
                   '<verbatim>* int answer = 42;\n</verbatim></para></simplesect>')
        self.write_namespace(xml([function(detail='<para>' + interactive() + example + '</para>')]))
        self.assertIn('```cpp\nint answer = 42;\n```', self.render())

    def test_cpp_qualified_name_is_not_an_error_label(self):
        error_section = ('<simplesect kind="par"><title>Limits and errors</title>'
                         '<para>depth_error: exceeds limits::nesting_depth. '
                         'syntax_error: invalid input.</para></simplesect>')
        self.write_namespace(xml([function(detail='<para>' + interactive() + error_section + '</para>')]))
        output = self.render()
        self.assertIn('`depth_error`: exceeds limits::nesting_depth.', output)
        self.assertNotIn('`limits`:', output)

    def test_namespace_underscores_use_doxygen_filename_encoding(self):
        fn = function(name='f', refid='f', brief='<para>Brief.</para>',
                      detail='<para>' + interactive() + '</para>')
        self.write_namespace(xml([fn], namespace='foo_bar').replace('demo::f', 'foo_bar::f'),
                             namespace='foo_bar')
        self.assertIn('Brief.', self.render('foo_bar::f'))


if __name__ == '__main__':
    unittest.main()
