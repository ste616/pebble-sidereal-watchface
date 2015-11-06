#!/usr/bin/perl

use Data::Dumper;
use CGI qw(:standard);
use strict;

# We're here to return the longitude of a city or known observatory, so our
# LST calculator on the Pebble watchface can work.

my $in = CGI->new;
my %input = $in->Vars;

# The name of the file with the list of cities.
my $city_file = "worldcitiespop.txt";

# Debugging.
#$input{'location'} = "perth";

# We only return JSON.
binmode(STDOUT, ":utf8");
print $in->header('text/json; charset=UTF-8');
print "{";

# Get the search term.
if (!$input{'location'}) {
    print '"error": "No location to search for." }\n';
    exit;
}

# Check against our list of known observatories.
my %observatories = (
    'atca' => { 'names' => [ 'ATCA', "Australia Telescope Compact Array" ],
		'longitude' => 149.5501388 }
);

my $s = lc($input{'location'});

if ($observatories{$s}) {
    # We guess that this is what the user wants.
    print '"name": [ "'.$observatories{$s}->{'names'}->[0].'" ]'.
	',"longitude": [ '.$observatories{$s}->{'longitude'}.' ]';
    
} else {

    # Otherwise we grep for that string in the text file.
    my $cmd = "grep \"".$input{'location'}."\" ".$city_file;
    chomp(my @l = `$cmd`);

    print '"name": [ ';
    for (my $i = 0; $i <= $#l; $i++) {
	my @e = split(/\,/, $l[$i]);
	if ($i > 0) {
	    print ",";
	}
	print '"'.$e[2].' ('.uc($e[3]).', '.uc($e[0]).')"';
    }
    print ' ], "longitude": [ ';
    for (my $i = 0; $i <= $#l; $i++) {
	my @e = split(/\,/, $l[$i]);
	if ($i > 0) {
	    print ',';
	}
	my $lo = $e[6];
	if ($lo =~ /^\-\./) {
	    $lo =~ s/^\-\.(.*)$/\-0\.$1/;
	}
	print $lo;
    }
    print ' ]';
}

print "}\n";

exit;
