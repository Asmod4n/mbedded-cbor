# CBOR::XS: the canonical dump of one top-level item, the same text as dump.hpp. CBOR::XS gives a tag that it
# does not know as CBOR::XS::Tagged (RFC 8949 3.4). Perl keeps text and bytes apart by the UTF-8 flag.
# A map key that is not text becomes a Perl string; the warning for it and for deep recursion is off.
use strict;
use warnings;
no warnings qw(recursion uninitialized);
use CBOR::XS;
use B;

my $cbor = CBOR::XS->new->allow_sharing;

sub dump_value {
    my ($v) = @_;
    return "s(22)" unless defined $v;
    my $r = ref $v;
    if ($r eq "ARRAY") { return "[" . join(",", map { dump_value($_) } @$v) . "]" }
    if ($r eq "HASH") {
        return "{" . join(",", sort map { "t'" . unpack("H*", do { my $k = $_; utf8::encode($k); $k }) . "':" . dump_value($v->{$_}) } keys %$v) . "}";
    }
    if ($r eq "CBOR::XS::Tagged") { return $v->[0] . "(" . dump_value($v->[1]) . ")" }
    if ($r eq "Types::Serialiser::Boolean" || $r eq "JSON::PP::Boolean") { return $v ? "s(21)" : "s(20)" }
    if ($r eq "CBOR::XS::Undefined" || (Types::Serialiser->can("is_undef") && $r =~ /Undef/)) { return "s(23)" }
    if ($r eq "CBOR::XS::Simple") { return "s(" . ($$v + 0) . ")" }
    if ($r eq "Math::BigInt") { return $v->bstr }
    return "<$r>" if $r;
    my $flags = B::svref_2object(\$v)->FLAGS;
    if ($flags & B::SVp_POK) {
        my $s = $v;
        if (utf8::is_utf8($s)) { utf8::encode($s); return "t'" . unpack("H*", $s) . "'" }
        return "h'" . unpack("H*", $s) . "'";
    }
    if ($flags & B::SVp_IOK) { return "$v" }
    if ($v != $v) { return "nan" }
    return "f" . unpack("H*", pack("d>", $v));
}

while (my $line = <STDIN>) {
    chomp $line;
    my @f = split /\t/, $line, -1;
    next if @f != 4 || $f[2] =~ /^!/;
    my @out = ($f[0]);
    for my $h ($f[2], $f[3]) {
        if ($h eq "-") { push @out, "-"; next }
        my $v = eval { $cbor->decode(pack("H*", $h)) };
        if ($@) { my $e = $@; $e =~ s/[\t\n]/ /g; push @out, "!$e" } else { push @out, dump_value($v) }
    }
    print join("\t", @out), "\n";
}
