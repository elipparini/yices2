(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_BV)
(declare-fun v0 () (_ BitVec 67))
(declare-fun v1 () (_ BitVec 44))
(declare-fun v2 () (_ BitVec 56))
(assert (let ((e3(_ bv4030253076943881611415095710 92)))
(let ((e4(_ bv12932920561111501 56)))
(let ((e5 (bvshl e3 e3)))
(let ((e6 (ite (bvult e4 ((_ sign_extend 12) v1)) (_ bv1 1) (_ bv0 1))))
(let ((e7 ((_ zero_extend 16) e3)))
(let ((e8 (ite (bvslt e7 ((_ zero_extend 41) v0)) (_ bv1 1) (_ bv0 1))))
(let ((e9 (bvnot e4)))
(let ((e10 (ite (bvugt v0 ((_ sign_extend 11) e4)) (_ bv1 1) (_ bv0 1))))
(let ((e11 ((_ rotate_right 0) e10)))
(let ((e12 (bvcomp e7 e7)))
(let ((e13 (ite (= (_ bv1 1) ((_ extract 0 0) e11)) e5 ((_ sign_extend 91) e11))))
(let ((e14 ((_ rotate_right 43) e13)))
(let ((e15 (bvadd ((_ sign_extend 36) e4) e3)))
(let ((e16 (bvnand e3 e14)))
(let ((e17 (ite (= (_ bv1 1) ((_ extract 39 39) e14)) v1 v1)))
(let ((e18 (bvsdiv ((_ sign_extend 91) e11) e16)))
(let ((e19 (bvsdiv ((_ zero_extend 91) e10) e16)))
(let ((e20 (bvxnor e9 ((_ zero_extend 55) e10))))
(let ((e21 (bvashr ((_ zero_extend 36) e9) e5)))
(let ((e22 ((_ rotate_right 54) v2)))
(let ((e23 (= e7 ((_ zero_extend 107) e8))))
(let ((e24 (bvugt ((_ zero_extend 55) e12) e9)))
(let ((e25 (bvsgt e15 ((_ zero_extend 91) e12))))
(let ((e26 (bvuge e5 ((_ zero_extend 91) e11))))
(let ((e27 (bvuge ((_ sign_extend 36) e4) e13)))
(let ((e28 (bvult e5 ((_ sign_extend 36) e4))))
(let ((e29 (bvult v0 ((_ zero_extend 23) e17))))
(let ((e30 (bvsgt v2 v2)))
(let ((e31 (bvult e6 e10)))
(let ((e32 (bvuge e4 ((_ zero_extend 55) e6))))
(let ((e33 (bvult ((_ zero_extend 66) e10) v0)))
(let ((e34 (bvsgt ((_ zero_extend 91) e10) e5)))
(let ((e35 (bvugt e15 ((_ sign_extend 36) e20))))
(let ((e36 (bvule ((_ zero_extend 36) e9) e18)))
(let ((e37 (bvuge e13 ((_ zero_extend 91) e8))))
(let ((e38 (distinct e6 e8)))
(let ((e39 (bvuge e19 ((_ sign_extend 91) e10))))
(let ((e40 (bvugt ((_ sign_extend 36) e22) e15)))
(let ((e41 (distinct e6 e11)))
(let ((e42 (= e5 ((_ zero_extend 36) e9))))
(let ((e43 (= ((_ sign_extend 91) e6) e13)))
(let ((e44 (bvslt e4 ((_ zero_extend 12) e17))))
(let ((e45 (bvsle e16 ((_ zero_extend 36) e20))))
(let ((e46 (bvsge ((_ sign_extend 91) e6) e21)))
(let ((e47 (bvuge e14 e3)))
(let ((e48 (bvule ((_ sign_extend 91) e11) e16)))
(let ((e49 (bvult e11 e6)))
(let ((e50 (bvult ((_ sign_extend 43) e11) v1)))
(let ((e51 (or e47 e50)))
(let ((e52 (=> e23 e24)))
(let ((e53 (= e34 e27)))
(let ((e54 (ite e51 e29 e29)))
(let ((e55 (or e42 e32)))
(let ((e56 (not e25)))
(let ((e57 (not e44)))
(let ((e58 (and e45 e33)))
(let ((e59 (= e26 e26)))
(let ((e60 (not e49)))
(let ((e61 (not e48)))
(let ((e62 (or e46 e38)))
(let ((e63 (not e56)))
(let ((e64 (= e36 e55)))
(let ((e65 (or e31 e64)))
(let ((e66 (or e59 e65)))
(let ((e67 (and e58 e61)))
(let ((e68 (= e37 e66)))
(let ((e69 (or e60 e68)))
(let ((e70 (or e69 e30)))
(let ((e71 (= e62 e67)))
(let ((e72 (xor e28 e39)))
(let ((e73 (=> e53 e71)))
(let ((e74 (or e52 e52)))
(let ((e75 (xor e41 e63)))
(let ((e76 (and e74 e75)))
(let ((e77 (xor e35 e57)))
(let ((e78 (and e76 e72)))
(let ((e79 (=> e54 e77)))
(let ((e80 (and e78 e79)))
(let ((e81 (not e43)))
(let ((e82 (=> e70 e40)))
(let ((e83 (=> e80 e82)))
(let ((e84 (xor e73 e81)))
(let ((e85 (not e84)))
(let ((e86 (= e85 e85)))
(let ((e87 (and e86 e83)))
(let ((e88 (and e87 (not (= e16 (_ bv0 92))))))
(let ((e89 (and e88 (not (= e16 (bvnot (_ bv0 92)))))))
e89
))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)