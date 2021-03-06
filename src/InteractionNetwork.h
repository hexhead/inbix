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
#include "Insilico.h"

const double DEFAULT_CONNECTIVITY_THRESHOLD = 0;
const double MODULARITY_THRESHOLD = 0; 

typedef unsigned int Indices;
typedef std::vector<Indices> ModuleIndices;
typedef std::vector<ModuleIndices> ModuleList;
typedef std::pair<double, ModuleList> ModularityResult;
typedef std::pair<double, std::vector<double> > HomophilyResult;

typedef struct _RipmResult {
  std::vector<int> degrees;
  std::vector<int> sizes;
  std::vector<Indices> hubs;
  std::vector<int> hubDegrees;
  ModuleList modules;
} RipmResult;

enum MatrixFileType {
  INVALID_FILE,
	REGAIN_FILE,
	CORR_1D_FILE,
	CSV_FILE,
	SIF_FILE
};

enum NetworkMatrixType {
  NET_MATRIX_INVALID,
	NET_MATRIX_ADJ,
	NET_MATRIX_CON,
	NET_MATRIX_BOTH
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
  bool SetConnectivityThresholding(bool connFlag);
  bool SetConnectivityThreshold(double threshold);
  bool SetConnectivityThresholdAbs(bool absFlag);
  bool SetBinaryThresholding(bool binaryFlag);
  
	// adjacency matrix
	unsigned int NumNodes();
	arma::mat GetAdjacencyMatrix();
	arma::mat GetConnectivityMatrix();
	std::vector<std::string> GetNodeNames();
	void PrintAdjacencyMatrix();
	void PrintConnectivityMatrix();
	void PrintSummary();
	void PrintModulesSummary();
	bool WriteToFile(std::string outfile, MatrixFileType fileType=CSV_FILE,
                   NetworkMatrixType matrixType=NET_MATRIX_BOTH);

	// community/modularity methods
	ModularityResult	ModularityLeadingEigenvector();
	// recursive indirect paths modularity - bcw - 5/31/16
	bool ripM(unsigned int pStartMergeOrder, 
		        unsigned int pMaxMergeOrder,
		        unsigned int pMinModuleSize, 
		        unsigned int pMaxModuleSize);
  ModuleList GetModules();
	bool Homophily(HomophilyResult& results);
	double ComputeQ();
	bool SetModulesFromFile(std::string modulesFilename);
	void ShowModules();
	void ShowModuleSizes();
	void ShowModuleIndices();
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
	// apply a power transform with exponent
	bool ApplyFisherTransform();
  // deconvolution - 10/22/13
  bool Deconvolve(arma::mat& nd, double alpha=1, double beta=0.9, int control=0);
  // debugging mode for algorithm development
  void SetDebugMode(bool debugFlag=true);
private:
	// data readers
	bool ReadCsvFile(std::string matrixFilename);
	bool ReadGainFile(std::string gainFilename, bool isUpperTriangular=false);
	bool ReadBrainCorr1DFile(std::string corr1dFilename);
	bool ReadSifFile(std::string sifFilename);

	// prepare the adjacency matrix for analysis algorithm(s)
  bool PrepareConnectivityMatrix();
  
	// matrix writers
	bool WriteDelimitedFile(std::string outFilename, std::string fileType,
                          NetworkMatrixType matrixType=NET_MATRIX_BOTH);
	bool WriteSifFile(std::string outFilename,
                    NetworkMatrixType matrixType=NET_MATRIX_BOTH);

	// logging
	void DebugMessage(std::string msg);

	// modularity support methods
	std::pair<double, arma::vec> ModularityBestSplit(arma::mat& B, double m);
	vector<unsigned int> FlattenModules();

	// rip-M support methods
	ModuleList RecursiveIndirectPathsModularity(ModuleIndices thisModuleIdx);
	bool GetNewmanModules(ModuleIndices thisModuleIdx, 
		                    ModularityResult& results);
	bool MergeSmallModules(ModuleList smallModules,
		                     ModuleList& results);
	bool SumMatrixPowerSeries(arma::mat& A, unsigned int maxPower, arma::mat& B);
	bool CheckMergeResults(ModularityResult results);

	// modules
  bool CheckIndices(ModuleIndices toCheck);
  bool AddModule(ModuleIndices newModule);

	// graph/network filename
	std::string networkFile;
	// graph node names
	std::vector<std::string> nodeNames;
	std::map<std::string, unsigned int> nodeNameIndex;

	// adjacency matrix
	arma::mat adjMatrix;
	// connectivity matrix
	arma::mat connMatrix;

	// node degrees - not necessarily discrete
	arma::rowvec degrees;
	// number of edges/links
	double numEdges;
	// number of nodes/vertices
	double numNodes;
	
  // edge thresholds
  bool useConnectivityThreshold;
  double connectivityThreshold;
  bool connectivityThresholdAbs;
  bool useBinaryThreshold;

  // ripM parameters
	unsigned int startMergeOrder; 	
	unsigned int maxMergeOrder;
	unsigned int maxModuleSize;
	unsigned int minModuleSize;
	// connectivity matrix
	arma::mat ripmMatrix;
  RipmResult ripmResult;

	// communities/modules
	double Q;
	ModuleList modules;
  
  Plink* inbixEnv;
  bool debugMode;
};

#endif
