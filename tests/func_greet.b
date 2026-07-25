* String-returning FUNCTION; a value-less RETURN yields "".
FUNCTION GREET(NM)
   IF NM = "" THEN RETURN
   RETURN("Hello, ":NM:"!")
