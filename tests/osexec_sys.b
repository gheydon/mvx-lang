* OSEXEC — the system-account layer overrides account permits (issue #80).
* This account's .mvx-private/permissions grants  permit * = echo , but the
* system account ($MVXSYSTEM/.mvx-private/permissions) has  deny * = echo  —
* a deny wins globally, so echo is refused despite the account grant.
   OUT = ""
   ST = OSEXEC("echo":@FM:"hi", OUT)
   PRINT "echo: status=":ST
   END
