/*
# Copyright (c) 2021-2022 Juan J. Garcia Mesa <juanjosegarciamesa@gmail.com>
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

#include <coati/align_marginal.hpp>
#include <filesystem>

namespace coati {

/**
 * @brief Pairwise alignment using dynamic programming and a marginal model.
 *
 * Alignment of two sequences via dynamic programming using an affine
 *  (geometrical) gap model and a marginal codon substitution model.
 *
 * @param[in,out] aln coati::alignment_t alignment information.
 *
 * @retval true successful run.
 */
bool marg_alignment(coati::alignment_t& aln) {
    coati::Matrixf P(64, 64), p_marg;
    std::ofstream out_w;

    // read input data
    aln.data = coati::io::read_input(aln);
    if(aln.data.size() != 2) {
        throw std::invalid_argument("Exactly two sequences required.");
    }

    // set substitution matrix according to model
    coati::utils::set_subst(aln);

    // set reference sequence as first sequence (in aln.data)
    if(!aln.refs.empty() || aln.rev) {
        order_ref(aln);
    }

    // if -s or --score, score alignment and exit
    if(aln.score) {
        std::cout << alignment_score(aln, aln.subst_matrix) << std::endl;
        return true;
    }

    // check that length of ref sequence is multiple of 3 and gap unit length
    size_t len_a = aln.seq(0).length();
    if(len_a % 3 != 0 || len_a % aln.gap.len != 0) {
        throw std::invalid_argument(
            "Length of reference sequence must be multiple of 3.");
    }
    // check that length of descendant sequence is multiple of gap unit length
    if(aln.seq(1).length() % aln.gap.len != 0) {
        throw std::invalid_argument(
            "Length of descendant sequence must be multiple of " +
            std::to_string(aln.gap.len) + ".");
    }

    // encode sequences
    auto anc = aln.seq(0);
    auto des = aln.seq(1);
    coati::utils::sequence_pair_t seq_pair =
        coati::utils::marginal_seq_encoding(anc, des);

    // dynamic programming pairwise alignment and traceback
    coati::align_pair_work_mem_t work;
    try {
        coati::viterbi_mem(work, seq_pair[0], seq_pair[1], aln);
    } catch(const std::bad_alloc& e) {
        std::cerr << "ERROR: sequences to align exceed available memory."
                  << std::endl;
        return EXIT_FAILURE;
    } catch(const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return false;
    }
    coati::traceback(work, anc, des, aln, aln.gap.len);

    if(!aln.weight_file.empty()) {  // save weight and filename
        out_w.open(aln.weight_file, std::ios::app | std::ios::out);
        out_w << aln.data.path.string() << "," << aln.model << ","
              << aln.data.weight << std::endl;
        out_w.close();
    }

    // write alignment
    coati::io::write_output(aln.data);
    return true;
}

/**
 * @brief Reorder pair of input sequences so that reference is at position zero.
 *
 * @param[in,out] aln coati::alignment_t alignment data.
 */
void order_ref(coati::alignment_t& aln) {
    if(aln.data.names[0] == aln.refs) {
        // already the first sequence: do nothing
    } else if(aln.data.names[1] == aln.refs) {  // swap sequences
        std::swap(aln.data.names[0], aln.data.names[1]);
        std::swap(aln.data.seqs[0], aln.data.seqs[1]);
    } else if(aln.rev) {  // swap sequences
        std::swap(aln.data.names[0], aln.data.names[1]);
        std::swap(aln.data.seqs[0], aln.data.seqs[1]);
    } else {  // aln.refs was specified and doesn't match any seq names
        throw std::invalid_argument("Name of reference sequence not found.");
    }
}

/// @private
// GCOVR_EXCL_START
TEST_CASE("marg_alignment") {
    // NOLINTNEXTLINE(misc-unused-parameters)
    auto test_fasta = [](alignment_t& aln, const data_t& expected,
                         const std::string& file) {
        std::ofstream out;
        out.open(aln.data.path);
        REQUIRE(out);
        out << file;
        out.close();

        REQUIRE(marg_alignment(aln));

        std::ifstream infile(aln.output);
        std::string s1, s2;

        infile >> s1 >> s2;
        CHECK_EQ(s1, expected.names[0]);
        CHECK_EQ(s2, expected.seqs[0]);

        infile >> s1 >> s2;
        CHECK_EQ(s1, expected.names[1]);
        CHECK_EQ(s2, expected.seqs[1]);
        REQUIRE(std::filesystem::remove(aln.output));
        REQUIRE(std::filesystem::remove("test-marg.fasta"));

        if(!aln.weight_file.empty()) {
            std::ifstream inweight(aln.weight_file);
            std::string s;  // NOLINT(clang-diagnostic-unused-variable)
            inweight >> s;
            // NOLINTNEXTLINE(clang-diagnostic-unused-variable)
            std::size_t start = s.find_last_of(',');
            CHECK_EQ(std::stof(s.substr(start + 1)), expected.weight);
            CHECK_EQ(s.substr(0, start), "test-marg.fasta,marginal");
            REQUIRE(std::filesystem::remove(aln.weight_file));
        }
    };

    // NOLINTNEXTLINE(misc-unused-parameters)
    auto test_phylip = [](alignment_t& aln, const data_t& expected,
                          const std::string& file) {
        std::ofstream out;
        out.open(aln.data.path);
        REQUIRE(out);
        out << file;
        out.close();

        REQUIRE(marg_alignment(aln));

        std::ifstream infile(aln.output);
        std::string s1, s2;

        infile >> s1 >> s2;
        CHECK_EQ(s1, "2");
        CHECK_EQ(std::stoul(s2), aln.seq(0).size());

        infile >> s1 >> s2;
        CHECK_EQ(s1, expected.names[0]);
        CHECK_EQ(s2, expected.seqs[0]);

        infile >> s1 >> s2;
        CHECK_EQ(s1, expected.names[1]);
        CHECK_EQ(s2, expected.seqs[1]);
        REQUIRE(std::filesystem::remove(aln.output));
        REQUIRE(std::filesystem::remove("test-marg.fasta"));
    };

    alignment_t aln;
    data_t expected;
    SUBCASE("Alignment - output fasta") {
        std::string file{">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.weight_file = "score.log";
        aln.output = "test-marg_alignment-fasta.fasta";

        expected = data_t("", {">1", ">2"}, {"CTCTGGATAGTG", "CT----ATAGTG"});
        expected.weight = 1.51294f;

        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment - output fasta - use aln.refs") {
        std::string file{">1\nCTATAGTG\n>2\nCTCTGGATAGTG\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-fasta.fasta";
        aln.refs = "2";

        expected = data_t("", {">2", ">1"}, {"CTCTGGATAGTG", "CT----ATAGTG"});

        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment - output fasta - use aln.refs - no change") {
        std::string file{">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-fasta.fasta";
        aln.refs = "1";

        expected = data_t("", {">1", ">2"}, {"CTCTGGATAGTG", "CT----ATAGTG"});

        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment - output phylip") {
        std::string file{">1\nGCGACTGTT\n>2\nGCGATTGCTGTT\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-phylip.phy";

        expected = data_t("", {"1", "2"}, {"GCGA---CTGTT", "GCGATTGCTGTT"});

        test_phylip(aln, expected, file);
    }
    SUBCASE("Alignment - output phylip - use aln.refn") {
        std::string file{">A\nGCGATTGCTGTT\n>B\nGCGACTGTT\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-phylip.phy";
        aln.rev = true;

        expected = data_t("", {"B", "A"}, {"GCGA---CTGTT", "GCGATTGCTGTT"});

        test_phylip(aln, expected, file);
    }
    SUBCASE("Alignment 2 dels - output phylip") {
        std::string file{">1\nACGTTAAGGGGT\n>2\nACGAAT\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-phylip2.phy";

        expected = data_t("", {"1", "2"}, {"ACGTTAAGGGGT", "ACG--AA----T"});

        test_phylip(aln, expected, file);
    }
    SUBCASE("Alignment with gap length multiple of 3") {
        std::string file{">1\nACGTTAAGGGGT\n>2\nACGAAT\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.output = "test-marg_alignment-no-frameshifts.fa";
        aln.gap.len = 3;

        expected =
            data_t("", {">1", ">2"}, {"ACG---TTAAGGGGT", "ACGAAT---------"});

        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment with ambiguous nucleotides - AVG") {
        std::string file{">1\nCTCTGGATAGTG\n>2\nCTATAGTR\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.weight_file = "score.log";
        aln.output = "test-marg_alignment_ambiguous.fa";

        expected = data_t("", {">1", ">2"}, {"CTCTGGATAGTG", "CT----ATAGTR"});
        expected.weight = -1.03892f;  // AVG
        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment with ambiguous nucleotides - BEST") {
        std::string file{">1\nCTCTGGATAGTG\n>2\nCTATAGTR\n"};
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.weight_file = "score.log";
        aln.output = "test-marg_alignment_ambiguous.fa";
        aln.amb = coati::AmbiguousNucs::BEST;

        expected = data_t("", {">1", ">2"}, {"CTCTGGATAGTG", "CT----ATAGTR"});
        expected.weight = 1.51294f;  // BEST
        test_fasta(aln, expected, file);
    }
    SUBCASE("Alignment with gap length multiple of 3 - fail") {
        std::ofstream out;
        out.open("test-marg.fasta");
        out << ">1\nGCGATTGCTGT\n>2\nGCGACTGTT\n";
        out.close();
        aln.model = "marginal";
        aln.data.path = "test-marg.fasta";
        aln.output = "test-marg_alignment-no-frameshifts-f.fasta";
        aln.gap.len = 3;

        REQUIRE_THROWS_AS(marg_alignment(aln), std::invalid_argument);
        CHECK(std::filesystem::remove("test-marg.fasta"));
    }
    SUBCASE("Length of descendant not multiple of gap.len") {
        std::ofstream out;
        out.open("test-marg.fasta");
        out << ">A\nCTCGGA\n>B\nCTCGG\n";
        out.close();
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.gap.len = 3;
        REQUIRE_THROWS_AS(marg_alignment(aln), std::invalid_argument);
        CHECK(std::filesystem::remove("test-marg.fasta"));
    }
    SUBCASE("Score alignment") {
        std::ofstream out;
        out.open("test-marg.fasta");
        out << ">1\nCTCTGGATAGTG\n>2\nCT----ATAGTG\n";
        out.close();
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.weight_file = "";
        aln.output = "test-marg_alignment-score.fasta";
        aln.score = true;

        REQUIRE(marg_alignment(aln));
        CHECK(std::filesystem::remove("test-marg.fasta"));
    }
    SUBCASE("Score alignment - fail") {
        std::ofstream out;
        out.open("test-marg.fasta");
        out << ">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n";
        out.close();
        aln.data.path = "test-marg.fasta";
        aln.model = "marginal";
        aln.weight_file = "";
        aln.output = "test-marg_alignment-score-f.fasta";
        aln.score = true;

        coati::data_t result(aln.output);

        REQUIRE_THROWS_AS(marg_alignment(aln), std::invalid_argument);
        CHECK(std::filesystem::remove("test-marg.fasta"));
    }
    SUBCASE("Name of reference not found") {
        std::ofstream out;
        out.open("test-marg.fasta");
        out << ">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n";
        out.close();
        aln.data.path = "test-marg.fasta";
        aln.refs = "seq_name";

        REQUIRE_THROWS_AS(marg_alignment(aln), std::invalid_argument);
        CHECK(std::filesystem::remove("test-marg.fasta"));
    }
    SUBCASE("User-provided codon substitution matrix") {
        std::ofstream outfile;
        coati::Matrix<coati::float_t> P(
            mg94_p(0.0133, 0.2, {0.308, 0.185, 0.199, 0.308}));

        const std::vector<std::string> codons = {
            "AAA", "AAC", "AAG", "AAT", "ACA", "ACC", "ACG", "ACT",
            "AGA", "AGC", "AGG", "AGT", "ATA", "ATC", "ATG", "ATT",
            "CAA", "CAC", "CAG", "CAT", "CCA", "CCC", "CCG", "CCT",
            "CGA", "CGC", "CGG", "CGT", "CTA", "CTC", "CTG", "CTT",
            "GAA", "GAC", "GAG", "GAT", "GCA", "GCC", "GCG", "GCT",
            "GGA", "GGC", "GGG", "GGT", "GTA", "GTC", "GTG", "GTT",
            "TAA", "TAC", "TAG", "TAT", "TCA", "TCC", "TCG", "TCT",
            "TGA", "TGC", "TGG", "TGT", "TTA", "TTC", "TTG", "TTT"};

        outfile.open("test-marg-matrix.csv");
        REQUIRE(outfile);

        float Q[4096]{0.0f};
        for(auto i = 0; i < 640; i++) {
            Q[mg94_indexes[i]] = mg94Q[i];
        }

        outfile << "0.0133" << std::endl;  // branch length
        for(auto i = 0; i < 64; i++) {
            for(auto j = 0; j < 64; j++) {
                outfile << codons[i] << "," << codons[j] << "," << Q[i * 64 + j]
                        << std::endl;
            }
        }

        outfile.close();
        std::string file{">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n"};
        aln.data.path = "test-marg.fasta";
        aln.rate = "test-marg-matrix.csv";
        aln.output = "test-marg_alignment-fasta.fasta";

        expected = data_t("", {">1", ">2"}, {"CTCTGGATAGTG", "CT----ATAGTG"});

        test_fasta(aln, expected, file);
        REQUIRE(std::filesystem::remove("test-marg-matrix.csv"));
    }
    SUBCASE("Numb of sequences != 2 - fail") {
        aln.data.path = "test-marg.fasta";
        std::ofstream out;
        out.open(aln.data.path);
        REQUIRE(out);
        out << ">1\nCTCTGGATAGTG\n";
        out.close();
        CHECK_THROWS_AS(marg_alignment(aln), std::invalid_argument);

        aln.data.path = "test-marg.fasta";
        out.open("test-marg.fasta");
        REQUIRE(out);
        out << ">1\nCTCTGGATAGTG\n>2\nCTATAGTG\n>3\nCTCTGGGTG\n";
        out.close();
        CHECK_THROWS_AS(marg_alignment(aln), std::invalid_argument);
        REQUIRE(std::filesystem::remove("test-marg.fasta"));
    }
}
// GCOVR_EXCL_STOP

/**
 * @brief Score alignment using marginal model.
 *
 * @param[in] aln coati::alignment_t input parameters.
 * @param[in] p_marg coati::Matrixf substitution matrix.
 *
 * @retval float alignment score.
 */
float alignment_score(const coati::alignment_t& aln,
                      const coati::Matrixf& p_marg) {
    std::vector<std::string> seqs = aln.data.seqs;

    // check that both sequences have equal length
    if(seqs[0].length() != seqs[1].length()) {
        throw std::invalid_argument(
            "For alignment scoring both sequences must have equal length.");
    }

    // encode desc and gap-less ref sequences for subsitution matrix access
    std::string anc{seqs[0]};
    boost::erase_all(anc, "-");
    coati::utils::sequence_pair_t seq_pair =
        coati::utils::marginal_seq_encoding(anc, seqs[1]);

    coati::semiring::tropical tropical_rig;
    // calculate log(1-g) log(1-e) log(g) log(e) log(pi)
    float_t no_gap = tropical_rig.from_linear_1mf(aln.gap.open);
    float_t gap_stop = tropical_rig.from_linear_1mf(aln.gap.extend);
    float_t gap_open = tropical_rig.from_linearf(aln.gap.open);
    float_t gap_extend = tropical_rig.from_linearf(aln.gap.extend);
    std::vector<coati::float_t> pi{aln.pi};
    std::transform(pi.cbegin(), pi.cend(), pi.begin(),
                   [](auto value) { return ::logf(value); });

    float weight{0.f};
    int state{0}, ngap{0};
    for(size_t i = 0; i < seqs[0].length(); i++) {
        switch(state) {
        case 0:  // subsitution
            if(seqs[0][i] == '-') {
                // insertion;
                weight += gap_open;
                state = 2;
                ngap++;
            } else if(seqs[1][i] == '-') {
                // deletion;
                weight += no_gap + gap_open;
                state = 1;
            } else {
                // match/mismatch;
                weight +=
                    2 * no_gap + p_marg(seq_pair[0][i - ngap], seq_pair[1][i]);
            }
            break;
        case 1:  // deletion
            if(seqs[0][i] == '-') {
                throw std::runtime_error(
                    "Insertion after deletion is not modeled.");
            } else if(seqs[1][i] == '-') {
                // deletion_ext
                weight += gap_extend;
            } else {
                // match/mismatch
                weight +=
                    gap_stop + p_marg(seq_pair[0][i - ngap], seq_pair[1][i]);
                state = 0;
            }
            break;
        case 2:  // insertion
            if(seqs[0][i] == '-') {
                // insertion_ext
                weight += gap_extend;
                ngap++;
            } else if(seqs[1][i] == '-') {
                // deletion
                weight += gap_stop + gap_open;
                state = 1;
            } else {
                // match/mismatch
                weight += gap_stop + no_gap +
                          p_marg(seq_pair[0][i - ngap], seq_pair[1][i]);
                state = 0;
            }
            break;
        }
    }
    // terminal state weight
    if(state == 0) {
        weight += no_gap;
    } else if(state == 2) {
        weight += gap_stop;
    }
    return weight;
}

/// @private
// GCOVR_EXCL_START
TEST_CASE("alignment_score") {
    coati::alignment_t aln;
    coati::Matrixf P(mg94_p(0.0133, 0.2, {0.308, 0.185, 0.199, 0.308}));
    coati::Matrixf p_marg = marginal_p(P, aln.pi, AmbiguousNucs::AVG);

    aln.data.seqs = {"CTCTGGATAGTG", "CT----ATAGTG"};
    CHECK_EQ(alignment_score(aln, p_marg), doctest::Approx(1.51294f));

    aln.data.seqs = {"CTCT--AT", "CTCTGGAT"};
    CHECK_EQ(alignment_score(aln, p_marg), doctest::Approx(-0.835939f));

    aln.data.seqs = {"ACTCT-A", "ACTCTG-"};
    CHECK_EQ(alignment_score(aln, p_marg), doctest::Approx(-8.73357f));

    aln.data.seqs = {"ACTCTA-", "ACTCTAG"};
    CHECK_EQ(alignment_score(aln, p_marg), doctest::Approx(-0.658564f));
    // different length
    aln.data.seqs = {"CTC", "CT"};
    REQUIRE_THROWS_AS(alignment_score(aln, p_marg), std::invalid_argument);
    // insertion after deletion is not modeled
    aln.data.seqs = {"ATAC-GGGTC", "ATA-GGGGTC"};
    REQUIRE_THROWS_AS(alignment_score(aln, p_marg), std::runtime_error);
}
// GCOVR_EXCL_STOP

/**
 * @brief Sample from a marginal alignment.
 *
 * @param[in,out] aln coati::alignment_t alignment data.
 * @param[in] sample_size size_t number of alignments to sample.
 * @param[in] rand coati::random_t random seed generator object.
 *
 */
void marg_sample(coati::alignment_t& aln, size_t sample_size, random_t& rand) {
    coati::Matrixf P(64, 64), p_marg;

    // read input data
    aln.data = coati::io::read_input(aln);
    if(aln.data.size() != 2) {
        throw std::invalid_argument("Exactly two sequences required.");
    }

    // set output pointer
    std::ostream* pout(nullptr);
    std::ofstream outfile;
    if(aln.data.out_file.path.empty() || aln.data.out_file.path == "-") {
        pout = &std::cout;
    } else {
        outfile.open(aln.data.out_file.path);
        if(!outfile) {
            throw std::invalid_argument("Opening output file " +
                                        aln.data.out_file.path + " failed.");
        }
        pout = &outfile;
    }
    std::ostream& out = *pout;

    // check that length of ref sequence is multiple of 3 and gap unit size
    size_t len_a = aln.seq(0).length();
    if(len_a % 3 != 0 || len_a % aln.gap.len != 0) {
        throw std::invalid_argument(
            "Length of reference sequence must be multiple of 3.");
    }
    // check that length of descendant sequence is multiple of gap unit size
    if(aln.seq(1).length() % aln.gap.len != 0) {
        throw std::invalid_argument(
            "Length of descendant sequence must be multiple of " +
            std::to_string(aln.gap.len) + ".");
    }

    // encode sequences
    auto anc = aln.seq(0);
    auto des = aln.seq(1);
    coati::utils::sequence_pair_t seq_pair =
        coati::utils::marginal_seq_encoding(anc, des);

    // set substitution matrix according to model
    coati::utils::set_subst(aln);

    // dynamic programming pairwise alignment and traceback
    coati::align_pair_work_t work;
    coati::viterbi(work, seq_pair[0], seq_pair[1], aln);

    out << "[" << std::endl;
    // sample and print as many aligments as required (sample_size)
    for(size_t i = 0; i < sample_size; ++i) {
        coati::sampleback(work, anc, des, aln, aln.gap.len, rand);

        out << "  {\n    \"aln\": {\n";
        out << "      \"" << aln.name(0) << "\": ";
        out << "\"" << aln.seq(0) << "\",\n";
        out << "      \"" << aln.name(1) << "\": ";
        out << "\"" << aln.seq(1) << "\"\n";
        out << "    },\n";
        out << "    \"weight\": " << ::expf(aln.data.weight) << ",\n";
        out << "    \"log_weight\": " << aln.data.weight << "\n";
        if(i < sample_size - 1) {
            out << "  },";
        } else {
            out << "  }";
        }
        out << std::endl;
    }

    out << "]" << std::endl;
}

/// @private
// GCOVR_EXCL_START
TEST_CASE("marg_sample") {
    // test helper function
    // NOLINTNEXTLINE(misc-unused-parameters)
    auto check_line_eq = [](std::ifstream& in, const std::string_view line) {
        std::string s;
        std::getline(in, s);
        CHECK_EQ(s, line);
    };

    auto test = [check_line_eq](const std::string& seq1,
                                const std::string& seq2,
                                const std::vector<std::string>& expected_s1,
                                const std::vector<std::string>& expected_s2,
                                const std::vector<std::string>& weight,
                                const std::vector<std::string>& lweight) {
        // set seed
        coati::random_t rand;
        const std::vector<std::string> s42{{"42"}};
        auto seed = fragmites::random::string_seed_seq(s42.begin(), s42.end());
        rand.Seed(seed);

        coati::alignment_t aln;
        aln.data.path = "test-marg.fasta";
        aln.output = "test-marg_sample.json";
        std::ofstream out;
        out.open(aln.data.path);
        REQUIRE(out);
        out << ">A\n" << seq1 << "\n>B\n" << seq2 << std::endl;
        out.close();

        size_t reps{expected_s1.size()};
        coati::marg_sample(aln, reps, rand);

        std::ifstream infile(aln.output);
        REQUIRE(infile.good());

        check_line_eq(infile, "[");
        for(size_t i = 0; i < reps; ++i) {
            check_line_eq(infile, "  {");
            check_line_eq(infile, "    \"aln\": {");
            check_line_eq(infile, R"(      "A": ")" + expected_s1[i] + "\",");
            check_line_eq(infile, R"(      "B": ")" + expected_s2[i] + "\"");
            check_line_eq(infile, "    },");
            check_line_eq(infile, "    \"weight\": " + weight[i] + ",");
            check_line_eq(infile, "    \"log_weight\": " + lweight[i]);
            if(i < reps - 1) {
                check_line_eq(infile, "  },");
            } else {
                check_line_eq(infile, "  }");
            }
        }
        check_line_eq(infile, "]");
        infile.close();
        REQUIRE(std::filesystem::remove(aln.output));
        REQUIRE(std::filesystem::remove("test-marg.fasta"));
    };

    SUBCASE("sample size 1") {
        std::vector<std::string> seq1{"CC--CCCC"};
        std::vector<std::string> seq2{"CCCCCCCC"};
        std::vector<std::string> weight{"0.031239"};
        std::vector<std::string> lweight{"-3.46609"};
        test("CCCCCC", "CCCCCCCC", seq1, seq2, weight, lweight);
    }
    SUBCASE("sample size 1 - deletion") {
        std::vector<std::string> seq1{"CCCCCC"};
        std::vector<std::string> seq2{"CCCC--"};
        std::vector<std::string> weight{"0.856821"};
        std::vector<std::string> lweight{"-0.154526"};
        test("CCCCCC", "CCCC", seq1, seq2, weight, lweight);
    }
    SUBCASE("sample size 3") {
        std::vector<std::string> seq1{"CC--CCCC", "CCCCCC--", "CCCCC--C"};
        std::vector<std::string> seq2{"CCCCCCCC", "CCCCCCCC", "CCCCCCCC"};
        std::vector<std::string> weight{"0.031239", "0.499854", "0.249923"};
        std::vector<std::string> lweight{"-3.46609", "-0.69344", "-1.3866"};
        test("CCCCCC", "CCCCCCCC", seq1, seq2, weight, lweight);
    }
    SUBCASE("length of reference not multiple of 3") {
        coati::random_t rand;
        coati::alignment_t aln;
        aln.data.path = "marg-sample.fasta";
        std::ofstream outfile;
        outfile.open(aln.data.path);
        REQUIRE(outfile);
        outfile << ">seq1\nAC\n>seq2\nACG\n";
        outfile.close();
        CHECK_THROWS_AS(marg_sample(aln, 1, rand), std::invalid_argument);
        REQUIRE(std::filesystem::remove("marg-sample.fasta"));
    }
    SUBCASE("length of descendant no multiple of gap len") {
        coati::random_t rand;
        coati::alignment_t aln;
        aln.data.path = "marg-sample.fasta";
        aln.gap.len = 3;
        std::ofstream outfile;
        outfile.open(aln.data.path);
        REQUIRE(outfile);
        outfile << ">A\nCCC\n>B\nCCCC\n";
        outfile.close();
        CHECK_THROWS_AS(marg_sample(aln, 1, rand), std::invalid_argument);
        REQUIRE(std::filesystem::remove("marg-sample.fasta"));
    }
    SUBCASE("error opening output file") {
        coati::random_t rand;
        coati::alignment_t aln;
        aln.data.path = "marg-sample.fasta";
        aln.data.out_file = {{"."}, {".fasta"}};
        CHECK_THROWS_AS(marg_sample(aln, 1, rand), std::invalid_argument);
    }
    SUBCASE("Number of seqs  != 2") {
        coati::random_t rand;
        coati::alignment_t aln;
        aln.data.path = "marg-sample.fasta";
        std::ofstream outfile;
        outfile.open(aln.data.path);
        REQUIRE(outfile);
        outfile << ">A\nCCC\n";
        outfile.close();
        CHECK_THROWS_AS(marg_sample(aln, 1, rand), std::invalid_argument);
        aln.data.path = "marg-sample.fasta";
        outfile.open("marg-sample.fasta");
        REQUIRE(outfile);
        outfile << ">A\nCCC\n>B\nCCC\n>C\nCCC\n";
        outfile.close();
        CHECK_THROWS_AS(marg_sample(aln, 1, rand), std::invalid_argument);
        REQUIRE(std::filesystem::remove("marg-sample.fasta"));
    }
}
// GCOVR_EXCL_STOP

}  // namespace coati