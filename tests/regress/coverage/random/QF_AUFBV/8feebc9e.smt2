(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_AUFBV)
(declare-fun v0 () (_ BitVec 15))
(declare-fun v1 () (_ BitVec 1))
(declare-fun v2 () (_ BitVec 6))
(declare-fun v3 () (_ BitVec 7))
(declare-fun v4 () (_ BitVec 15))
(declare-fun a5 () (Array  (_ BitVec 5)  (_ BitVec 5)))
(declare-fun a6 () (Array  (_ BitVec 15)  (_ BitVec 2)))
(assert (let ((e7(_ bv28 6)))
(let ((e8(_ bv1981 11)))
(let ((e9 (ite (bvslt ((_ zero_extend 4) e8) v0) (_ bv1 1) (_ bv0 1))))
(let ((e10 (bvlshr e7 ((_ sign_extend 5) v1))))
(let ((e11 ((_ rotate_left 4) v2)))
(let ((e12 (ite (bvule e7 v2) (_ bv1 1) (_ bv0 1))))
(let ((e13 (bvcomp v4 ((_ zero_extend 14) e12))))
(let ((e14 ((_ rotate_left 6) e8)))
(let ((e15 (ite (bvsge ((_ zero_extend 8) v3) v4) (_ bv1 1) (_ bv0 1))))
(let ((e16 (store a5 ((_ extract 5 1) e10) ((_ zero_extend 4) e13))))
(let ((e17 (store a6 ((_ sign_extend 8) v3) ((_ sign_extend 1) e13))))
(let ((e18 (select e16 ((_ extract 5 1) e7))))
(let ((e19 (store e17 ((_ zero_extend 4) e14) ((_ zero_extend 1) e9))))
(let ((e20 (bvor e8 ((_ sign_extend 5) v2))))
(let ((e21 (bvsub v0 ((_ zero_extend 9) v2))))
(let ((e22 (bvxnor e7 ((_ zero_extend 5) e9))))
(let ((e23 (bvnor e13 e15)))
(let ((e24 (bvsub e7 e22)))
(let ((e25 (bvnot e23)))
(let ((e26 (bvneg e7)))
(let ((e27 (ite (distinct ((_ zero_extend 5) v2) e8) (_ bv1 1) (_ bv0 1))))
(let ((e28 (bvudiv ((_ zero_extend 4) v1) e18)))
(let ((e29 (concat e12 v4)))
(let ((e30 (bvudiv ((_ sign_extend 5) e22) e20)))
(let ((e31 (ite (bvsge v3 ((_ zero_extend 1) e22)) (_ bv1 1) (_ bv0 1))))
(let ((e32 (bvsdiv e10 e26)))
(let ((e33 (bvand e14 e8)))
(let ((e34 (ite (bvsge v2 ((_ zero_extend 5) e13)) (_ bv1 1) (_ bv0 1))))
(let ((e35 (ite (bvugt e32 ((_ sign_extend 5) e9)) (_ bv1 1) (_ bv0 1))))
(let ((e36 (ite (bvsge e11 ((_ sign_extend 5) e31)) (_ bv1 1) (_ bv0 1))))
(let ((e37 (bvsgt ((_ zero_extend 1) e10) v3)))
(let ((e38 (bvult ((_ sign_extend 4) e15) e18)))
(let ((e39 (bvult e9 e23)))
(let ((e40 (bvsge v0 ((_ zero_extend 4) e14))))
(let ((e41 (bvult v1 e15)))
(let ((e42 (bvsle ((_ sign_extend 4) e12) e18)))
(let ((e43 (bvuge e32 ((_ sign_extend 5) e13))))
(let ((e44 (bvsle e27 e35)))
(let ((e45 (bvsle e28 ((_ zero_extend 4) e31))))
(let ((e46 (bvsgt ((_ sign_extend 9) e7) v4)))
(let ((e47 (bvsgt e32 ((_ sign_extend 5) e27))))
(let ((e48 (bvslt ((_ sign_extend 10) e23) e14)))
(let ((e49 (bvugt ((_ zero_extend 5) e27) v2)))
(let ((e50 (bvule ((_ zero_extend 5) e35) e24)))
(let ((e51 (= e10 ((_ zero_extend 5) v1))))
(let ((e52 (bvugt e29 ((_ sign_extend 10) e7))))
(let ((e53 (distinct v4 ((_ zero_extend 9) e22))))
(let ((e54 (bvule e30 ((_ sign_extend 10) e36))))
(let ((e55 (= e23 e23)))
(let ((e56 (bvslt e24 ((_ sign_extend 5) e36))))
(let ((e57 (bvuge v2 e7)))
(let ((e58 (bvuge e12 e35)))
(let ((e59 (bvugt ((_ zero_extend 5) e7) e14)))
(let ((e60 (bvsge ((_ sign_extend 10) e34) e14)))
(let ((e61 (bvult e32 e7)))
(let ((e62 (bvsle e15 e34)))
(let ((e63 (bvsge e32 ((_ zero_extend 5) e13))))
(let ((e64 (bvsge ((_ sign_extend 1) e28) e22)))
(let ((e65 (= e26 ((_ zero_extend 1) e28))))
(let ((e66 (bvsle e33 ((_ zero_extend 5) e24))))
(let ((e67 (bvult ((_ sign_extend 10) e35) e8)))
(let ((e68 (bvugt e11 ((_ zero_extend 5) e35))))
(let ((e69 (bvslt ((_ sign_extend 10) e31) e20)))
(let ((e70 (distinct ((_ sign_extend 15) e27) e29)))
(let ((e71 (bvsge e11 ((_ zero_extend 5) e23))))
(let ((e72 (bvsgt v2 e26)))
(let ((e73 (bvule ((_ sign_extend 5) e12) e11)))
(let ((e74 (bvule e18 ((_ sign_extend 4) e27))))
(let ((e75 (bvuge e32 ((_ sign_extend 5) e13))))
(let ((e76 (bvult e30 e14)))
(let ((e77 (bvugt ((_ zero_extend 10) e25) e20)))
(let ((e78 (bvsle ((_ sign_extend 15) e15) e29)))
(let ((e79 (bvslt e33 ((_ zero_extend 10) e13))))
(let ((e80 (bvugt ((_ sign_extend 10) e18) v4)))
(let ((e81 (bvslt ((_ zero_extend 10) e15) e14)))
(let ((e82 (bvule e30 ((_ sign_extend 6) e18))))
(let ((e83 (= e28 ((_ zero_extend 4) e9))))
(let ((e84 (bvsgt e18 ((_ sign_extend 4) v1))))
(let ((e85 (bvuge e23 e34)))
(let ((e86 (bvsle e34 e31)))
(let ((e87 (bvslt e22 e24)))
(let ((e88 (bvugt e25 v1)))
(let ((e89 (bvugt ((_ zero_extend 1) e22) v3)))
(let ((e90 (bvuge ((_ zero_extend 4) v1) e18)))
(let ((e91 (bvsle e30 ((_ sign_extend 10) v1))))
(let ((e92 (bvsge ((_ sign_extend 5) e36) e32)))
(let ((e93 (bvugt e25 e12)))
(let ((e94 (bvsge ((_ zero_extend 9) e32) e21)))
(let ((e95 (=> e87 e44)))
(let ((e96 (ite e72 e85 e62)))
(let ((e97 (= e63 e51)))
(let ((e98 (ite e37 e96 e55)))
(let ((e99 (=> e67 e91)))
(let ((e100 (=> e54 e42)))
(let ((e101 (= e50 e59)))
(let ((e102 (or e100 e38)))
(let ((e103 (or e94 e52)))
(let ((e104 (=> e70 e74)))
(let ((e105 (and e90 e80)))
(let ((e106 (= e75 e66)))
(let ((e107 (not e69)))
(let ((e108 (=> e60 e97)))
(let ((e109 (=> e45 e98)))
(let ((e110 (xor e46 e95)))
(let ((e111 (ite e82 e89 e57)))
(let ((e112 (= e106 e86)))
(let ((e113 (not e39)))
(let ((e114 (= e88 e83)))
(let ((e115 (ite e101 e93 e76)))
(let ((e116 (ite e71 e114 e109)))
(let ((e117 (ite e104 e53 e77)))
(let ((e118 (=> e47 e110)))
(let ((e119 (or e108 e116)))
(let ((e120 (xor e103 e73)))
(let ((e121 (not e48)))
(let ((e122 (= e64 e56)))
(let ((e123 (and e43 e79)))
(let ((e124 (=> e68 e84)))
(let ((e125 (and e124 e119)))
(let ((e126 (xor e117 e40)))
(let ((e127 (xor e115 e102)))
(let ((e128 (and e121 e81)))
(let ((e129 (= e92 e92)))
(let ((e130 (ite e126 e49 e111)))
(let ((e131 (not e118)))
(let ((e132 (ite e123 e78 e61)))
(let ((e133 (or e130 e120)))
(let ((e134 (or e113 e133)))
(let ((e135 (xor e131 e99)))
(let ((e136 (ite e41 e125 e127)))
(let ((e137 (or e128 e122)))
(let ((e138 (or e58 e135)))
(let ((e139 (xor e138 e136)))
(let ((e140 (= e112 e134)))
(let ((e141 (=> e129 e129)))
(let ((e142 (xor e140 e137)))
(let ((e143 (xor e105 e139)))
(let ((e144 (ite e142 e132 e107)))
(let ((e145 (ite e65 e143 e144)))
(let ((e146 (= e145 e141)))
(let ((e147 (and e146 (not (= e26 (_ bv0 6))))))
(let ((e148 (and e147 (not (= e26 (bvnot (_ bv0 6)))))))
(let ((e149 (and e148 (not (= e18 (_ bv0 5))))))
(let ((e150 (and e149 (not (= e20 (_ bv0 11))))))
e150
)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
