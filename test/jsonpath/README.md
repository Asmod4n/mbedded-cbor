JSONPath Compliance Test Suite
==============================

cts.json is the file of the same name from
https://github.com/jsonpath-standard/jsonpath-compliance-test-suite at
commit 9d1a415a53f5dfb291bc874823892e49174e38eb. LICENSE is the license
of that repository.

cts.cbor holds the same cases in CBOR. cts_to_cbor.py writes it:

    python3 -I cts_to_cbor.py cts.json cts.cbor

A valid case is [name, selector, document, results]; results is the
array of every nodelist that the suite allows. A case with an invalid
selector is [name, selector].
