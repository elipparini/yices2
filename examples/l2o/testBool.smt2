(set-logic QF_NRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun w () Real)
(declare-fun b () Bool)
(assert b)
;(assert (or (and b (= x y)) (and (not b) (= x w))))
(assert (or b (= x y)))
(assert (or (not b) (= z w)))
;(assert (= x y))
(assert (= (+ (* z z) (* w w)) 1))
;(assert (>= (+ (* (- x z) (- x z)) (* (- y w) (- y w))) 1))
(check-sat)