/* 
 * File:   RReliefF.cpp
 * Author: billwhite
 * 
 * Created on September 27, 2011, 9:21 PM
 */

#include <iostream>
#include <vector>

#include "plink.h"
#include "helper.h"

#include "ReliefF.h"
#include "RReliefF.h"
#include "Dataset.h"
#include "DistanceMetrics.h"
#include "Insilico.h"

using namespace std;

RReliefF::RReliefF(Dataset* ds, Plink* plinkPtr) :
		ReliefF::ReliefF(ds, plinkPtr, REGRESSION_ANALYSIS) {
	cout << Timestamp() << "RReliefF initialization" << endl;
	if (!ds->HasContinuousPhenotypes()) {
		error("ERROR: Attempting to construct RReliefF object without a continuous phenotype data set");
	}
}

RReliefF::~RReliefF() {
}

bool RReliefF::ComputeAttributeScores() {

  PP->printLOG(Timestamp() + "---------------------------------------\n");
  PP->printLOG(Timestamp() + "Regression Relief-F ComputeAttributeScores() START\n");
	// precompute all instance-to-instance distances and get nearest neighbors
	PreComputeDistances();

	// results are stored in scores
	W.resize(dataset->NumVariables(), 0.0);

	/* using pseudocode notation from paper
	 *
	 * Used to hold the probability of a different class value given nearest
	 * instances (numeric class)
	 */
	double ndc = 0.0;
	/**
	 * Used to hold the probability of different value of an attribute given
	 * nearest instances (numeric class case)
	 */
	vector<double> nda;
	nda.resize(dataset->NumVariables(), 0.0);
	/**
	 * Used to hold the probability of a different class value and different 
   * attribute value given nearest instances (numeric class case)
	 */
	vector<double> ndcda;
	ndcda.resize(dataset->NumVariables(), 0.0);

	// pointer to the (possibly random) instance being sampled
	DatasetInstance* R_i = NULL;
	PP->printLOG(Timestamp() + "Running RRelief-F algorithm:\n");
	vector<string> instanceIds = dataset->GetInstanceIds();
	for (int i = 0; i < (int) m; i++) {

		if (randomlySelect) {
			// randomly sample an instance (without replacement?)
			R_i = dataset->GetRandomInstance();
		} else {
			// deterministic/indexed instance sampling, ie, every instance against
			// every other instance
			unsigned int instanceIndex;
			dataset->GetInstanceIndexForID(instanceIds[i], instanceIndex);
			R_i = dataset->GetInstance(instanceIndex);
		}
		if (!R_i) {
			cerr
					<< "ERROR: Random or indexed instance count not be found for index: ["
					<< i << "]" << endl;
			return false;
		}

		// K NEAREST NEIGHBORS
		// find k nearest neighbors
		vector<unsigned int> nNearestNeighbors;
		bool canGetNeighbors = R_i->GetNNearestInstances(k, nNearestNeighbors);
		if (!canGetNeighbors) {
			cerr << "ERROR: Cannot get " << k << " nearest neighbors" << endl;
			return false;
		}
		// check algorithm preconditions
		if (nNearestNeighbors.size() < 1) {
			cerr << "ERROR: No nearest hits found" << endl;
			return false;
		}
		if (nNearestNeighbors.size() < k) {
			cerr << "ERROR: Could not find enough neighbors" << endl;
			return false;
		}

		// update: using pseudocode notation
		for (unsigned int j = 0; j < k; ++j) {
			// get the jth nearest neighbor
			DatasetInstance* I_j = dataset->GetInstance(nNearestNeighbors[j]);
			double diffPredicted = diffPredictedValueTau(R_i, I_j);
			double d_ij = R_i->GetInfluenceFactorD(j);
			ndc += (diffPredicted * d_ij);
			unsigned int scoresIndex = 0;
      // ---------------------
			// attributes
			vector<unsigned int> attributeIndicies =
					dataset->MaskGetAttributeIndices(DISCRETE_TYPE);
			for (unsigned int attrIdx = 0; attrIdx < attributeIndicies.size();
					++attrIdx) {
				unsigned int A = attributeIndicies[attrIdx];
				double attrScore = snpDiffFuncPtr(A, R_i, I_j) * d_ij;
				nda[scoresIndex] += attrScore;
				ndcda[scoresIndex] += (diffPredicted * attrScore);
        if(par::verbose) {
          cout << "(i, j) = (" << i << "," << j << ") =>"
                  << " diff predicted: " << diffPredicted
                  << ", d_ij: " << d_ij
                  << ", ndc: " << ndc
                  << ", A: " << A
                  << ", snpDiff: " << snpDiffFuncPtr(A, R_i, I_j)
                  << ", nda[A}: " << nda[scoresIndex]
                  << " ndcda[A]: " << ndcda[scoresIndex]
                  << endl;
        }
				++scoresIndex;
			}
      // ---------------------
			// numerics
			vector<unsigned int> numericIndices = 
        dataset->MaskGetAttributeIndices(NUMERIC_TYPE);
			for (unsigned int numIdx = 0; numIdx < numericIndices.size(); ++numIdx) {
				unsigned int N = numericIndices[numIdx];
				double numScore = numDiffFuncPtr(N, R_i, I_j) * d_ij;
				nda[scoresIndex] += numScore;
				ndcda[scoresIndex] += (diffPredicted * numScore);
        if(par::algorithm_verbose) {
          cout << "(i, j) = (" << i << "," << j << ") =>"
                  << " diff predicted: " << diffPredicted
                  << ", d_ij: " << d_ij
                  << ", N: " << N
                  << ", snpDiff: " << numDiffFuncPtr(N, R_i, I_j)
                  << ", nda[N}: " << nda[scoresIndex]
                  << " ndcda[N]: " << ndcda[scoresIndex]
                  << endl;
        }
				++scoresIndex;
			}
      if(par::verbose) cout << "******************************" << endl;
		}

    if(par::verbose) cout << "--------------------------------------------------" << endl;

		// happy lights
		if (i && ((i % 100) == 0)) {
			cout << Timestamp() << i << "/" << m << endl;
		}
	}
	cout << Timestamp() << m << "/" << m << " done" << endl;

	cout << Timestamp() << "Computing final scores" << endl;
	for (unsigned int A = 0; A < dataset->NumVariables(); ++A) {
    double dblM = static_cast<double>(m);
    double tempW = (ndcda[A] / ndc) - ((nda[A] - ndcda[A]) / (dblM - ndc));
    if(std::isnan(tempW)) {
      cerr << "WARNING: detected [NaN] in weight calculation, using zero instead" << endl;
      W[A] = 0.0;
    } else {
      W[A] = tempW;
    }
	}

  PP->printLOG(Timestamp() + "Relief-F ComputeAttributeScores() END\n");
  
	return true;
}
