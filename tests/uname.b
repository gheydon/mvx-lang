* UNAME(which) — read-only platform info (issue #80), ungated at any tier.
* Values vary by host, so assert structure: fields are non-empty, the first
* letter selects the field, and no argument defaults to sysname.
   S = UNAME("s")
   M = UNAME("m")
   PRINT "sysname nonempty: ":(S # "")
   PRINT "machine nonempty: ":(M # "")
   PRINT "default is sysname: ":(UNAME("") = S)
   PRINT "first-letter selects: ":(UNAME("sysname") = S)
   PRINT "machine distinct key: ":(UNAME("m") = M)
   END
