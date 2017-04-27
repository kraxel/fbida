#!/usr/bin/perl -w
#
# build header file from
#
use strict;

my $in  = shift;
my $out = shift;

open(IN, "<", $in) or die "open (read): $in";
open(OUT, ">", $out) or die "open (write): $out";

while (my $line = <IN>) {
	chomp $line;

	# ignore comments
	next if $line =~ /^!/;
#	next if $line =~ /^\s*$/;

	# quote stuff
	$line =~ s/\\/\\\\/g;
	$line =~ s/\"/\\\"/g;

	# continued line?
	if ($line =~ s/\\\\$//) {
		printf OUT "\"%s\"\n",$line;
		next;
	}

	# write out
	printf OUT "\"%s\",\n",$line;
}
