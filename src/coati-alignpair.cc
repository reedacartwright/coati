/*
# Copyright (c) 2020 Juan J. Garcia Mesa <juanjosegarciamesa@gmail.com>
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

#include <fst/fstlib.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <fstream>
#include <boost/filesystem.hpp>
#include <coati/mut_models.hpp>

using namespace fst;
using namespace std;

namespace po = boost::program_options;

int main(int argc, char *argv[]) {

	VectorFst<StdArc> mutation_fst;
	string fasta, mut_model, weight_f, output;
	bool score = false;

	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h","Display this message")
			("fasta,f",po::value<string>(&fasta)->required(), "fasta file path")
			("model,m",po::value<string>(&mut_model)->default_value("m-coati"),
				"substitution model: coati, m-coati (default), dna, ecm, m-ecm")
			("weight,w",po::value<string>(&weight_f), "Write alignment score to file")
			("output,o",po::value<string>(&output), "Alignment output file")
			("score,s", "Calculate alignment score using marginal COATi model")
		;

		po::positional_options_description pos_p;
		pos_p.add("fasta",-1);
		po::variables_map varm;
		po::store(po::command_line_parser(argc,argv).options(desc).positional(pos_p).run(),varm);

		if(varm.count("help")) {
			cout << desc << endl;
			exit(EXIT_SUCCESS);
		}

		if(varm.count("score")) {
			 score = true;
		}

		po::notify(varm);

	} catch (po::error& e) {
		cerr << e.what() << ". Exiting!" << endl;
		exit(EXIT_FAILURE);
	}


	// read input fasta file sequences as FSA (acceptors)
	vector<string> seq_names, sequences;
	vector<VectorFst<StdArc>> fsts;

	if(read_fasta(fasta,seq_names, fsts, sequences) != 0) {
		cerr << "Error reading " << fasta << " file. Exiting!" << endl;
		exit(EXIT_FAILURE);
	} else if(seq_names.size() < 2 || seq_names.size() != fsts.size()) {
		cerr << "At least two sequences required. Exiting!" << endl;
		exit(EXIT_FAILURE);
	}

	if(mut_model.compare("coati") == 0) {
		mg94(mutation_fst);
	} else if(mut_model.compare("dna") == 0) {
		dna(mutation_fst);
	} else if(mut_model.compare("ecm") == 0) {
		ecm(mutation_fst);
	} else if(mut_model.compare("m-ecm") == 0) {
		ecm_marginal(mutation_fst);
	} else if(mut_model.compare("m-coati") == 0) {
		vector<string> alignment;
		float weight;
		ofstream out_w;

		if(score) {
			cout << alignment_score(sequences) << endl;
			exit(EXIT_SUCCESS);
		}

		alignment = mg94_marginal(sequences, weight);

		if(!weight_f.empty()) {
			// append weight and fasta file name to file
			out_w.open(weight_f, ios::app | ios::out);
			out_w << fasta << "," << mut_model << "," << weight << endl;
			out_w.close();
		}

		if(output.empty()) {
			// if no output is specified save in current dir in PHYLIP format
			output = boost::filesystem::path(fasta).stem().string()+".phy";
		}
		// write alignment
		if(boost::filesystem::extension(output) == ".fasta") {
			write_fasta(alignment, output, seq_names);
		} else {
			write_phylip(alignment, output, seq_names);
		}

		exit(EXIT_SUCCESS);
	} else {
		cerr << "Mutation model specified is unknown. Exiting!\n";
		exit(EXIT_FAILURE);
	}

	if(output.empty()) {
		// if no output is specified save in current dir with .phy extension
		output = boost::filesystem::path(fasta).stem().string()+".phy";
	}

	// get indel FST
	VectorFst<StdArc> indel_fst;
	indel(indel_fst, mut_model);

	// sort mutation and indel FSTs
	VectorFst<StdArc> mutation_sort, indel_sort;
	mutation_sort = ArcSortFst<StdArc, OLabelCompare<StdArc>>(mutation_fst, OLabelCompare<StdArc>());
	indel_sort = ArcSortFst<StdArc, ILabelCompare<StdArc>>(indel_fst, ILabelCompare<StdArc>());

	// compose mutation and indel FSTs
	ComposeFst<StdArc> coati_comp = ComposeFst<StdArc>(mutation_sort, indel_sort);

	// optimize coati FST
	VectorFst<StdArc> coati_fst;
	coati_fst = optimize(VectorFst<StdArc>(coati_comp));

	// find alignment graph
	// 1. compose in_tape and coati FSTs
	ComposeFst<StdArc> aln_inter = ComposeFst<StdArc>(fsts[0], coati_fst);
	// 2. sort intermediate composition
	VectorFst<StdArc> aln_inter_sort;
	aln_inter_sort = ArcSortFst<StdArc,OLabelCompare<StdArc>>(aln_inter, OLabelCompare<StdArc>());
	// 3. compose intermediate and out_tape FSTs
	VectorFst<StdArc> graph_fst;
	Compose(aln_inter_sort,fsts[1], &graph_fst);

	//  case 1: ComposeFst is time: O(v1 v2 d1 (log d2 + m2)), space O(v1 v2)
	//			ShortestPath with ComposeFst (PDT) is time: O((V+E)^3), space: O((V+E)^3)
	//          Then convert to VectorFst (FST) is time, space: O(e^(O(V+E)))
	//  case 2: ComposeFst is time: O(v1 v2 d1 (log d2 + m2)), space: O(v1 v2)
	//			Convert ComposeFst (PDT) to VectorFst(FST) is time, space: O(e^(O(V+E)))
	//			Then ShortestPath with VectorFst is O(V log(V) + E)
	//  case 3: Compose is time: O(V1 V2 D1 (log D2 + M2)), space O(V1 V2 D1 M2)
	//			Then ShortestPath with VectorFst is O(V log(V) + E)

	// find shortest path through graph
	VectorFst<StdArc> aln_path;
	ShortestPath(graph_fst, &aln_path);

	// shortestdistance = weight of shortestpath
	if(!weight_f.empty()) {
		vector<StdArc::Weight> distance;
		ofstream out_w;

		ShortestDistance(aln_path, &distance);
		// append weight and fasta file name info in file
		out_w.open(weight_f, ios::app | ios::out);
		out_w << fasta << "," << mut_model << "," << distance[0] << endl;
		out_w.close();
	}

	// topsort path FST
	TopSort(&aln_path);

	// write path FST
	if(boost::filesystem::extension(output) == ".fasta") {
		write_fasta(aln_path, output, seq_names);
	} else {
		write_phylip(aln_path, output, seq_names);
	}

    return 0;
}
