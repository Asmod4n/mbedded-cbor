# CBOR::XS: the canonical dump of one top-level item, the same text as dump.hpp. CBOR::XS gives a tag that it
# does not know as CBOR::XS::Tagged (RFC 8949 3.4). Perl keeps text and bytes apart by the UTF-8 flag.
# A map key that is not text becomes a Perl string; the warning for it and for deep recursion is off. Perl keeps a
# hash key as bytes where every character fits a byte, and CBOR::XS->new->encode writes such a key as a byte string;
# so the dump gives such a key as bytes, and every other key as text. In cycles-read every key is text, because
# every key of cycles.tsv is text and CBOR::XS keeps no difference between a text key and a byte key.
# A type of the data model of CBOR::XS is the tag and the content that its TO_CBOR method writes: Time::Piece the
# tag 1 and its epoch, URI the tag 32 and its text, Types::Serialiser::Error (the undefined of CBOR::XS) the simple
# value 23. Math::BigInt is its decimal value, as check.py reads a bignum. Math::BigFloat stays "<Math::BigFloat>",
# which check.py counts as not comparable. A float with an integral value from 0 to 2^32 - 1 is the integer that
# CBOR::XS->new->encode writes for it. A map key that is an array or a map is the text of a Perl reference, which
# holds an address, and stays "<reference key>".
# With no argument it prints "name TAB dump-of-written TAB dump-of-expected" for each line of cases.tsv. With
# "reencode" it writes the value of the input of each case again with CBOR::XS->new->encode and prints "name TAB
# input TAB written TAB dump-of-written", as dump.hpp does. With "cycles-write" it prints the cases of cycles.tsv
# that CBOR::XS->new->allow_sharing writes; the hash is written with text_keys, so its key is text as in the case of
# cbor2. With "cycles-read" it prints "name TAB graph" for each line of
# cycles.tsv, in the text of read.cpp.
use strict;
use warnings;
no warnings qw(recursion uninitialized);
use CBOR::XS;
use B;
use Scalar::Util qw(refaddr);

my $epoch_tag = 1;
my $uri_tag = 32;
my $largest_float_written_as_integer = 4294967295;
my $cbor = CBOR::XS->new->allow_sharing->allow_cycles;
my $writer = CBOR::XS->new;
my $keys_as_written = 1;

sub children {
    my ($v) = @_;
    my $r = ref $v;
    return @$v if $r eq "ARRAY";
    return map { ($_, $v->{$_}) } keys %$v if $r eq "HASH";
    return ($v->[1]) if $r eq "CBOR::XS::Tagged";
    return ();
}

sub targets {
    my ($root) = @_;
    my (%seen, %named);
    my @todo = ($root);
    while (@todo) {
        my $v = pop @todo;
        my $r = ref $v;
        next unless $r eq "ARRAY" || $r eq "HASH" || $r eq "CBOR::XS::Tagged";
        if ($seen{refaddr $v}++) { $named{refaddr $v} = 1; next }
        push @todo, children($v);
    }
    return \%named;
}

sub text_dump {
    my ($s) = @_;
    utf8::encode($s);
    return "t'" . unpack("H*", $s) . "'";
}

sub key_dump {
    my ($k) = @_;
    return "<reference key>" if $k =~ /^([\w:]+=)?(ARRAY|HASH)\(0x[0-9a-f]+\)$/;
    my $bytes = $k;
    return "h'" . unpack("H*", $bytes) . "'" if $keys_as_written && utf8::downgrade($bytes, 1);
    return text_dump($k);
}

sub dump_value {
    my ($v, $named, $numbered, $key) = @_;
    $named //= {};
    my $r = ref $v;
    if ($r && $named->{refaddr $v}) {
        return "*" if $key;
        return "*" . $numbered->{refaddr $v} if exists $numbered->{refaddr $v};
        my $n = scalar keys %$numbered;
        $numbered->{refaddr $v} = $n;
        return "&$n" . content($v, $named, $numbered, 0);
    }
    return content($v, $named, $numbered, $key);
}

sub content {
    my ($v, $named, $numbered, $key) = @_;
    return "s(22)" unless defined $v;
    my $r = ref $v;
    if ($r eq "ARRAY") { return "[" . join(",", map { dump_value($_, $named, $numbered, $key) } @$v) . "]" }
    if ($r eq "HASH") {
        my %pairs = map { key_dump($_) . ":" . dump_value($v->{$_}, $named, $numbered, 1) => $_ } keys %$v;
        return "{" . join(",", sort keys %pairs) . "}" if $key || !%$named;
        return "{" . join(",", map { key_dump($pairs{$_}) . ":" . dump_value($v->{$pairs{$_}}, $named, $numbered, 0) }
                                   sort keys %pairs) . "}";
    }
    if ($r eq "CBOR::XS::Tagged") { return $v->[0] . "(" . dump_value($v->[1], $named, $numbered, $key) . ")" }
    if ($r eq "Types::Serialiser::Boolean" || $r eq "JSON::PP::Boolean") { return $v ? "s(21)" : "s(20)" }
    if ($r eq "Types::Serialiser::Error") { return "s(23)" }
    if ($r eq "CBOR::XS::Simple") { return "s(" . ($$v + 0) . ")" }
    if ($r eq "Math::BigInt") { return $v->bstr }
    if ($r eq "Time::Piece") { return "$epoch_tag(" . content(0 + $v->epoch) . ")" }
    if ($r =~ /^URI(::|$)/) { my $s = $v->as_string; return "$uri_tag(" . text_dump($s) . ")" }
    return "<$r>" if $r;
    my $flags = B::svref_2object(\$v)->FLAGS;
    if ($flags & B::SVp_POK) {
        my $s = $v;
        if (utf8::is_utf8($s)) { return text_dump($s) }
        return "h'" . unpack("H*", $s) . "'";
    }
    if ($flags & B::SVp_IOK) { return "$v" }
    if ($v != $v) { return "nan" }
    if ($v == int($v) && $v >= 0 && $v <= $largest_float_written_as_integer) { return sprintf("%.0f", $v + 0) }
    return "f" . unpack("H*", pack("d>", $v));
}

sub one_line {
    my ($e) = @_;
    $e =~ s/[\t\n]/ /g;
    return "!$e";
}

sub result {
    my ($h) = @_;
    return "-" if $h eq "-";
    my $v = eval { $cbor->decode(pack("H*", $h)) };
    return $@ ? one_line($@) : dump_value($v);
}

sub cycles_write {
    my $itself = [];
    push @$itself, $itself;
    my $holder = {};
    $holder->{a} = $holder;
    my $x = [1];
    my $sharing = CBOR::XS->new->allow_sharing;
    print join("\t", "cbor_xs/array_holds_itself", unpack("H*", $sharing->encode($itself)), "&0[*0]", "cycle"), "\n";
    print join("\t", "cbor_xs/hash_holds_itself", unpack("H*", $sharing->text_keys->encode($holder)), "&0{t'61':*0}",
               "cycle"), "\n";
    print join("\t", "cbor_xs/one_array_twice", unpack("H*", $sharing->encode([$x, $x])), "[&0[1],*0]", "sharing"),
        "\n";
    @$itself = ();
    %$holder = ();
}

sub cycles_read {
    $keys_as_written = 0;
    while (my $line = <STDIN>) {
        chomp $line;
        my ($name, $written) = split /\t/, $line, -1;
        my $v = eval { $cbor->decode(pack("H*", $written)) };
        print join("\t", $name, $@ ? one_line($@) : dump_value($v, targets($v), {}, 0)), "\n";
    }
}

my $mode = $ARGV[0] // "";
if ($mode eq "cycles-write") { cycles_write(); exit 0 }
if ($mode eq "cycles-read") { cycles_read(); exit 0 }
die "usage: cbor_xs_dump.pl [reencode|cycles-write|cycles-read] <lines\n" unless $mode eq "" || $mode eq "reencode";
while (my $line = <STDIN>) {
    chomp $line;
    my @f = split /\t/, $line, -1;
    next if @f != 4 || $f[2] =~ /^!/;
    if ($mode eq "reencode") {
        my $input = $f[3] ne "-" ? $f[3] : $f[2];
        my $v = eval { $cbor->decode(pack("H*", $input)) };
        next if $@;
        my $written = eval { unpack("H*", $writer->encode($v)) };
        if ($@) { print join("\t", $f[0], $input, one_line($@), "-"), "\n"; next }
        print join("\t", $f[0], $input, $written, result($written)), "\n";
        next;
    }
    print join("\t", $f[0], result($f[2]), result($f[3])), "\n";
}
