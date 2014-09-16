(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_LRA)
(declare-fun v0 () Real)
(assert (let ((e1 1))
(let ((e2 (- v0 v0)))
(let ((e3 (/ e1 (- e1))))
(let ((e4 (> v0 e2)))
(let ((e5 (= e3 e2)))
(let ((e6 (ite e4 e2 e2)))
(let ((e7 (ite e5 e3 e3)))
(let ((e8 (ite e4 v0 e7)))
(let ((e9 (< e8 e2)))
(let ((e10 (< v0 e3)))
(let ((e11 (> e3 e6)))
(let ((e12 (>= e2 e7)))
(let ((e13 (not e5)))
(let ((e14 (or e13 e4)))
(let ((e15 (=> e12 e14)))
(let ((e16 (and e15 e10)))
(let ((e17 (ite e11 e11 e16)))
(let ((e18 (and e17 e9)))
e18
)))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
