* OSEXEC — fine-grained OS-command whitelist + switch restrictions (issue #80).
* Runs at the RESTRICTED tier (no MVXPRIV): a command is allowed only by a
* 'permit' grant in this account's .mvx-private/permissions, matched to the
* caller's OS group, and a 'deny' rule can block it or specific switches.
* Argv-style (FM-delimited), so shell metacharacters in an argument are inert.
* This account grants:
*     permit *      = echo        (any group may run echo)
*     permit <grp>  = true        (the caller's primary group may run true)
*     deny   *      = echo -n -r --long   (never echo WITH these switches)
   OUT = ""
   ST = OSEXEC("echo":@FM:"hi there", OUT)
   PRINT "echo: status=":ST:" out=[":OUT:"]"
   ST = OSEXEC("true")
   PRINT "true: status=":ST
   ST = OSEXEC("false")                       ;* command not permitted -> denied
   PRINT "false: status=":ST
   ST = OSEXEC("echo":@FM:"a; echo b", OUT)   ;* one literal arg, no shell
   PRINT "meta: out=[":OUT:"]"
   ST = OSEXEC("echo":@FM:"-n":@FM:"x", OUT)      ;* denied: short -n
   PRINT "deny -n: status=":ST
   ST = OSEXEC("echo":@FM:"-fr":@FM:"x", OUT)     ;* denied: bundled -r inside -fr
   PRINT "deny -fr: status=":ST
   ST = OSEXEC("echo":@FM:"--long":@FM:"x", OUT)  ;* denied: long --long
   PRINT "deny --long: status=":ST
   ST = OSEXEC("echo":@FM:"-f":@FM:"ok", OUT)     ;* -f not denied -> runs
   PRINT "allow -f: status=":ST
   END
