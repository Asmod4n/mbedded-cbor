# Sequence documentation pilot

This pilot generates a reference from the API comments in
`include/cbor/sequence.hpp`. It does not replace the existing website,
Markdown reference or installed manual pages.

Install Doxygen from the distribution. From the repository root, run:

```sh
CBOR_DOXYGEN_OUTPUT=/absolute/path/out doxygen tools/doxygen/sequence.Doxyfile
```

Use a fresh output directory outside the source tree. The command writes
HTML to `out/html`, manual pages to `out/man/man3`, and XML to `out/xml`.
Warnings fail the command. Open `out/html/index.html` in a browser.
Render each generated class and iterator manual page with `mandoc`.

The configuration excludes the iterator's two storage fields from the
reference. They remain unchanged in the C++ class. This pilot does not
establish a complete inventory of the library's public declarations.

Doxygen's default class manual format groups constructors and methods.
It must be inspected before choosing it as a replacement for the current
manual format. The owner decides whether this sample should be extended.

## Pilot result

Checked with distribution packages: Doxygen 1.17.0, mandoc and Clang
23.1.3. Doxygen generated HTML, XML and two manual pages without warnings.
All nine callable overloads have a Complexity section. The generated
include path is `cbor/sequence.hpp`.

The header example compiled in C++23 with warnings as errors and ran
successfully. The preprocessed header matched the unmodified header
exactly. Four existing sequence reference test cases passed, with
13 assertions. Boost.PFR was included as a system header, as in CMake.

The raw Doxygen manual output is not ready to replace the installed
manual pages. `mandoc -Tlint -Wwarning` reports redundant paragraph and
spacing macros. The pages render at 80 columns, but use Doxygen's
class-oriented headings instead of the existing DESCRIPTION, RETURN VALUE
and ERRORS organization. The default HTML navigation also renders the
comparison operator's equals signs as escaped text in its sidebars.

The sample shows that header comments can supply the reference content.
It does not justify replacing the current rendering pipeline. If the
owner adopts header comments, a later step can adapt the current renderer
to consume Doxygen XML and retain the established manual structure and
interactive website. Do not remove Markdown until that path reproduces
the examples and reference information. No wider migration is included.

## Compact revision

The header now shares lifetime and allocation rules at class level.
Each operation keeps its own complexity statement. The example is in
`sequence-example.cpp` and Doxygen includes it in the generated pages.
The header has 150 lines, compared with 218 in the first pilot and 101
before API comments. The example compiled and ran; preprocessing again
matched the original header. Four sequence reference tests passed with
13 assertions. Doxygen generated the pages without warnings.

The earlier manual-format and HTML-navigation limitations still apply.
This revision does not change the installed documentation pipeline.
