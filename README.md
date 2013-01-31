kexec
=====

port knock a host and then execute a command

Description
-----------
kexec is expected to by symlinked to a hostname and $HOME/.kexec should provide
a configuration entry for that hostname. If this is done, you can knock the host
and execute an arbitrary command after the knocking sequence is executed:
```
host1.net git push
```
This looks up the configuration for host1.net in $HOME/.kexec, executes the
defined knocking sequence, and then executes git push.

Config
------
```
$ cat $HOME/.kexec
#this is a comment
host1.net: tcp:123, tcp: 456
host2.net: udp:145, udp: 453, udp:347
```

Running kexec
-------------
```
$ cp kexec /usr/local/bin
$ cd /usr/local/bin
$ ln -s kexec host1.net
$ ln -s kexec host2.net
$ host1.net ssh host1.net
$ host2.net git push
```

Build
-----
```
$ gcc -std=c99 -D_XOPEN_SOURCE=500 -Wall -pedantic -o kexec kexec.c
```
