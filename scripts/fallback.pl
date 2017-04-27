#!/usr/bin/perl -w
#
# build header file from
#
use strict;

while (my $line = <>) {
	chomp $line;

	# ignore comments
	next if $line =~ /^!/;
#	next if $line =~ /^\s*$/;

	# quote stuff
	$line =~ s/\\/\\\\/g;
	$line =~ s/\"/\\\"/g;

	# continued line?
	if ($line =~ s/\\\\$//) {
		printf "\"%s\"\n",$line;
		next;
	}

	# write out
	printf "\"%s\",\n",$line;
}
