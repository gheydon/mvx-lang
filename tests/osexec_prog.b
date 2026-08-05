* OSEXEC — program-identity binding (issue #80, ARCHITECTURE.md 8.4, constraint
* 3).  This program is granted echo ONLY via  permit prog:TESTPROG = echo  plus
* a system blessing of its binary's sha256 (program TESTPROG = <hash>).  With the
* correct hash blessed it runs; with a stale hash — a re-cataloged or substituted
* binary — the grant no longer matches and it is refused.
   OUT = ""
   ST = OSEXEC("echo":@FM:"blessed", OUT)
   PRINT "echo: status=":ST
   END
