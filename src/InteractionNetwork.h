/*
 * InteractionNetwork.h - Bill White - 12/1/12
 *
 * In Silico Lab interaction network class.
 */

#ifndef INTERACTION_NETWORK_H
#define INTERACTION_NETWORK_H

#include <string>
#include <vector>
#include <cstring>

#include <armadillo>

#include "plink.h"

const double DEFAULT_CONNECTIVITY_THRESHOLD = 0;
const double MODULARITY_THRESHOLD = 0; 

enum MatrixFileType {
	REGAIN_FILE,
	CORR_1D_FILE,
	CSV_FILE,
	SIF_FILE
};

class InteractionNetwork {
public:
	// construct using a file representing the variable interactions matrix
	InteractionNetwork(std::string matrixFileParam, MatrixFileType fileType,
			bool isUpperTriangular, Plink* pp);
	// matrix constructor for calling as a library method
	InteractionNetwork(double** variablesMatrix, unsigned int dim,
			std::vector<std::string>& variableNames, Plink* pp);
	virtual ~InteractionNetwork();

  // set edge threshold
  bool SetConnectivityThreshold(double threshold);
  bool SetBinaryThresholding(bool binaryFlag);
  
	// write adjacency matrix to file
	unsigned int NumNodes();
	arma::mat GetAdjacencyMatrix();
	std::vector<std::string> GetNodeNames();
	void PrintAdjacencyMatrix();
	void PrintSummary();
	bool WriteToFile(std::string outfile, MatrixFileType fileType=CSV_FILE);

	// community/modularity methods
	std::pair<double, std::vector<std::vector<unsigned int> > >
		ModularityLeadingEigenvector();
	std::pair<double, std::vector<double> >	Homophily();
	double ComputeQ();
	bool SetModulesFromFile(std::string modulesFilename);
	void ShowModules();
	void SaveModules(std::string saveFilename);
  void ShowHomophily();

	// merge this network with another one
	bool Merge(InteractionNetwork& toMerge,
					double priorProbEdges,
					double alpha,
					double omega,
					double threshold);
	
	// apply a power transform with exponent
	bool ApplyPowerTransform(double transformExponent = 1.0);
private:
	// data readers
	bool ReadCsvFile(std::string matrixFilename);
	bool ReadGainFile(std::string gainFilename, bool isUpperTriangular=false);
	bool ReadBrainCorr1DFile(std::string corr1dFilename);
	bool ReadSifFile(std::string sifFilename);

  bool PrepareConnectivytMatrix();
  
	// matrix writers
	bool WriteDelimitedFile(std::string outFilename, std::string fileType);
	bool WriteSifFile(std::string outFilename);

	// modularity support functions
	std::pair<double, arma::vec> ModularityBestSplit(arma::mat& B, double m);
	vector<unsigned int> FlattenModules();

	// graph/network filename
	std::string networkFile;
	// graph node names
	std::vector<std::string> nodeNames;
	std::map<std::string, unsigned int> nodeNameIndex;

	// adjacency matrix
	arma::mat adjMatrix;
	// connectivity matrix
	arma::mat A;
	// node degrees - not necessarily discrete
	arma::rowvec k;
	// number of edges/links
	double m;
	// number of nodes/vertices
	double n;
	
	// modularity matrix
	arma::mat B;
	
  // edge threshold
  double connectivityThreshold;
  bool useBinaryThreshold;
  
	// communities/modules
	double Q;
	std::vector<std::vector<unsigned int> > modules;
  
  Plink* inbixEnv;
};

#endif
