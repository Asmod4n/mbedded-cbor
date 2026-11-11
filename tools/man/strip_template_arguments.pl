# Takes the template arguments out of the name in the NAME section, so the name is the one the page is installed under.
use strict;
use warnings;
local $/;
my $page = <STDIN>;
$page =~ s{^(\.SH NAME\n)([^\n]*?)( \\- )}{
    my ($head, $name, $dash) = ($1, $2, $3);
    1 while $name =~ s{<[^<>]*>}{};
    $name =~ s{ }{}g;
    "$head$name$dash"
}me;
print $page;
