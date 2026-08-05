* open-dict round-trip prototype (issue #25).  Converts mvx native dictionary
* items to the canonical *open-dict* form and back, and derives the cross-
* platform semantics a target dialect needs.  Proves the mapping is lossless.
*
*   native mvx D-item : 1=D/I  2=attr#/expr  3=conv  4=heading  5=format  6=assoc
*   open-dict record  : 1..6 as native (mvx is the reference dialect) PLUS
*                       7=single/multi (S/M)   8=role (C=controlling/D=dependent)
* Fields 7-8 are DERIVED on export (advisory for the importer); import needs only
* 1-6, so native -> open -> native round-trips exactly.  A small dictionary with
* an association (INV: QTY/PRICE/DESC) exercises the derived role.
   NAMES = "CUSTNO":@AM:"AMOUNT":@AM:"ODATE":@AM:"QTY":@AM:"PRICE":@AM:"DESC":@AM:"FULLNAME"
   DICT = ""
   DICT<1> = "D":@VM:"1":@VM:"":@VM:"Cust":@VM:"6R":@VM:""
   DICT<2> = "D":@VM:"3":@VM:"MD2":@VM:"Amount":@VM:"10R":@VM:""
   DICT<3> = "D":@VM:"2":@VM:"D2/":@VM:"Date":@VM:"10R":@VM:""
   DICT<4> = "D":@VM:"5":@VM:"":@VM:"Qty":@VM:"6R":@VM:"INV"
   DICT<5> = "D":@VM:"6":@VM:"MD2":@VM:"Price":@VM:"10R":@VM:"INV"
   DICT<6> = "D":@VM:"7":@VM:"":@VM:"Desc":@VM:"20L":@VM:"INV"
   DICT<7> = "I":@VM:'FIRST" "LAST':@VM:"":@VM:"Full Name":@VM:"30L":@VM:""
   N = DCOUNT(NAMES, @AM)
   FOR I = 1 TO N
      TY = DICT<I,1> ; SRC = DICT<I,2> ; CV = DICT<I,3>
      HD = DICT<I,4> ; FM = DICT<I,5> ; AS = DICT<I,6>
      * ---- export: native -> open (1-6 verbatim, derive 7-8) ----
      SM = "S" ; IF AS # "" THEN SM = "M"
      RO = ""
      IF AS # "" AND TY = "D" THEN
         RO = "C"
         FOR J = 1 TO N
            IF J # I AND DICT<J,6> = AS AND DICT<J,1> = "D" THEN
               IF (DICT<J,2> + 0) < (SRC + 0) THEN RO = "D"
            END
         NEXT J
      END
      O = TY:@VM:SRC:@VM:CV:@VM:HD:@VM:FM:@VM:AS:@VM:SM:@VM:RO
      * ---- import: open -> native (needs only 1-6) ----
      N2 = O<1,1>:@VM:O<1,2>:@VM:O<1,3>:@VM:O<1,4>:@VM:O<1,5>:@VM:O<1,6>
      RT = "ok" ; IF N2 # DICT<I> THEN RT = "MISMATCH"
      PRINT NAMES<I> : " open=" : CHANGE(O, @VM, "|") : " rt=" : RT
   NEXT I
   END
