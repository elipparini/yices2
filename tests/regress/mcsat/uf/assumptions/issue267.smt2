(set-logic QF_UFNIA)
(declare-const v1 Bool)
(declare-const v2 Bool)
(declare-const v3 Bool)
(declare-const v4 Bool)
(declare-const v6 Bool)
(declare-const v7 Bool)
(declare-const i7 Int)
(declare-const i8 Int)
(declare-const i9 Int)
(declare-const i10 Int)
(declare-const i12 Int)
(declare-const i14 Int)
(declare-const i15 Int)
(declare-const i16 Int)
(declare-const i17 Int)
(declare-const i18 Int)
(declare-const v12 Bool)
(assert v3)
(assert (= (distinct v1 v6) v3 (= 760 (mod 89 i8)) v12 v7 (= v7 v2 v3 v4 v7) v6))
(declare-const i20 Int)
(check-sat-assuming-model (i7 i8 i9 i10 i12 i14 i15 i16 i17 i18 i20 ) (43573 22243 26500 30479 33328 5345 20750 9155 4252 28397 46446 ))
(check-sat)