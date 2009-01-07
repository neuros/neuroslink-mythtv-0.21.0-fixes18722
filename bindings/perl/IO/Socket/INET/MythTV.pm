#!/usr/bin/perl -w
#
# A MythTV Socket class that extends IO::Socket::INET to include some
# MythTV-specific data queries
#
# @url       $URL: http://svn.mythtv.org/svn/branches/release-0-21-fixes/mythtv/bindings/perl/IO/Socket/INET/MythTV.pm $
# @date      $Date: 2007-12-01 15:49:49 -0600 (Sat, 01 Dec 2007) $
# @version   $Revision: 15016 $
# @author    $Author: xris $
# @copyright Silicon Mechanics
#

package IO::Socket::INET::MythTV;
    use base 'IO::Socket::INET';

# Basically, just inherit the constructor from IO::Socket::INET
    sub new {
        my $class = shift;
        return $class->SUPER::new(@_);
    }

# Send data to the connected backend
    sub send_data {
        my $self    = shift;
        my $command = shift;
    # The command format should be <length + whitespace to 8 total bytes><data>
        print $self length($command),
                    ' ' x (8 - length(length($command))),
                    $command;
    }

# Read the response from the backend
    sub read_data {
        my $self = shift;
    # Read the response header to find out how much data we'll be grabbing
        my $result = $self->sysread($length, 8);
        if (! defined $result) {
            warn "Error reading from MythTV backend: $!\n";
            return '';
        }
        elsif ($result == 0) {
            #warn "No data returned by MythTV backend.\n";
            return '';
        }
        $length = int($length);
    # Read and return any data that was returned
        my $ret;
        while ($length > 0) {
            my $bytes = $self->sysread($data, ($length < 262144 ? $length : 262144));
        # Error?
            last unless (defined $bytes);
        # EOF?
            last if ($bytes < 1);
        # On to the next
            $ret .= $data;
            $length -= $bytes;
        }
        return $ret;
    }

# Return true
1;

