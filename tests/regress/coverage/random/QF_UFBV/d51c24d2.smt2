(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_UFBV)
(declare-fun f0 ( (_ BitVec 11) (_ BitVec 15)) (_ BitVec 15))
(declare-fun f1 ( (_ BitVec 4)) (_ BitVec 9))
(declare-fun p0 ( (_ BitVec 4)) Bool)
(declare-fun v0 () (_ BitVec 16))
(declare-fun v1 () (_ BitVec 8))
(declare-fun v2 () (_ BitVec 6))
(declare-fun v3 () (_ BitVec 9))
(declare-fun v4 () (_ BitVec 15))
(assert (let ((e5(_ bv41 6)))
(let ((e6 (f1 ((_ extract 3 0) e5))))
(let ((e7 (f0 ((_ sign_extend 5) e5) v4)))
(let ((e8 (ite (p0 ((_ extract 9 6) e7)) (_ bv1 1) (_ bv0 1))))
(let ((e9 ((_ zero_extend 11) e8)))
(let ((e10 (bvor ((_ zero_extend 9) v2) v4)))
(let ((e11 (f0 ((_ extract 11 1) v0) e7)))
(let ((e12 (ite (bvsge ((_ zero_extend 5) e8) v2) (_ bv1 1) (_ bv0 1))))
(let ((e13 (bvcomp e7 ((_ sign_extend 9) e5))))
(let ((e14 (bvnot e7)))
(let ((e15 (ite (bvsle v2 ((_ zero_extend 5) e13)) (_ bv1 1) (_ bv0 1))))
(let ((e16 (bvashr ((_ zero_extend 3) e9) v4)))
(let ((e17 (bvor e5 ((_ sign_extend 5) e15))))
(let ((e18 (ite (bvuge v1 v1) (_ bv1 1) (_ bv0 1))))
(let ((e19 (bvsdiv ((_ zero_extend 14) e18) e10)))
(let ((e20 (bvxnor ((_ sign_extend 2) e5) v1)))
(let ((e21 (bvashr e19 ((_ zero_extend 9) v2))))
(let ((e22 (bvlshr v4 ((_ sign_extend 6) e6))))
(let ((e23 (bvor e11 ((_ sign_extend 3) e9))))
(let ((e24 (bvashr e15 e15)))
(let ((e25 (ite (bvsgt ((_ sign_extend 9) v2) e23) (_ bv1 1) (_ bv0 1))))
(let ((e26 (ite (bvsle e24 e12) (_ bv1 1) (_ bv0 1))))
(let ((e27 (bvnand e5 ((_ zero_extend 5) e26))))
(let ((e28 (bvnor v1 v1)))
(let ((e29 (bvsub ((_ zero_extend 15) e25) v0)))
(let ((e30 (ite (= ((_ sign_extend 15) e12) e29) (_ bv1 1) (_ bv0 1))))
(let ((e31 (ite (p0 ((_ extract 6 3) e9)) (_ bv1 1) (_ bv0 1))))
(let ((e32 (bvashr ((_ zero_extend 14) e15) v4)))
(let ((e33 (ite (bvugt ((_ sign_extend 5) e18) e27) (_ bv1 1) (_ bv0 1))))
(let ((e34 (bvsrem e10 e10)))
(let ((e35 ((_ repeat 3) e8)))
(let ((e36 (bvlshr v1 ((_ sign_extend 7) e24))))
(let ((e37 (bvsrem ((_ zero_extend 8) e28) v0)))
(let ((e38 ((_ rotate_left 6) v0)))
(let ((e39 (bvudiv e26 e15)))
(let ((e40 (bvnot e7)))
(let ((e41 ((_ rotate_left 4) e37)))
(let ((e42 (ite (= (_ bv1 1) ((_ extract 3 3) e27)) ((_ zero_extend 12) e35) e11)))
(let ((e43 (bvor e10 e40)))
(let ((e44 ((_ repeat 2) e25)))
(let ((e45 ((_ repeat 1) e43)))
(let ((e46 (bvudiv ((_ sign_extend 5) e31) e27)))
(let ((e47 (bvlshr e19 e22)))
(let ((e48 ((_ extract 0 0) e26)))
(let ((e49 (ite (= (_ bv1 1) ((_ extract 0 0) e26)) ((_ zero_extend 5) e12) e17)))
(let ((e50 (ite (p0 ((_ zero_extend 3) e15)) (_ bv1 1) (_ bv0 1))))
(let ((e51 (ite (= e40 e34) (_ bv1 1) (_ bv0 1))))
(let ((e52 (bvsdiv ((_ sign_extend 6) v3) e42)))
(let ((e53 (p0 ((_ extract 13 10) e19))))
(let ((e54 (bvsgt ((_ sign_extend 9) e27) e7)))
(let ((e55 (bvuge ((_ sign_extend 3) v3) e9)))
(let ((e56 (p0 ((_ zero_extend 3) e30))))
(let ((e57 (bvuge e19 e43)))
(let ((e58 (bvugt ((_ sign_extend 5) e31) e17)))
(let ((e59 (bvsgt e19 ((_ sign_extend 14) e39))))
(let ((e60 (bvuge e39 e39)))
(let ((e61 (bvule ((_ zero_extend 6) v3) e47)))
(let ((e62 (= ((_ zero_extend 15) e48) v0)))
(let ((e63 (bvult ((_ zero_extend 9) v2) e22)))
(let ((e64 (distinct e49 v2)))
(let ((e65 (p0 ((_ zero_extend 3) e25))))
(let ((e66 (bvugt e45 ((_ zero_extend 14) e18))))
(let ((e67 (distinct e45 ((_ zero_extend 14) e33))))
(let ((e68 (bvuge ((_ zero_extend 13) e44) e47)))
(let ((e69 (bvugt e8 e8)))
(let ((e70 (bvuge e35 ((_ zero_extend 2) e30))))
(let ((e71 (distinct ((_ zero_extend 14) e8) e45)))
(let ((e72 (p0 ((_ extract 3 0) e28))))
(let ((e73 (bvsle ((_ zero_extend 8) e28) v0)))
(let ((e74 (bvsge v0 ((_ sign_extend 15) e25))))
(let ((e75 (= e28 ((_ sign_extend 6) e44))))
(let ((e76 (bvuge ((_ sign_extend 14) e25) e22)))
(let ((e77 (bvugt e48 e50)))
(let ((e78 (= ((_ zero_extend 14) e26) e11)))
(let ((e79 (bvugt e22 ((_ sign_extend 14) e31))))
(let ((e80 (bvsgt ((_ zero_extend 10) e5) e37)))
(let ((e81 (bvuge ((_ zero_extend 7) e8) v1)))
(let ((e82 (p0 ((_ extract 6 3) e20))))
(let ((e83 (bvsle ((_ sign_extend 7) e28) e42)))
(let ((e84 (bvsle ((_ sign_extend 3) e9) e42)))
(let ((e85 (bvsgt ((_ zero_extend 5) e26) e27)))
(let ((e86 (bvugt ((_ zero_extend 5) e18) v2)))
(let ((e87 (bvult e11 ((_ sign_extend 14) e25))))
(let ((e88 (bvsgt ((_ sign_extend 11) e8) e9)))
(let ((e89 (p0 ((_ extract 11 8) e29))))
(let ((e90 (bvuge e26 e25)))
(let ((e91 (bvsge e7 e32)))
(let ((e92 (bvule ((_ zero_extend 7) e20) e21)))
(let ((e93 (p0 ((_ extract 8 5) v0))))
(let ((e94 (bvslt e17 ((_ sign_extend 5) e13))))
(let ((e95 (= ((_ zero_extend 14) e13) e11)))
(let ((e96 (p0 ((_ extract 6 3) e32))))
(let ((e97 (bvsle e52 ((_ sign_extend 13) e44))))
(let ((e98 (bvuge ((_ sign_extend 7) e20) e42)))
(let ((e99 (= e29 ((_ zero_extend 15) e26))))
(let ((e100 (bvugt e16 ((_ sign_extend 14) e26))))
(let ((e101 (bvslt v0 ((_ sign_extend 1) e21))))
(let ((e102 (bvsle e37 ((_ sign_extend 15) e12))))
(let ((e103 (bvugt v0 ((_ zero_extend 1) v4))))
(let ((e104 (p0 ((_ sign_extend 3) e31))))
(let ((e105 (bvugt e8 e24)))
(let ((e106 (bvule e30 e8)))
(let ((e107 (bvsle e33 e18)))
(let ((e108 (p0 ((_ extract 6 3) e16))))
(let ((e109 (bvsge ((_ sign_extend 1) e15) e44)))
(let ((e110 (bvugt e17 ((_ zero_extend 5) e26))))
(let ((e111 (bvuge e9 ((_ sign_extend 11) e48))))
(let ((e112 (bvule ((_ sign_extend 1) e24) e44)))
(let ((e113 (p0 ((_ extract 9 6) e7))))
(let ((e114 (bvugt e22 e7)))
(let ((e115 (bvslt e10 ((_ zero_extend 14) e15))))
(let ((e116 (bvsge e34 ((_ sign_extend 14) e26))))
(let ((e117 (bvsgt e9 ((_ sign_extend 10) e44))))
(let ((e118 (bvule e23 ((_ zero_extend 13) e44))))
(let ((e119 (bvsgt e52 ((_ sign_extend 14) e13))))
(let ((e120 (distinct e16 e52)))
(let ((e121 (bvslt ((_ zero_extend 7) v1) e43)))
(let ((e122 (bvsge ((_ sign_extend 1) e31) e44)))
(let ((e123 (bvult ((_ sign_extend 8) e36) e37)))
(let ((e124 (= ((_ sign_extend 7) e36) e45)))
(let ((e125 (bvsge e37 ((_ sign_extend 1) e16))))
(let ((e126 (bvsgt ((_ sign_extend 8) e33) v3)))
(let ((e127 (bvsge ((_ zero_extend 14) e18) e7)))
(let ((e128 (bvsle e19 ((_ zero_extend 14) e13))))
(let ((e129 (bvslt ((_ sign_extend 9) e27) e32)))
(let ((e130 (p0 ((_ extract 13 10) e21))))
(let ((e131 (p0 ((_ extract 14 11) e14))))
(let ((e132 (bvule e13 e12)))
(let ((e133 (bvult e41 ((_ zero_extend 15) e51))))
(let ((e134 (bvsgt e38 ((_ zero_extend 10) v2))))
(let ((e135 (bvsle ((_ sign_extend 15) e31) e41)))
(let ((e136 (bvslt e13 e30)))
(let ((e137 (distinct ((_ zero_extend 4) e36) e9)))
(let ((e138 (bvsgt ((_ sign_extend 15) e26) e38)))
(let ((e139 (= e47 ((_ sign_extend 14) e31))))
(let ((e140 (p0 ((_ extract 3 0) v3))))
(let ((e141 (distinct ((_ sign_extend 14) e33) e42)))
(let ((e142 (bvule ((_ zero_extend 15) e33) v0)))
(let ((e143 (bvsge ((_ sign_extend 10) e46) e29)))
(let ((e144 (bvult e17 ((_ zero_extend 5) e31))))
(let ((e145 (bvuge e26 e15)))
(let ((e146 (= e49 e46)))
(let ((e147 (bvule e6 ((_ sign_extend 3) e17))))
(let ((e148 (p0 ((_ extract 4 1) e46))))
(let ((e149 (distinct ((_ sign_extend 3) e35) v2)))
(let ((e150 (bvule e23 ((_ zero_extend 6) e6))))
(let ((e151 (bvule e20 ((_ zero_extend 7) e15))))
(let ((e152 (bvsge e40 e32)))
(let ((e153 (xor e75 e137)))
(let ((e154 (not e95)))
(let ((e155 (not e118)))
(let ((e156 (ite e108 e83 e59)))
(let ((e157 (or e156 e155)))
(let ((e158 (=> e143 e114)))
(let ((e159 (xor e113 e69)))
(let ((e160 (not e124)))
(let ((e161 (or e123 e110)))
(let ((e162 (ite e159 e102 e85)))
(let ((e163 (xor e104 e144)))
(let ((e164 (not e160)))
(let ((e165 (= e122 e150)))
(let ((e166 (ite e149 e136 e153)))
(let ((e167 (not e141)))
(let ((e168 (=> e62 e74)))
(let ((e169 (xor e133 e66)))
(let ((e170 (and e148 e70)))
(let ((e171 (xor e126 e158)))
(let ((e172 (xor e93 e92)))
(let ((e173 (ite e170 e140 e77)))
(let ((e174 (or e78 e84)))
(let ((e175 (=> e129 e105)))
(let ((e176 (=> e115 e73)))
(let ((e177 (= e116 e161)))
(let ((e178 (xor e101 e63)))
(let ((e179 (ite e65 e162 e130)))
(let ((e180 (ite e107 e58 e142)))
(let ((e181 (ite e151 e88 e90)))
(let ((e182 (and e177 e111)))
(let ((e183 (not e68)))
(let ((e184 (=> e121 e56)))
(let ((e185 (= e169 e154)))
(let ((e186 (=> e96 e96)))
(let ((e187 (or e128 e54)))
(let ((e188 (or e185 e60)))
(let ((e189 (and e67 e145)))
(let ((e190 (=> e146 e55)))
(let ((e191 (not e167)))
(let ((e192 (ite e175 e172 e174)))
(let ((e193 (xor e87 e165)))
(let ((e194 (xor e166 e80)))
(let ((e195 (and e135 e186)))
(let ((e196 (= e91 e178)))
(let ((e197 (not e125)))
(let ((e198 (xor e72 e82)))
(let ((e199 (not e157)))
(let ((e200 (= e164 e64)))
(let ((e201 (and e192 e98)))
(let ((e202 (and e184 e190)))
(let ((e203 (or e103 e168)))
(let ((e204 (= e132 e106)))
(let ((e205 (=> e76 e97)))
(let ((e206 (or e109 e188)))
(let ((e207 (or e180 e134)))
(let ((e208 (and e112 e176)))
(let ((e209 (and e202 e197)))
(let ((e210 (not e196)))
(let ((e211 (ite e183 e86 e61)))
(let ((e212 (not e181)))
(let ((e213 (and e94 e194)))
(let ((e214 (ite e199 e200 e152)))
(let ((e215 (= e117 e120)))
(let ((e216 (xor e79 e171)))
(let ((e217 (or e53 e216)))
(let ((e218 (= e89 e81)))
(let ((e219 (=> e195 e99)))
(let ((e220 (= e213 e215)))
(let ((e221 (ite e203 e173 e57)))
(let ((e222 (= e219 e204)))
(let ((e223 (and e100 e191)))
(let ((e224 (and e127 e209)))
(let ((e225 (=> e139 e71)))
(let ((e226 (xor e220 e220)))
(let ((e227 (xor e222 e138)))
(let ((e228 (or e205 e206)))
(let ((e229 (xor e208 e163)))
(let ((e230 (not e201)))
(let ((e231 (xor e179 e189)))
(let ((e232 (ite e224 e212 e223)))
(let ((e233 (and e193 e221)))
(let ((e234 (ite e217 e214 e187)))
(let ((e235 (or e227 e147)))
(let ((e236 (=> e234 e119)))
(let ((e237 (or e182 e218)))
(let ((e238 (= e232 e237)))
(let ((e239 (or e230 e210)))
(let ((e240 (xor e239 e231)))
(let ((e241 (=> e236 e131)))
(let ((e242 (= e225 e229)))
(let ((e243 (xor e238 e238)))
(let ((e244 (=> e207 e226)))
(let ((e245 (ite e233 e233 e235)))
(let ((e246 (or e244 e198)))
(let ((e247 (and e243 e242)))
(let ((e248 (= e228 e240)))
(let ((e249 (ite e246 e211 e211)))
(let ((e250 (or e248 e241)))
(let ((e251 (=> e247 e245)))
(let ((e252 (and e251 e250)))
(let ((e253 (or e252 e252)))
(let ((e254 (or e249 e249)))
(let ((e255 (not e253)))
(let ((e256 (and e254 e254)))
(let ((e257 (or e255 e255)))
(let ((e258 (not e257)))
(let ((e259 (and e256 e256)))
(let ((e260 (= e258 e259)))
(let ((e261 (and e260 (not (= e15 (_ bv0 1))))))
(let ((e262 (and e261 (not (= v0 (_ bv0 16))))))
(let ((e263 (and e262 (not (= v0 (bvnot (_ bv0 16)))))))
(let ((e264 (and e263 (not (= e10 (_ bv0 15))))))
(let ((e265 (and e264 (not (= e10 (bvnot (_ bv0 15)))))))
(let ((e266 (and e265 (not (= e42 (_ bv0 15))))))
(let ((e267 (and e266 (not (= e42 (bvnot (_ bv0 15)))))))
(let ((e268 (and e267 (not (= e27 (_ bv0 6))))))
e268
)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
