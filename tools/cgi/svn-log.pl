#!/usr/bin/perl
# Generate a nice log format for the Subversion repository.

use strict;

my $repos = shift @ARGV;

# Make sure we got all the arguments we wanted
if ((not defined $repos) or ($repos eq ''))
   {
       print "Usage: svn_logs.pl REPOS-PATH\n\n";
       exit;
   }

# Get the youngest revision in the repository.
my $youngest = `svnadmin youngest $repos`;
chomp $youngest;    # don't want carriage return
die ("Error using svnadmin to get youngest revision") if (not $youngest =~
/^\d/);

while ($youngest >= 1)
   {
       print "--------------------------------------------------------\n";
       print "Revision $youngest\n";
       print `svnlook $repos rev $youngest info`;
       print "\n";
       $youngest--;
   }

