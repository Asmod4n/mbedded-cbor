@0xc7e2d4b3e5f6a891;
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("cprecords");
struct User { name@0: Text; followers@1: UInt64; }
struct Record { id@0: Int64; text@1: Text; user@2: User; ratio@3: Float64; tags@4: List(Text); neg@5: Int64; }
struct Records { records@0: List(Record); }
