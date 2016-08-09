His Shell
=========
A telnet server that is simplified from netkit-telnetd, the authentication and other useless module are removed.

The function that reverse connection was added.

USAGE:
	yoursh [-l port | -r host:port] [-L program] [-n] [-h]
	
	-l port		Bind the <port> and listen, default port 80. (default option)
	-r		Reverse connect to the <port> of speciafied <host>, it is essential to make port forwarding.
	-L program	Login program, default '/bin/sh'.
	-n		Keep connection alive.
	-h		Help

BUILD:
	Type `make` to build.
	There is a file *yoursh* is released.

EXAMPLE:
	$ yoursh -l 10023 -n
	##listen to port 10023 and keep alive.

	$ yoursh -r example.com:9998 
	##run `socat tcp4-listen:9998 tcp4-listen:9999` in your VPS, the port 9999 is telnet service port now.


