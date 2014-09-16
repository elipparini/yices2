(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_BV)
(declare-fun v0 () (_ BitVec 8))
(declare-fun v1 () (_ BitVec 4))
(declare-fun v2 () (_ BitVec 15))
(declare-fun v3 () (_ BitVec 4))
(declare-fun v4 () (_ BitVec 14))
(assert (let ((e5(_ bv0 1)))
(let ((e6(_ bv942 11)))
(let ((e7 (ite (bvult e5 e5) (_ bv1 1) (_ bv0 1))))
(let ((e8 (ite (distinct ((_ zero_extend 7) v1) e6) (_ bv1 1) (_ bv0 1))))
(let ((e9 (ite (bvsle ((_ sign_extend 13) e8) v4) (_ bv1 1) (_ bv0 1))))
(let ((e10 (bvsub v0 ((_ zero_extend 7) e7))))
(let ((e11 (ite (= (_ bv1 1) ((_ extract 0 0) e7)) e9 e9)))
(let ((e12 (bvcomp ((_ zero_extend 6) v0) v4)))
(let ((e13 (bvadd v0 ((_ zero_extend 7) e9))))
(let ((e14 ((_ sign_extend 6) v1)))
(let ((e15 (bvnot v1)))
(let ((e16 (ite (= v4 ((_ sign_extend 13) e7)) (_ bv1 1) (_ bv0 1))))
(let ((e17 (bvor e12 e8)))
(let ((e18 (bvnand v0 ((_ zero_extend 7) e7))))
(let ((e19 (bvmul v0 e10)))
(let ((e20 (bvmul v4 ((_ zero_extend 10) v1))))
(let ((e21 (bvsrem ((_ zero_extend 3) e16) e15)))
(let ((e22 (bvneg e6)))
(let ((e23 (bvneg e13)))
(let ((e24 ((_ extract 0 0) e5)))
(let ((e25 (ite (bvsgt ((_ zero_extend 10) e9) e22) (_ bv1 1) (_ bv0 1))))
(let ((e26 (bvand e23 v0)))
(let ((e27 ((_ zero_extend 7) e7)))
(let ((e28 (ite (bvsle e27 e10) (_ bv1 1) (_ bv0 1))))
(let ((e29 (ite (bvule ((_ zero_extend 7) e16) v0) (_ bv1 1) (_ bv0 1))))
(let ((e30 (ite (bvslt e5 e28) (_ bv1 1) (_ bv0 1))))
(let ((e31 ((_ zero_extend 5) e25)))
(let ((e32 ((_ repeat 2) e27)))
(let ((e33 (bvneg e27)))
(let ((e34 (bvadd v0 e18)))
(let ((e35 (ite (bvult e21 v3) (_ bv1 1) (_ bv0 1))))
(let ((e36 (bvsdiv ((_ sign_extend 3) e12) v1)))
(let ((e37 (ite (bvult e20 ((_ zero_extend 10) e15)) (_ bv1 1) (_ bv0 1))))
(let ((e38 (bvmul e19 ((_ sign_extend 4) v3))))
(let ((e39 (ite (bvslt ((_ sign_extend 4) v3) e18) (_ bv1 1) (_ bv0 1))))
(let ((e40 (ite (bvugt e27 ((_ zero_extend 2) e31)) (_ bv1 1) (_ bv0 1))))
(let ((e41 (ite (bvuge e37 e12) (_ bv1 1) (_ bv0 1))))
(let ((e42 (ite (bvuge ((_ zero_extend 9) e8) e14) (_ bv1 1) (_ bv0 1))))
(let ((e43 ((_ repeat 16) e5)))
(let ((e44 (bvor e38 ((_ sign_extend 4) e15))))
(let ((e45 (ite (bvsgt e35 e9) (_ bv1 1) (_ bv0 1))))
(let ((e46 (bvxor e36 ((_ sign_extend 3) e9))))
(let ((e47 (ite (bvsle e22 ((_ zero_extend 7) v3)) (_ bv1 1) (_ bv0 1))))
(let ((e48 (bvneg e36)))
(let ((e49 ((_ rotate_right 2) e48)))
(let ((e50 (bvmul e38 ((_ zero_extend 7) e42))))
(let ((e51 ((_ rotate_left 9) v2)))
(let ((e52 (bvsgt ((_ sign_extend 7) e39) v0)))
(let ((e53 (bvsgt ((_ zero_extend 4) v3) e44)))
(let ((e54 (bvuge e51 ((_ zero_extend 14) e41))))
(let ((e55 (bvsgt ((_ zero_extend 7) e12) e23)))
(let ((e56 (bvugt e13 ((_ zero_extend 7) e24))))
(let ((e57 (distinct ((_ zero_extend 15) e5) e32)))
(let ((e58 (bvuge ((_ zero_extend 7) e9) e50)))
(let ((e59 (bvslt e35 e35)))
(let ((e60 (bvsge ((_ zero_extend 15) e17) e32)))
(let ((e61 (bvsle e12 e39)))
(let ((e62 (bvugt e34 e19)))
(let ((e63 (bvule e11 e5)))
(let ((e64 (= ((_ zero_extend 10) v3) v4)))
(let ((e65 (bvsle ((_ zero_extend 3) e26) e22)))
(let ((e66 (bvult e22 ((_ sign_extend 10) e42))))
(let ((e67 (bvsge ((_ zero_extend 13) e30) e20)))
(let ((e68 (= ((_ sign_extend 7) e7) e23)))
(let ((e69 (bvsge e51 ((_ sign_extend 14) e25))))
(let ((e70 (bvsge e17 e47)))
(let ((e71 (bvsle e46 ((_ sign_extend 3) e45))))
(let ((e72 (bvugt e42 e25)))
(let ((e73 (bvsgt e13 ((_ sign_extend 7) e47))))
(let ((e74 (bvule e28 e35)))
(let ((e75 (bvugt ((_ zero_extend 4) e36) e26)))
(let ((e76 (bvsge ((_ zero_extend 7) e7) e50)))
(let ((e77 (bvuge e9 e11)))
(let ((e78 (bvslt ((_ sign_extend 10) e7) e6)))
(let ((e79 (bvult e12 e29)))
(let ((e80 (bvugt ((_ zero_extend 7) e30) e50)))
(let ((e81 (bvule e39 e41)))
(let ((e82 (bvugt e48 ((_ sign_extend 3) e5))))
(let ((e83 (bvsge e21 ((_ sign_extend 3) e28))))
(let ((e84 (bvuge e32 ((_ sign_extend 15) e8))))
(let ((e85 (bvult e12 e7)))
(let ((e86 (= e36 ((_ sign_extend 3) e35))))
(let ((e87 (bvsle ((_ zero_extend 7) e12) e13)))
(let ((e88 (bvuge e34 e10)))
(let ((e89 (bvuge e20 ((_ sign_extend 13) e16))))
(let ((e90 (bvugt e36 e36)))
(let ((e91 (bvsge e18 ((_ zero_extend 4) e49))))
(let ((e92 (bvslt e29 e16)))
(let ((e93 (bvule e20 ((_ zero_extend 10) v3))))
(let ((e94 (bvule v1 ((_ zero_extend 3) e28))))
(let ((e95 (bvuge ((_ zero_extend 5) e6) e32)))
(let ((e96 (bvult e16 e39)))
(let ((e97 (distinct e26 ((_ sign_extend 7) e25))))
(let ((e98 (bvuge ((_ sign_extend 14) e8) e51)))
(let ((e99 (bvsge e15 ((_ zero_extend 3) e28))))
(let ((e100 (bvuge e27 ((_ sign_extend 7) e25))))
(let ((e101 (distinct e12 e7)))
(let ((e102 (= e18 ((_ sign_extend 4) e46))))
(let ((e103 (bvsgt e28 e17)))
(let ((e104 (bvslt ((_ zero_extend 7) e26) e51)))
(let ((e105 (bvult e25 e35)))
(let ((e106 (bvule e29 e5)))
(let ((e107 (bvult e34 e27)))
(let ((e108 (distinct ((_ sign_extend 15) e25) e43)))
(let ((e109 (bvslt e31 ((_ zero_extend 5) e9))))
(let ((e110 (bvule ((_ sign_extend 7) e41) e38)))
(let ((e111 (bvule e19 e26)))
(let ((e112 (bvsgt ((_ sign_extend 7) e25) e10)))
(let ((e113 (bvsge e11 e35)))
(let ((e114 (bvsle e33 ((_ zero_extend 7) e41))))
(let ((e115 (distinct e6 ((_ sign_extend 10) e42))))
(let ((e116 (bvsle v0 ((_ sign_extend 7) e45))))
(let ((e117 (bvuge v3 ((_ zero_extend 3) e9))))
(let ((e118 (bvuge e51 ((_ sign_extend 14) e16))))
(let ((e119 (bvugt e49 e36)))
(let ((e120 (= e25 e9)))
(let ((e121 (bvule ((_ zero_extend 3) e42) v1)))
(let ((e122 (bvuge ((_ sign_extend 4) e48) e26)))
(let ((e123 (bvslt e27 ((_ zero_extend 4) e48))))
(let ((e124 (bvsgt e27 ((_ sign_extend 7) e37))))
(let ((e125 (bvsle e33 e10)))
(let ((e126 (bvsge e10 e26)))
(let ((e127 (bvugt e43 e32)))
(let ((e128 (bvult e20 v4)))
(let ((e129 (bvsgt ((_ zero_extend 4) e21) e44)))
(let ((e130 (bvugt ((_ zero_extend 7) e33) v2)))
(let ((e131 (bvule v2 ((_ zero_extend 5) e14))))
(let ((e132 (bvugt e8 e11)))
(let ((e133 (bvule e51 ((_ zero_extend 14) e7))))
(let ((e134 (bvsle e24 e28)))
(let ((e135 (= ((_ sign_extend 10) e9) e6)))
(let ((e136 (bvsge ((_ zero_extend 3) e6) v4)))
(let ((e137 (bvuge e10 ((_ zero_extend 7) e7))))
(let ((e138 (bvult ((_ zero_extend 7) e49) e22)))
(let ((e139 (distinct e20 ((_ zero_extend 13) e25))))
(let ((e140 (bvult ((_ zero_extend 14) e7) v2)))
(let ((e141 (bvugt e40 e11)))
(let ((e142 (xor e78 e128)))
(let ((e143 (xor e58 e80)))
(let ((e144 (=> e100 e111)))
(let ((e145 (= e113 e92)))
(let ((e146 (or e86 e126)))
(let ((e147 (or e88 e121)))
(let ((e148 (ite e115 e134 e130)))
(let ((e149 (xor e114 e116)))
(let ((e150 (=> e110 e90)))
(let ((e151 (= e70 e147)))
(let ((e152 (and e65 e61)))
(let ((e153 (or e69 e72)))
(let ((e154 (ite e59 e53 e142)))
(let ((e155 (ite e135 e133 e118)))
(let ((e156 (= e99 e105)))
(let ((e157 (ite e150 e74 e145)))
(let ((e158 (=> e109 e131)))
(let ((e159 (not e148)))
(let ((e160 (not e153)))
(let ((e161 (or e108 e93)))
(let ((e162 (not e57)))
(let ((e163 (ite e85 e127 e141)))
(let ((e164 (ite e64 e139 e149)))
(let ((e165 (not e124)))
(let ((e166 (not e77)))
(let ((e167 (and e63 e138)))
(let ((e168 (and e123 e156)))
(let ((e169 (= e52 e62)))
(let ((e170 (or e66 e120)))
(let ((e171 (or e119 e82)))
(let ((e172 (xor e97 e98)))
(let ((e173 (xor e79 e71)))
(let ((e174 (not e171)))
(let ((e175 (xor e132 e157)))
(let ((e176 (= e175 e136)))
(let ((e177 (or e107 e140)))
(let ((e178 (and e95 e164)))
(let ((e179 (not e101)))
(let ((e180 (ite e75 e167 e81)))
(let ((e181 (= e152 e162)))
(let ((e182 (and e137 e144)))
(let ((e183 (=> e102 e163)))
(let ((e184 (= e180 e84)))
(let ((e185 (or e89 e155)))
(let ((e186 (ite e117 e182 e55)))
(let ((e187 (or e106 e177)))
(let ((e188 (ite e161 e112 e143)))
(let ((e189 (ite e76 e91 e87)))
(let ((e190 (and e188 e165)))
(let ((e191 (not e73)))
(let ((e192 (=> e158 e67)))
(let ((e193 (not e192)))
(let ((e194 (=> e174 e151)))
(let ((e195 (xor e187 e191)))
(let ((e196 (not e159)))
(let ((e197 (= e125 e184)))
(let ((e198 (= e129 e154)))
(let ((e199 (=> e104 e185)))
(let ((e200 (ite e181 e56 e178)))
(let ((e201 (and e94 e103)))
(let ((e202 (ite e166 e193 e196)))
(let ((e203 (or e195 e202)))
(let ((e204 (and e197 e96)))
(let ((e205 (ite e169 e203 e169)))
(let ((e206 (= e198 e83)))
(let ((e207 (= e68 e60)))
(let ((e208 (and e54 e122)))
(let ((e209 (xor e176 e170)))
(let ((e210 (xor e207 e189)))
(let ((e211 (xor e172 e208)))
(let ((e212 (xor e168 e173)))
(let ((e213 (xor e209 e146)))
(let ((e214 (ite e212 e213 e212)))
(let ((e215 (xor e199 e186)))
(let ((e216 (= e205 e200)))
(let ((e217 (xor e179 e201)))
(let ((e218 (= e194 e160)))
(let ((e219 (not e216)))
(let ((e220 (or e211 e206)))
(let ((e221 (or e220 e214)))
(let ((e222 (xor e217 e204)))
(let ((e223 (=> e221 e219)))
(let ((e224 (=> e218 e215)))
(let ((e225 (= e224 e183)))
(let ((e226 (and e190 e223)))
(let ((e227 (= e226 e226)))
(let ((e228 (=> e227 e222)))
(let ((e229 (ite e228 e210 e228)))
(let ((e230 (or e225 e229)))
(let ((e231 (and e230 (not (= e15 (_ bv0 4))))))
(let ((e232 (and e231 (not (= e15 (bvnot (_ bv0 4)))))))
(let ((e233 (and e232 (not (= v1 (_ bv0 4))))))
(let ((e234 (and e233 (not (= v1 (bvnot (_ bv0 4)))))))
e234
)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
