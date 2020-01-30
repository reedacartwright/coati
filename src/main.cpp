#include <fst/fstlib.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <fstream>
#include "mut_models.h"
#include "utils.h"

using namespace fst;
using namespace std;

namespace po = boost::program_options;

//TODO: incorporate unique pointers
int main(int argc, char *argv[]) {

	VectorFst<StdArc> mutation_fst;
	string fasta, mut_model, weight_f, output;

	try {
		po::options_description desc("Allowed options");
		desc.add_options()
			("help,h","arg1 fasta to align, arg2 model")
			("fasta,f",po::value<string>(&fasta)->required(), "name of fasta file")
			("model,m",po::value<string>(&mut_model)->required(), "substitution model")
			("weight,w",po::value<string>(&weight_f), "weight storing file")
			("output,o",po::value<string>(&output), "output file")
		;

		po::variables_map varm;
		po::store(po::parse_command_line(argc,argv,desc),varm);

		if(varm.count("help") || varm.count("-h")) {
			cout << desc << endl;
			return 0;
		}

		po::notify(varm);

	} catch (po::error& e) {
		cerr << e.what() << ". Exiting!" << endl;
		exit(EXIT_FAILURE);
	}

	if(mut_model.compare("toycoati") == 0) {
		toycoati(mutation_fst);
	} else if(mut_model.compare("toy-marginal") == 0) {
		toy_marg(mutation_fst);
	} else if(mut_model.compare("dna") == 0) {
		dna_mut(mutation_fst);
	} else if(mut_model.compare("ecm") == 0) {
		ecm(mutation_fst);
	} else if(mut_model.compare("ecm-marginal") == 0) {
		ecm_marginal(mutation_fst);
	} else {
		cerr << "Mutation model specified is unknown. Exiting!\n";
		exit(EXIT_FAILURE);
	}

	if(output.empty())
		output = "./"+fasta;

	// read input fasta file sequences as FSA (acceptors)
	vector<string> seq_names;
	vector<VectorFst<StdArc>> fsts;
	if(read_fasta(fasta,seq_names, fsts) != 0) {
		cerr << "Error reading " << fasta << "file. Exiting!" << endl;
		exit(EXIT_FAILURE);
	} else if(seq_names.size() < 2 || seq_names.size() != fsts.size()) {
		cerr << "At least two sequences required. Exiting!" << endl;
		exit(EXIT_FAILURE);
	}

	// TODO: think about deleting fst (delete fst;)

	// tropical semiring & read mutation and indel raw FSTs
	const VectorFst<StdArc> *indel_raw = VectorFst<StdArc>::Read("fst/indel.fst");

	// optimize mutation and indel raw FSTs
	VectorFst<StdArc> indel_fst;
	indel_fst = optimize(*indel_raw);

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
	write_fasta(aln_path, output, seq_names);

    return 0;
}
