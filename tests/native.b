* Native filesystem primitives — MKDIR / RMTREE / UNTAR (issue #80).
* No shell, permit-gated by op name.  This account grants  permit * = mkdir
* untar  but NOT rmtree, so mkdir + untar run while rmtree is refused (-1).
   RC = MKDIR("nativetest/sub")
   PRINT "mkdir: ":RC
   RC = UNTAR("fixture.tgz", "nativetest")
   PRINT "untar: ":RC
   T = OSREAD("nativetest/marker.txt")
   PRINT "content: ":TRIM(T)
   RC = RMTREE("nativetest")
   PRINT "rmtree (not permitted): ":RC
   END
