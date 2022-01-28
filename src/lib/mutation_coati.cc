/*
# Copyright (c) 2020-2021 Juan J. Garcia Mesa <juanjosegarciamesa@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
*/

#include <doctest/doctest.h>

#include <coati/mutation_coati.hpp>

/* nonsynonymous-synonymous bias (\omega) */
// const float omega = 0.2;  // github.com/reedacartwright/toycoati

namespace coati {
/**
 * \brief Create Muse \& Gaut (1994) substitution matrix.
 *
 * Given a branch length, create a 64x64 codon substitution P matrix based on
 *  Muse \& Gaut model. Using nucleotide substitution rates from Yang (1994).
 *
 * @param[in] br_len float branch length.
 * @param[in] omega float nonsynonymous-synonymous bias.
 * @param[in] nuc_freqs std::vector<coati::float_t> nucleotide frequencies
 *  (A,C,G,T).
 *
 * \return substitution P matrix (coati::Matrixf).
 */
coati::Matrixf mg94_p(float br_len, float omega,
                      const std::vector<coati::float_t>& nuc_freqs,
                      const std::vector<coati::float_t>& sigma) {
    if(br_len <= 0) {
        throw std::out_of_range("Branch length must be positive.");
    }

    coati::Matrixf nuc_q(4, 4);

    if(std::any_of(sigma.cbegin(), sigma.cend(),
                   [](coati::float_t f) { return f > 0.f; })) {
        // Use GTR model for nuc_q
        nuc_q = gtr_q(nuc_freqs, sigma);
    } else {
        // Use Yang (1994) estimating the pattern of nucleotide substitution
        nuc_q = {{-0.818, 0.132, 0.586, 0.1},
                 {0.221, -1.349, 0.231, 0.897},
                 {0.909, 0.215, -1.322, 0.198},
                 {0.1, 0.537, 0.128, -0.765}};
    }

    // MG94 model - doi:10.1534/genetics.108.092254
    Matrix64f Q = Matrix64f::Zero();
    float Pi[64];
    float w{NAN}, d = 0.0f;
    int x = 0, y = 0;
    uint8_t first_cod_mask = 48, second_cod_mask = 12, third_cod_mask = 3;

    // construct transition matrix
    for(uint8_t i = 0; i < 64; i++) {
        // uint8_t codon;
        // (codon & 48) >> 4 = nt16_table encoding of first codon nucleotide
        // (codon & 12) >> 2 = nt16_table encoding of second codon nucleotide
        // (codon & 03) = nt16_table encoding of third codon nucleotide
        // e.g. 00 00 11 10 = 00 A T G = codon "ATG"
        // (00001110 & 48) >> 4 = (00001110 & 00110000) >> 4 = 00000000 >> 4 = 0
        // (A) (00001110 & 12) >> 2 = (00001110 & 00001100) >> 2 = 00001100 >> 2
        // = 3 (T)
        // (00001110 & 03) 		= (00001110 & 00000011) 	 =
        // 00000010 = 2 (G)

        Pi[i] = nuc_freqs[((i & 48) >> 4)] * nuc_freqs[((i & 12) >> 2)] *
                nuc_freqs[(i & 3)];
        float rowSum = 0.0;
        for(uint8_t j = 0; j < 64; j++) {
            if(i == j) {
                Q(i, j) = 0;
            } else if(coati::utils::cod_distance(i, j) > 1) {
                Q(i, j) = 0;
            } else {
                w = ((amino_group_table[i] == amino_group_table[j]) ? 1
                                                                    : omega);

                // split into cases to avoid use of pow (speed-up)
                if((i & first_cod_mask) != (j & first_cod_mask)) {
                    x = (i & first_cod_mask) >> 4;
                    y = (j & first_cod_mask) >> 4;
                } else if((i & second_cod_mask) != (j & second_cod_mask)) {
                    x = (i & second_cod_mask) >> 2;
                    y = (j & second_cod_mask) >> 2;
                } else if((i & third_cod_mask) != (j & third_cod_mask)) {
                    x = i & third_cod_mask;
                    y = j & third_cod_mask;
                }

                Q(i, j) = w * nuc_q(x, y);
            }
            rowSum += Q(i, j);
        }
        Q(i, i) = -rowSum;
        d += Pi[i] * rowSum;
    }

    // normalize
    Q = Q / d;

    Q = Q * br_len;
    Q = Q.exp();

    coati::Matrixf P(64, 64, Q);

    return P;
}

/// @private
TEST_CASE("mg94_p") {
    coati::Matrixf P(mg94_p(0.0133, 0.2, {0.308, 0.185, 0.199, 0.308}));

    for(int i = 0; i < 64; i++) {
        for(int j = 0; j < 64; j++) {
            CHECK(P(i, j) == doctest::Approx(mg94P[i][j]));
        }
    }
}

/**
 * \brief Create marginal 192x4 substitution P matrix give a 64x64
 *  substitution matrix.
 *
 * @param[in] P coati::Matrixf 64x64 codon substitution matrix.
 * @param[in] pi std::vector<coati::float_t> nucleotide frequencies (A,C,G,T).
 *
 * \return marginal 192x4 substitution matrix (coati::Matrixf).
 */
coati::Matrixf marginal_p(const coati::Matrixf& P,
                          const std::vector<coati::float_t>& pi) {
    // const coati::utils::AmbiguousNucs amb) {
    float marg{NAN};

    coati::Matrixf p(192, 5);
    // coati::Matrixf p(192, 16);

    for(int cod = 0; cod < 64; cod++) {
        for(int pos = 0; pos < 3; pos++) {
            for(int nuc = 0; nuc < 4; nuc++) {  // A,C,G,T
                marg = 0.0;
                switch(pos) {  // divide cases into each value of pos for speed
                               // up (reduce use of pow())
                case 0:
                    for(uint8_t i = 0; i < 64; i++) {
                        marg += (((i & 48) >> 4) == nuc ? P(cod, i) : 0.0f);
                    }
                    break;
                case 1:
                    for(uint8_t i = 0; i < 64; i++) {
                        marg += (((i & 12) >> 2) == nuc ? P(cod, i) : 0.0f);
                    }
                    break;
                case 2:
                    for(uint8_t i = 0; i < 64; i++) {
                        marg += ((i & 3) == nuc ? P(cod, i) : 0.0f);
                    }
                    break;
                }
                p(cod * 3 + pos, nuc) = ::logf(marg / pi[nuc]);
            }
            // switch(pos) {  // ambiguous nucleotides
            // }
        }
    }

    return p;
}

/// @private
TEST_CASE("marginal_p") {
    std::vector<coati::float_t> pi{0.308, 0.185, 0.199, 0.308};
    coati::Matrixf P = mg94_p(0.0133, 0.2, pi);
    coati::Matrixf p_marg = marginal_p(P, pi);

    for(int cod = 0; cod < 64; cod++) {
        for(int pos = 0; pos < 3; pos++) {
            float val = 0.f;
            for(int nuc = 0; nuc < 4; nuc++) {
                val += ::expf(p_marg(cod * 3 + pos, nuc)) * pi[nuc];
            }
            CHECK(val == doctest::Approx(1));  // sum per pos (all nuc) is 1
        }
    }
}

/**
 * \brief Create GTR subsitution model matrix.
 *
 * @param[in] pi std::vector<coati::float_t> nucleotide frequencies.
 * @param[in] sigma std::vector<coati::float_t> sigma parameters (6) for GTR
 * model.
 *
 * \return GTR Q matrix
 */
coati::Matrixf gtr_q(const std::vector<coati::float_t>& pi,
                     const std::vector<coati::float_t>& sigma) {
    //   |        A      |       C       |       G       |       T       |
    // A |        -      | pi_C*sigma_AC | pi_G*sigma_AG | pi_T*sigma_AT |
    // C | pi_A*sigma_AC |        -      | pi_G*sigma_CG | pi_T*sigma_CT |
    // G | pi_A*sigma_AG | pi_C*sigma_GC |       -       | pi_T*sigma_GT |
    // T | pi_A*sigma_AT | pi_C*sigma_CT | pi_G*sigma_GT |       -       |

    if(std::any_of(sigma.cbegin(), sigma.cend(),
                   [](coati::float_t f) { return f < 0.f || f > 1.f; })) {
        throw std::invalid_argument("Sigma values must be in range [0,1].");
    }

    coati::Matrixf gtr_mat(4, 4);

    // set sigmas
    gtr_mat(0, 1) = gtr_mat(1, 0) = sigma[0];  // sigma_AC
    gtr_mat(0, 2) = gtr_mat(2, 0) = sigma[1];  // sigma_AG
    gtr_mat(0, 3) = gtr_mat(3, 0) = sigma[2];  // sigma_AT
    gtr_mat(1, 2) = gtr_mat(2, 1) = sigma[3];  // sigma_GC
    gtr_mat(1, 3) = gtr_mat(3, 1) = sigma[4];  // sigma_CT
    gtr_mat(2, 3) = gtr_mat(3, 2) = sigma[5];  // sigma_GT

    // multiply by corresponding pi
    for(size_t i = 0; i < 4; i++) {
        for(size_t j = 0; j < 4; j++) {
            gtr_mat(i, j) *= pi[j];
        }
    }

    // set major diagonal
    gtr_mat(0, 0) = -(gtr_mat(0, 1) + gtr_mat(0, 2) + gtr_mat(0, 3));
    gtr_mat(1, 1) = -(gtr_mat(1, 0) + gtr_mat(1, 2) + gtr_mat(1, 3));
    gtr_mat(2, 2) = -(gtr_mat(2, 0) + gtr_mat(2, 1) + gtr_mat(2, 3));
    gtr_mat(3, 3) = -(gtr_mat(3, 0) + gtr_mat(3, 1) + gtr_mat(3, 2));

    return gtr_mat;
}

/// @private
TEST_CASE("gtr_q") {
    coati::Matrixf gtr(gtr_q({0.308, 0.185, 0.199, 0.308},
                             {0.009489730, 0.039164824, 0.004318182,
                              0.015438693, 0.038734091, 0.008550000}));

    coati::Matrixf gtr_correct = {
        {-0.010879400, 0.001755600, 0.00779380, 0.00133000},
        {0.002922837, -0.017925237, 0.00307230, 0.01193010},
        {0.012062766, 0.002856158, -0.01755232, 0.00263340},
        {0.001330000, 0.007165807, 0.00170145, -0.01019726}};

    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 4; j++) {
            CHECK(gtr(i, j) == doctest::Approx(gtr_correct(i, j)));
        }
    }

    SUBCASE("Sigma values out of range") {
        REQUIRE_THROWS_AS(gtr_q({0.308, 0.185, 0.199, 0.308},
                                {-0.009489730, 0.039164824, 0.004318182,
                                 0.015438693, 0.038734091, 0.008550000}),
                          std::invalid_argument);

        REQUIRE_THROWS_AS(gtr_q({0.308, 0.185, 0.199, 0.308},
                                {0.009489730, 0.039164824, 0.004318182,
                                 0.015438693, 1.038734091, 0.008550000}),
                          std::invalid_argument);
    }
}
}  // namespace coati
