(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_UF)
(declare-sort S0 0)
(declare-sort S1 0)
(declare-sort S2 0)
(declare-fun v0 () S0)
(declare-fun v1 () S0)
(declare-fun v2 () S0)
(declare-fun v3 () S1)
(declare-fun v4 () S1)
(declare-fun v5 () S1)
(declare-fun v6 () S2)
(declare-fun v7 () S2)
(declare-fun v8 () S2)
(declare-fun f0 ( S1) S1)
(declare-fun f1 ( S0 S0) S1)
(declare-fun f2 ( S0) S0)
(declare-fun f3 ( S2) S0)
(declare-fun f4 ( S1 S0 S0) S2)
(declare-fun p0 ( S0 S1) Bool)
(declare-fun p1 ( S0 S2) Bool)
(declare-fun p2 ( S0 S1 S0) Bool)
(declare-fun p3 ( S1) Bool)
(declare-fun p4 ( S1) Bool)
(assert (let ((e9 (f3 v8)))
(let ((e10 (f0 v3)))
(let ((e11 (f3 v7)))
(let ((e12 (f4 v4 v1 v2)))
(let ((e13 (f1 v1 e9)))
(let ((e14 (f0 v5)))
(let ((e15 (f2 v0)))
(let ((e16 (f3 v6)))
(let ((e17 (p0 e11 v5)))
(let ((e18 (= e12 e12)))
(let ((e19 (p3 e13)))
(let ((e20 (p1 e16 e12)))
(let ((e21 (p3 v5)))
(let ((e22 (p3 e14)))
(let ((e23 (distinct e15 v2)))
(let ((e24 (p2 e15 v3 e15)))
(let ((e25 (= v7 e12)))
(let ((e26 (= v0 v1)))
(let ((e27 (p2 e9 v5 e16)))
(let ((e28 (p1 v0 v8)))
(let ((e29 (p3 v4)))
(let ((e30 (distinct e10 v5)))
(let ((e31 (p0 e15 e10)))
(let ((e32 (p4 e13)))
(let ((e33 (= v6 e12)))
(let ((e34 (ite e20 e10 v5)))
(let ((e35 (ite e28 e34 v3)))
(let ((e36 (ite e33 v1 e9)))
(let ((e37 (ite e24 e13 v3)))
(let ((e38 (ite e18 v2 e15)))
(let ((e39 (ite e22 e35 e10)))
(let ((e40 (ite e32 e11 e16)))
(let ((e41 (ite e25 v6 e12)))
(let ((e42 (ite e28 e13 v5)))
(let ((e43 (ite e31 e14 e37)))
(let ((e44 (ite e29 e37 e13)))
(let ((e45 (ite e25 v8 v7)))
(let ((e46 (ite e23 v0 v0)))
(let ((e47 (ite e17 v4 e42)))
(let ((e48 (ite e26 e35 e42)))
(let ((e49 (ite e20 e37 v4)))
(let ((e50 (ite e27 e12 e41)))
(let ((e51 (ite e20 e41 v6)))
(let ((e52 (ite e21 v0 e36)))
(let ((e53 (ite e30 e40 e40)))
(let ((e54 (ite e19 e53 e16)))
(let ((e55 (distinct e54 e53)))
(let ((e56 (p3 e14)))
(let ((e57 (p4 e14)))
(let ((e58 (distinct e47 e42)))
(let ((e59 (= e46 v0)))
(let ((e60 (p0 v0 e14)))
(let ((e61 (= e37 e34)))
(let ((e62 (distinct e48 e14)))
(let ((e63 (p2 e53 v4 e11)))
(let ((e64 (p0 e52 e42)))
(let ((e65 (= e9 e52)))
(let ((e66 (p4 e48)))
(let ((e67 (p3 v5)))
(let ((e68 (p1 e9 e51)))
(let ((e69 (p4 e44)))
(let ((e70 (p1 e52 v7)))
(let ((e71 (= e36 e46)))
(let ((e72 (distinct e43 e39)))
(let ((e73 (p3 v5)))
(let ((e74 (p2 e9 e39 e46)))
(let ((e75 (p4 e47)))
(let ((e76 (p1 e38 e51)))
(let ((e77 (p2 e16 e14 e9)))
(let ((e78 (distinct v3 e43)))
(let ((e79 (p2 v1 v5 e54)))
(let ((e80 (p2 e36 e48 v1)))
(let ((e81 (p0 e40 e49)))
(let ((e82 (distinct e15 v1)))
(let ((e83 (p3 e10)))
(let ((e84 (p0 e11 v3)))
(let ((e85 (= v6 v7)))
(let ((e86 (distinct e50 v7)))
(let ((e87 (distinct v8 e51)))
(let ((e88 (p1 e53 e12)))
(let ((e89 (distinct e13 e13)))
(let ((e90 (= e41 e45)))
(let ((e91 (p0 e46 v4)))
(let ((e92 (p0 v0 e13)))
(let ((e93 (p4 e43)))
(let ((e94 (distinct v2 v1)))
(let ((e95 (= e35 e47)))
(let ((e96 (ite e26 e88 e89)))
(let ((e97 (=> e61 e85)))
(let ((e98 (not e31)))
(let ((e99 (xor e69 e97)))
(let ((e100 (ite e24 e95 e77)))
(let ((e101 (or e27 e18)))
(let ((e102 (ite e82 e66 e28)))
(let ((e103 (not e55)))
(let ((e104 (= e56 e78)))
(let ((e105 (xor e98 e57)))
(let ((e106 (not e70)))
(let ((e107 (ite e76 e83 e100)))
(let ((e108 (=> e29 e90)))
(let ((e109 (=> e22 e20)))
(let ((e110 (=> e58 e84)))
(let ((e111 (and e107 e17)))
(let ((e112 (ite e104 e86 e59)))
(let ((e113 (not e99)))
(let ((e114 (= e92 e102)))
(let ((e115 (not e81)))
(let ((e116 (ite e101 e113 e60)))
(let ((e117 (or e32 e80)))
(let ((e118 (ite e72 e112 e115)))
(let ((e119 (=> e71 e108)))
(let ((e120 (and e118 e74)))
(let ((e121 (= e106 e91)))
(let ((e122 (=> e114 e87)))
(let ((e123 (not e25)))
(let ((e124 (not e96)))
(let ((e125 (ite e67 e117 e121)))
(let ((e126 (not e124)))
(let ((e127 (xor e19 e120)))
(let ((e128 (and e109 e63)))
(let ((e129 (ite e23 e30 e122)))
(let ((e130 (or e94 e68)))
(let ((e131 (= e129 e105)))
(let ((e132 (or e127 e126)))
(let ((e133 (or e123 e125)))
(let ((e134 (not e116)))
(let ((e135 (= e64 e134)))
(let ((e136 (xor e103 e110)))
(let ((e137 (ite e131 e65 e128)))
(let ((e138 (xor e73 e137)))
(let ((e139 (ite e62 e75 e75)))
(let ((e140 (or e139 e135)))
(let ((e141 (or e79 e132)))
(let ((e142 (and e138 e138)))
(let ((e143 (and e93 e111)))
(let ((e144 (and e136 e133)))
(let ((e145 (xor e119 e144)))
(let ((e146 (=> e130 e130)))
(let ((e147 (xor e143 e145)))
(let ((e148 (ite e140 e147 e141)))
(let ((e149 (=> e21 e148)))
(let ((e150 (or e149 e142)))
(let ((e151 (and e150 e150)))
(let ((e152 (not e151)))
(let ((e153 (not e146)))
(let ((e154 (and e33 e33)))
(let ((e155 (or e153 e154)))
(let ((e156 (and e155 e152)))
e156
)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
