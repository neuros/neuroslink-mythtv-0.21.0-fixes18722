#!/usr/bin/perl -w
#
# Connects to the mythtv database and repairs/optimizes the tables that it
# finds.  Suggested use is to cron it to run once per day.
#
# @url       $URL$
# @date      $Date: 2007-08-04 04:25:32 -0500 (Sat, 04 Aug 2007) $
# @version   $Revision: 14140 $
# @author    $Author: xris $
# @license   GPL
#

# Includes
    use DBI;
    use MythTV;

# Connect to mythbackend
    my $Myth = new MythTV({'connect' => 0});

# Connect to the database
    $dbh = $Myth->{'dbh'};

# Repair and optimize each table
    foreach $table ($dbh->tables) {
        unless ($dbh->do("REPAIR TABLE $table")) {
            print "Skipped:  $table\n";
            next;
        };
        if ($dbh->do("OPTIMIZE TABLE $table")) {
            print "Repaired/Optimized: $table\n";
        }
        if ($dbh->do("ANALYZE TABLE $table")) {
            print "Analyzed: $table\n";
        }
    }

