# Copyright (C) 2010 Sun Microsystems, Inc. All rights reserved.  Use
# is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

package TestMySQLServer;

use base qw(Test::Unit::TestCase);
use lib 'lib';
use GenTest;
use GenTest::Server::MySQL;

use Data::Dumper;

sub new {
    my $self = shift()->SUPER::new(@_);
    # your state for fixture here
    return $self;
}

sub set_up {
}

@pids;

sub tear_down {
    if (windows) {
	## Need to ,kill leftover processes if there are some
	foreach my $p (@pids) {
	    Win32::Process::KillProcess($p,-1);
	}
	system("rmdir /s /q unit\\tmp");
    } else {
	## Need to ,kill leftover processes if there are some
	kill 9 => @pids;
	system("rm -rf unit/tmp");
    }
}

sub test_create_server {
    my $self = shift;
    if ($ENV{RQG_MYSQL_BASE}) {
	my $server = GenTest::Server::MySQL->new(basedir => $ENV{RQG_MYSQL_BASE},
						 datadir => "./unit/tmp",
						 portbase => 22120);
            
	$self->assert(-f "./unit/tmp/mysql/db.MYD");

	$server->startServer;
	push @pids,$server->serverpid;

	print Dumper($server);

	#$self->assert(-f "./unit/tmp/mysql.pid") if not windows();
	#$self->assert(-f "./unit/tmp/mysql.err");

	## Do some database commands
	#$server->stopServer;

    }
}

1;
