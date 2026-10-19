# Sony workarounds, and an optional "TV off, Host suspends"

The TV is a Sony FW-55BZ40H. libCEC documents three Sony faults: the TV forgets its
sources while it is off, a 2020 Sony can wake with an Active Source `0.0.0.0`, and on a
source change the TV sends User Control Power (`0x40`) and then Power On (`0x6D`) about
275 ms later (libCEC issues #744 and #753, `SYCommandHandler.cpp`). Our Host uses the
kernel driver and `cec-ctl`, not libCEC, so we handle the faults ourselves. After the TV
wakes, send Report Physical Address and, if we are the active source, Active Source again.
Treat a `0x40` followed by `0x6D` within 500 ms as Power On, not as a power toggle.
While Attached, a Host daemon does this, because the kernel owns the Identity. While
Detached, the Adapter does it. The same Host daemon has an optional rule: when the TV
broadcasts Standby, the Host suspends after 60 s. It is off by default, so that a test
does not suspend the Host by surprise.
