/*==============================================================================
 *
 * Filename:  regain.h - Bill White - 4/23/13
 *
 * Description:  Regression GAIN calculations.  Uses linear or logistic 
 * regression in calculating main effects and interactions of SNPs and 
 * numeric attributes.
 * 
 * Originaly Created:  02/02/2012
 * Author:  Nick Davis, nick-davis@utulsa.edu
 * =============================================================================
 */

#ifndef __REGAIN_H__
#define __REGAIN_H__

#include <iostream>
#include <vector>

#include "model.h"
#include "zfstream.h"

using namespace std;

// type for storing p-value and matrix position (row,col) of
// reGAIN interaction terms
typedef pair< double, pair<int, int> > matrixElement;

// output options - bcw - 4/30/13
enum RegainOutputFormat {
  REGAIN_OUTPUT_FORMAT_UPPER, 
  REGAIN_OUTPUT_FORMAT_FULL
};

enum RegainOutputTransform {
  REGAIN_OUTPUT_TRANSFORM_NONE, 
  REGAIN_OUTPUT_TRANSFORM_ABS, 
  REGAIN_OUTPUT_TRANSFORM_THRESH
};

class Regain {
public:
	Regain(bool compr, double sifthr, bool integrative, bool compo,
					bool fdrpr = false);
	~Regain();
  // set output threshold
  bool setOutputThreshold(double threshold);
  // set output format
  bool setOutputFormat(RegainOutputFormat format);
  // set output type
  bool setOutputTransform(RegainOutputTransform transform);
  // print output options to stdout and the inbix log
  void logOutputOptions();
	// iterate over all SNPs and numeric attributes (if present), calculating
	// regression for main effect and interaction terms
	void run();
	// calculate main effect regression coefficients for diagonal terms	
	void mainEffect(int varIndex, bool varIsNumeric);
	// add covariate terms to model, handling multiple covariates
	void addCovariates(Model &m);
	// calculate epistatic interaction between two SNPs or numeric attributes,
	// or a SNP and a numeric attribute
	void interactionEffect(int varIndex1, bool var1IsNumeric, 
        int varIndex2, bool var2IsNumeric);
	// calculate epistatic interaction between two SNPs or numeric attributes,
	// or a SNP and a numeric attribute; no main effects, i.e., "pure"
	void pureInteractionEffect(int varIndex1, bool var1IsNumeric, 
        int varIndex2, bool var2IsNumeric);
	// write contents of reGAIN or reGAIN p-value matrix to file.  Integrative
	// reGAIN files have a '.int.' in the filename, p-values have a '.pvals.'
	// If fdr is true, the filename is .pruned.regain, and the log output
	// reflects this.
	void writeRegain(bool pvalues, bool fdrprune = false);
	// Benjamini-Hochberg FDR pruning - removes interaction 
	// terms from reGAIN matrix based on BH FDR threshold
	// code based on method described in All of Statistics p. 167
	void fdrPrune(double fdr);
	// write R commands to plot FDR graph
	void writeRcomm(double T, double fdr);
	// comparison comparator for matrixElement types
	static bool mainEffectComparator(const matrixElement &l, const matrixElement &r);
  // set flag to include main effects in the interaction model or not
  void performPureInteraction(bool flag);
private:
  // output options - bcw - 4/30/13
  bool useOutputThreshold;
  double outputThreshold;
  RegainOutputTransform outputTransform;
  RegainOutputFormat outputFormat;
  // include main effects in interaction model? - bcw - 5/2/13
  bool pureInteractions;
	// integrative regain mode
	bool integratedAttributes;
	// use zlib compression when writing matrix files?
	bool writeCompressedFormat;
	// apply FDR pruning to output matrix?
	bool doFdrPrune;
	// write out component matrices
	bool writeComponents;
	// num attributes (SNPs + numeric for integrative, SNPs for normal regain)
	int numAttributes;
	// SIF interaction threshold
	double sifThresh;
	// Output matrix files (used for writing regain and p-values files)
	ZOutput REGAIN_MATRIX;
	ZOutput SNP_MATRIX;
	ZOutput NUM_MATRIX;
	ZOutput INT_MATRIX;
	// additional output files
	ofstream MEBETAS;
	ofstream BETAS;
	ofstream SIF;
	ofstream SNP_SIF;
	ofstream NUM_SIF;
	ofstream INT_SIF;
	// in memory arrays
	double** regainMatrix;
	double** regainPMatrix;
	// collection of all interaction terms as mat_el types
	vector<matrixElement> gainIntPvals;
  // regression warnings - bcw - 4/30/13
  vector<string> warnings;
};
#endif
