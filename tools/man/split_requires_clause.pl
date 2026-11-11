# Doxygen 1.17 writes a requires-clause of a template member into the man SYNOPSIS without a break before
# the return type and with the quote of .RI on the wrong line. This puts the clause on a line of its own.
use strict;
use warnings;
local $/;
my $page = <STDIN>;
$page =~ s{^(\.RI "template<[^\n]*?) ?\n\.br\n(requires [^"]*?)"\n}{
    my ($head, $clause) = ($1, $2);
    $clause =~ s{([>)])(?=[A-Za-z_]|\\fB)}{$1\n.br\n}
        or die "split_requires_clause: no return type after the clause: $clause\n";
    "$head\"\n.br\n$clause\n"
}gme;
print $page;
