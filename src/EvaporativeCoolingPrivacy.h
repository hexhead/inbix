#ifndef EVAPORATIVECOOLINGPRIVACY_H
#define EVAPORATIVECOOLINGPRIVACY_H

/* 
 * File:   EvaporativeCoolingPrivacy.h
 * Author: bwhite
 *
 * Created on October 18, 2016, 10:57 PM
 */

#include <string>
#include <vector>
#include <armadillo>
#include <random>

#include "plink.h"

#include "Dataset.h"

enum DATASET_TYPE {
  TRAIN, 
  HOLDOUT, 
  TEST
};

class EvaporativeCoolingPrivacy {
public:
  EvaporativeCoolingPrivacy(Dataset* trainset, Dataset* holdoset, 
                            Dataset* testset, Plink* plinkPtr);
  virtual ~EvaporativeCoolingPrivacy();
  bool ComputeScores();
  void PrintState();
private:
  bool ComputeImportance();
  bool ComputeAttributeProbabilities();
  bool GenerateRandomUniformProbabilities();
  bool EvaporateWorstAttributes(uint numToRemove);
  bool EvaporateWorstAttribute();
  double ClassifyAttributeSet(std::vector<std::string> attrs, DATASET_TYPE);
  bool ComputeBestAttributesErrors();
  bool UpdateTemperature();
  bool ComputeInverseImportance();

  // Mersenne twister random number engine - based Mersenne prime 2^19937 − 1
  std::mt19937_64 engine;  
  // PLINK environment Plink object
  Plink* PP;
  // CONSTANTS
  double Q_EPS;
  uint MAX_ITERATIONS;
  
  uint iteration;
  
  // classification data sets
  Dataset* train;
  Dataset* holdout;
  Dataset* test;
  uint numInstances;
  // track original and current set of variables and their indices
  // into the original data set
  std::vector<std::string> origVarNames;
  std::map<std::string, unsigned int> origVarMap;
  std::vector<std::string> curVarNames;
  std::map<std::string, unsigned int> curVarMap;
  
  // importance/quality scores
	AttributeScores trainImportance;       // q_t
	AttributeScores holdoutImportance;     // q_h
	AttributeScores trainInvImportance;    // p_t
	AttributeScores holdoutInvImportance;  // p_h

  // computing importance
  std::map<std::string, double> diffImportance;
  std::vector<double> diffScores;
  std::vector<double> diff;
  double deltaQ;
  double threshold;
  double tolerance;
  uint curDeleteImportanceIndex;
  
  // temperature schedule
  uint numToRemovePerIteration;
  double startTemp;
  double currentTemp;
  double finalTemp;
  double tau;
  
  // algorithm
  uint minRemainAttributes;
  uint updateInterval;
//  uint kConstant;
//  double probBiological; // probability biological influence

  // evaporation variables
  std::vector<double> attributeProbabilty;
  double summedProbabilities;
  std::vector<double> scaledProbabilities;
  std::vector<double> cummulativeProbabilities;
  std::vector<double> randUniformProbs;
  std::vector<std::string> removeAttrs;
  std::vector<std::string> keepAttrs;

  // classification/prediction errors
  double randomForestPredictError;
  double trainError;
  double holdError;
  double testError;
  std::vector<double> trainErrors;
  std::vector<double> holdoutErrors;
  std::vector<double> testErrors;
};

#endif /* EVAPORATIVECOOLINGPRIVACY_H */