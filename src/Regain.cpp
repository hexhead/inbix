/* =============================================================================
 *
 * Filename: Regain.cpp - Bill White - 4/23/13 - ported from Encore
 *
 * Description:  Regression GAIN calculation. Uses linear or logistic 
 * regression in calculating main effects and interactions of SNPs and 
 * numeric attributes.
 *
 * Created:  06/20/2011
 * Original Author:  Nick Davis, nick-davis@utulsa.edu
 * =============================================================================
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iterator>
#include <sstream>

#include <omp.h>

// PLINK functions and data types
#include "plink.h"
#include "options.h"
#include "model.h"
#include "logistic.h"
#include "linear.h"
#include "stats.h"
#include "helper.h"
// inbix constants and PLINK point PP
#include "Regain.h"
#include "Insilico.h"

bool pvalComparator(const matrixElement &l, const matrixElement &r) {
  return l.first < r.first;
}

Regain::Regain(bool compressionFlag, double sifThreshold, bool componentsFlag) {
  writeCompressedFormat = compressionFlag;
  sifThresh = sifThreshold;
  writeComponents = componentsFlag;
  // defaults
  integratedAttributes = false;
  doFdrPrune = false;
  useOutputThreshold = false;
  outputThreshold = 0.0;
  outputTransform = REGAIN_OUTPUT_TRANSFORM_NONE;
  outputFormat = REGAIN_OUTPUT_FORMAT_FULL;
  pureInteractions = false;
  failureValue = 0;
  nanCount = 0;
  infCount = 0;
  minMainEffect = 0;
  maxMainEffect = 0;
  minInteraction = 0;
  maxInteraction = 0;
  testParameter = 1;
}

Regain::Regain(bool compressionFlag, double sifThreshold, 
               bool integrative, bool componentsFlag,
               bool fdrPruneFlag, bool initMatrixFromData) {
  // set class vars to passed args
  writeCompressedFormat = compressionFlag;
  sifThresh = sifThreshold;
  integratedAttributes = integrative;
  writeComponents = componentsFlag;
  doFdrPrune = fdrPruneFlag;
  // set integrative/normal regain vars
  // additional ext for integrative
  string ext = integratedAttributes ? ".block" : "";
  // header in betas files
  string hdr = integratedAttributes ? "attr" : "SNP";
  // open output files
  string beta_f = par::output_file_name + ext + ".betas";
  string mebeta_f = par::output_file_name + ext + ".mebetas";
  BETAS.open(beta_f, ios::out);
  MEBETAS.open(mebeta_f, ios::out);
  PP->printLOG("Writing interaction beta values to [ " + beta_f + " ]\n");
  PP->printLOG("Writing main effect beta values to [ " + mebeta_f + " ]\n");
  // TODO: remove these set precision options?
//  BETAS.precision(6);
//  MEBETAS.precision(6);
  // print header
  if(par::regainPureInteractions) {
    BETAS << hdr << "1\t" << hdr << "2\tB_0";
    if(par::covar_file) {
      for(uint i = 0; i < par::clist_number; i++) {
        BETAS << "\t" << PP->clistname[i] << "\t" << PP->clistname[i] << " P-VAL";
      }
    }
    BETAS << "\tB_I\tB_I P-VAL" << endl;
  } else {
    BETAS << hdr << "1\t" << hdr << "2\tB_0\tB_1\tB_1 P-VAL\tB_2\tB_2 P-VAL";
    if(par::covar_file) {
      for(uint i = 0; i < par::clist_number; i++) {
        BETAS << "\t" << PP->clistname[i] << "\t" << PP->clistname[i] << " P-VAL";
      }
    }
    BETAS << "\tB_I\tB_I P-VAL" << endl;
  }
  MEBETAS << hdr << "\tB_0\tB_1\tB_1 P-VAL";
  if(par::covar_file) {
    for(uint i = 0; i < par::clist_number; i++) {
      MEBETAS << "\t" << PP->clistname[i] << "\t" << PP->clistname[i]
        << " P-VAL";
    }
  }
  MEBETAS << endl;
  string sif_f = par::output_file_name + ext + ".sif";
  SIF.open(sif_f, ios::out);
  PP->printLOG("Writing Cytoscape network file (SIF) to [ " + sif_f + " ]\n");
  SIF.precision(6);
  if(writeComponents) {
    string snp_sif_f = par::output_file_name + ".snp.sif";
    string num_sif_f = par::output_file_name + ".num.sif";
    string int_sif_f = par::output_file_name + ".int.sif";
    SNP_SIF.open(snp_sif_f, ios::out);
    PP->printLOG("Writing SNP Cytoscape network file (SIF) to [ " +
      snp_sif_f + " ]\n");
    SNP_SIF.precision(6);
    NUM_SIF.open(num_sif_f, ios::out);
    PP->printLOG("Writing numeric Cytoscape network file (SIF) to [ " +
      num_sif_f + " ]\n");
    NUM_SIF.precision(6);
    INT_SIF.open(int_sif_f, ios::out);
    PP->printLOG("Writing integrative Cytoscape network file (SIF) to [ " +
      int_sif_f + " ]\n");
    INT_SIF.precision(6);
  }
  if(initMatrixFromData) {
    // total number of attributes
    numAttributes = 0;
    if(integratedAttributes) {
      numAttributes = PP->nl_all + PP->nlistname.size();
    } else {
      numAttributes = PP->nl_all;
    }
    PP->printLOG("Total number of attributes [ " + int2str(numAttributes) + " ]\n");
    sizeMatrix(regainMatrix, numAttributes, numAttributes);
    sizeMatrix(regainPMatrix, numAttributes, numAttributes);
  }
  useOutputThreshold = par::regainMatrixThreshold;
  outputThreshold = par::regainMatrixThresholdValue;
  outputTransform = REGAIN_OUTPUT_TRANSFORM_NONE;
  outputFormat = REGAIN_OUTPUT_FORMAT_FULL;
  pureInteractions = par::regainPureInteractions;
  failureValue = par::regainFailValue;
  nanCount = 0;
  infCount = 0;
  minMainEffect = 0;
  maxMainEffect = 0;
  minInteraction = 0;
  maxInteraction = 0;
  testParameter = 1;
}

void Regain::setFailureValue(double fValue) {
  failureValue = fValue;
}

void Regain::performPureInteraction(bool flag) {
  pureInteractions = flag;
}

bool Regain::setOutputThreshold(double threshold) {
  useOutputThreshold = true;
  outputThreshold = threshold;
  return true;
}

bool Regain::setOutputFormat(RegainOutputFormat format) {
  outputFormat = format;
  return true;
}

bool Regain::setOutputTransform(RegainOutputTransform transform) {
  outputTransform = transform;
  return true;
}

void Regain::logOutputOptions() {
  stringstream ss;
  switch(outputTransform) {
    case REGAIN_OUTPUT_TRANSFORM_ABS:
      PP->printLOG("Output transform [ absolute value ]\n");
      break;
    case REGAIN_OUTPUT_TRANSFORM_NONE:
      break;
    case REGAIN_OUTPUT_TRANSFORM_THRESH:
      ss << "Output transform [ threshold values < " << outputThreshold
        << " => 0 ]\n";
      PP->printLOG(ss.str());
      break;
  }
  switch(outputFormat) {
    case REGAIN_OUTPUT_FORMAT_UPPER:
      PP->printLOG("Output format [ upper triangular matrix ]\n");
      break;
    case REGAIN_OUTPUT_FORMAT_FULL:
      PP->printLOG("Output format [ full matrix ]\n");
      break;
  }
  PP->printLOG("Regression failure substitution value [ " +
    dbl2str(failureValue) + " ]\n");
}

Regain::~Regain() {
  // close BETAS and SIF ofstreams
  BETAS.close();
  SIF.close();
}

bool Regain::readRegainFromFile(string regainFilename) {
  // open the numeric attributes file if possible
  checkFileExists(regainFilename);
  ifstream REGAIN(regainFilename, ios::in);
  if(REGAIN.fail()) {
    return false;
  }
  // read matrix entries
  bool readHeader = false;
  uint matrixRow = 0;
  uint matrixCol = 0;
  double rawValue = 0;
  while(!REGAIN.eof()) {
    char nline[par::MAX_LINE_LENGTH];
    REGAIN.getline(nline, par::MAX_LINE_LENGTH, '\n');
    // convert to string
    string sline = nline;
    if(sline == "") continue;
    // read line from text file into a vector of tokens
    string buf;
    stringstream ss(sline);
    vector<string> tokens;
    while(ss >> buf) {
      tokens.push_back(buf);
    }
    if(!readHeader) {
      // process reGAIN file header
      numAttributes = tokens.size();
      readHeader = true;
      sizeMatrix(regainMatrix, numAttributes, numAttributes);
      for(uint i = 0; i < numAttributes; ++i) {
        attributeNames.push_back(tokens[i]);
      }
      continue;
    } else {
      // process upper reGAIN matrix values, ignoring lower
      if(tokens.size() < numAttributes) {
        matrixCol = matrixRow;
      } else {
        matrixCol = 0;
      }
      for(uint tokenIdx = 0; tokenIdx < tokens.size(); ++tokenIdx, ++matrixCol) {
        if(!from_string<double>(rawValue, tokens[tokenIdx], std::dec)) {
          PP->printLOG("Error parsing token:" + tokens[tokenIdx] + "\n");
          return false;
        }
        // make symmetric from upper triangular if not already
        regainMatrix[matrixRow][matrixCol] = rawValue;
        regainMatrix[matrixCol][matrixRow] = rawValue;
      }
    }
    ++matrixRow;
  }
  REGAIN.close();

  return true;
}

bool Regain::writeRegainToFile(string newRegainFilename) {
  PP->printLOG("Writing REGAIN matrix [ " + newRegainFilename + " ]\n");
  ofstream outFile(newRegainFilename);
  if(outFile.fail()) {
    return false;
  }
  // write header
  uint hIdx = 0;
  for(vector<string>::const_iterator hIt = attributeNames.begin();
    hIt != attributeNames.end(); ++hIt, ++hIdx) {
    if(hIdx) {
      outFile << "\t" << *hIt;
    } else {
      outFile << *hIt;
    }
  }
  outFile << endl;
  // write matrix entries, possibly transformed
  double valueToWrite = 0;
  for(uint i = 0; i < numAttributes; ++i) {
    for(uint j = 0; j < numAttributes; ++j) {
      double rawValue = valueToWrite = regainMatrix[i][j];
      switch(outputTransform) {
        case REGAIN_OUTPUT_TRANSFORM_ABS:
          valueToWrite = fabs(rawValue);
          break;
        case REGAIN_OUTPUT_TRANSFORM_THRESH:
          valueToWrite = rawValue < outputThreshold ? 0 : rawValue;
          break;
        case REGAIN_OUTPUT_TRANSFORM_NONE:
          valueToWrite = rawValue;
          break;
      }
      switch(outputFormat) {
        case REGAIN_OUTPUT_FORMAT_FULL:
          if(j) {
            outFile << "\t" << valueToWrite;
          } else {
            outFile << valueToWrite;
          }
          break;
        case REGAIN_OUTPUT_FORMAT_UPPER:
          if(j < i) {
            // write tabs
            outFile << "\t";
          } else {
            if(j < (numAttributes - 1)) {
              outFile << valueToWrite << "\t";
            } else {
              outFile << valueToWrite;
            }
          }
          break;
      }
    }
    outFile << endl;
  }
  outFile.close();

  return true;
}

bool Regain::writeRegainToSifFile(string newSifFilename) {
  PP->printLOG("Writing REGAIN matrix to SIF [ " + newSifFilename + " ]\n");
  ofstream outFile(newSifFilename);
  if(outFile.fail()) {
    return false;
  }
  for(uint i = 0; i < numAttributes; ++i) {
    for(uint j = i + 1; j < numAttributes; ++j) {
      string attr1 = attributeNames[i];
      string attr2 = attributeNames[j];
      double valueToWrite = regainMatrix[i][j];
      if(valueToWrite > outputThreshold) {
        outFile << attr1 << "\t" << valueToWrite << "\t" << attr2 << endl;
      }
    }
  }
  outFile.close();

  return true;
}

void Regain::run() {
  // reset the warnings list
  warnings.clear();
  failures.clear();
  // OpenMP parallelization of this outer loop
  uint numThreads = omp_get_num_threads();
  uint numProcs = omp_get_num_procs();
  cout << "OpenMP: " << numThreads << " threads available" << endl;
  cout << "OpenMP: " << numProcs << " processors available" << endl;
  // all main effects
  cout << "Run all main effects models" << endl;
  #pragma omp parallel for
  for(int k=0; k < numAttributes; k++) {
    mainEffect(k, k >= PP->nl_all);
  } // Next SNP/numeric attribute
  // all pairs, triangular loop manually expanded and made parallel
  // since this is not intuitive to do with OpenMP pragmas
  cout << "Run all interaction effects models" << endl;
  #pragma omp parallel for
  for(int k=0; k < numAttributes * (numAttributes + 1) / 2; k++) {
    uint varIndex1 = k / (numAttributes + 1);
    uint varIndex2 = k % (numAttributes + 1);
    if(varIndex2 > varIndex1) {
      varIndex1 = numAttributes - varIndex1 - 1;
      varIndex2 = numAttributes - varIndex2;
    }
    if(pureInteractions) {
      pureInteractionEffect(varIndex1, varIndex1 >= PP->nl_all,
                            varIndex2, varIndex2 >= PP->nl_all);
    } else {
      interactionEffect(varIndex1, varIndex1 >= PP->nl_all,
                        varIndex2, varIndex2 >= PP->nl_all);
    }
  } // Next pair of SNPs/numeric attributes
  writeWarnings();
  writeFailures();
  if(nanCount) {
    PP->printLOG("Detected [ " + int2str(nanCount) + " ] NaN's\n");
  }
  if(infCount) {
    PP->printLOG("Detected [ " + int2str(infCount) + " ] Inf's\n");
  }
}

bool Regain::fitModelParameters() {
  bool success = true;
  // build design matrix and fit the model parameters
  currentModel->buildDesignMatrix();
  currentModel->fitLM();
  // Was the model fitting method successful?
  if(!currentModel->isValid()) {
    string failMsg = "WARNING: Invalid regression fit: ";
    RegressionInvalidType invalidReason = 
            currentModel->getRegressionFailureType();
    switch(invalidReason) {
      case REGRESSION_INVALID_NONE:
        failMsg += "Error code REGRESSION_INVALID_NONE detected";
        break;
      case REGRESSION_INVALID_SVDINV:
        failMsg += "SVD inverse failed";
        break;
      case REGRESSION_INVALID_EMPTY:
        failMsg += "Empty model, either individuals or parameters";
        break;
      case REGRESSION_INVALID_MULTICOLL:
        failMsg += "Possible multicollinearity";
        break;
      case REGRESSION_INVALID_VIF:
        failMsg += "VIF check failed";
        break;
      case REGRESSION_INVALID_LINHYPOTH:
        failMsg += "Linear model hypothesis failed";
        break;
      default:
        failMsg += "Regression invalid failure type detected: " + int2str(invalidReason);
        break;
    }
    failures.push_back(failMsg);
    success = false;
  }
  if(!currentModel->fitConverged()) {
    success = false;
  }  
  return success;
}

bool Regain::checkValue(string coefLabel, double checkVal, double checkPval, 
                        double& returnVal, double& returnPval) {
  returnVal = checkVal;
  returnPval = checkPval;
  bool useFailureValue = false;
  // report large p-value of coefficient as a warning
  if(checkPval > par::regainLargeCoefPvalue) {
    stringstream ss;
    ss << "Large p-value [" << checkPval
      << "] on coefficient for variable [" << coefLabel << "]";
    warnings.push_back(ss.str());
  }
  if(std::isinf(checkVal)) {
    useFailureValue = true;
    ++infCount;
    stringstream ss;
    ss << "Regression test statistic is +/-infinity on coefficient "
      << "for interaction variable [ " << coefLabel << " ]\n";
    warnings.push_back(ss.str());
  } 
  if(std::isnan(checkVal)) {
    useFailureValue = true;
    ++nanCount;
    stringstream ss;
    ss << "Regression test statistic is not a number NaN on coefficient "
      << "for interaction variable [ " << coefLabel << "] \n";
    warnings.push_back(ss.str());
  }
  if(useFailureValue) {
    returnVal = failureValue;
    returnPval = 1.0;
  } else {
    if(outputTransform == REGAIN_OUTPUT_TRANSFORM_ABS) {
      if(checkVal < 0) { 
        returnVal = checkVal * -1;
      }
    }
  }
  
  return useFailureValue;  
}

Model* Regain::createUnivariateModel(uint varIndex, bool varIsNumeric) {
  currentModel = 0;
  // logistic regression for binary phenotypes (traits), linear otherwise
  if(par::bt) {
    LogisticModel* m = new LogisticModel(PP);
    currentModel = m;
  } else {
    LinearModel* m = new LinearModel(PP);
    currentModel = m;
  }
  // Set missing data
  currentModel->setMissing();
  // label for regression model
  string coefLabel = "";
  if(varIsNumeric) {
    coefLabel = PP->nlistname[varIndex - PP->nl_all];
  } else {
    coefLabel = PP->locus[varIndex]->name;
  }
  // Main effect of SNP/numeric attribute
  if(varIsNumeric) {
    currentModel->addNumeric(varIndex - PP->nl_all);
  } else {
    currentModel->addAdditiveSNP(varIndex);
  }
  currentModel->label.push_back(coefLabel);
  testParameter = 1;
  currentModel->testParameter = testParameter; // single variable main effect
  // add covariates if specified
  if(par::covar_file) {
    addCovariates(*currentModel);
  }

  return currentModel;
}

Model* Regain::createInteractionModel(uint varIndex1, bool var1IsNumeric, 
                                      uint varIndex2, bool var2IsNumeric) {
  // attempt to fit a model and retrieve the estimated parameters
  double newVal = 0;
  double newPval = 1.0;
  currentModel = 0;
  // logistic regression for binary phenotypes (traits), linear otherwise
  if(par::bt) {
    LogisticModel* m = new LogisticModel(PP);
    currentModel = m;
  } else {
    LinearModel* m = new LinearModel(PP);
    currentModel = m;
  }
  // Set missing data
  currentModel->setMissing();
  // labels in regression model
  string coef1Label = "";
  if(var1IsNumeric) {
    coef1Label = PP->nlistname[varIndex1 - PP->nl_all];
  } else {
    coef1Label = PP->locus[varIndex1]->name;
  }
  string coef2Label = "";
  if(var2IsNumeric) {
    coef2Label = PP->nlistname[varIndex2 - PP->nl_all];
  } else {
    coef2Label = PP->locus[varIndex2]->name;
  }
  // Main effect of SNP/numeric attribute 1
  if(var1IsNumeric) {
    currentModel->addNumeric(varIndex1 - PP->nl_all);
  } else {
    currentModel->addAdditiveSNP(varIndex1);
  }
  currentModel->label.push_back(coef1Label);
  // Main effect of SNP/numeric attribute 2
  if(var2IsNumeric) {
    currentModel->addNumeric(varIndex2 - PP->nl_all);
  } else {
    currentModel->addAdditiveSNP(varIndex2);
  }
  currentModel->label.push_back(coef2Label);
  currentModel->addInteraction(1, 2);
  currentModel->label.push_back("EPI");
  testParameter = 3;
  // add covariates if specified
  if(par::covar_file) {
    addCovariates(*currentModel);
    testParameter += par::clist_number;
  }

  return currentModel;
}

void Regain::mainEffect(uint varIndex, bool varIsNumeric) {
  // setup regression model
  Model *currentModel = createUnivariateModel(varIndex, varIsNumeric);
  // update class variables and open file
  // attempt to fit a model and retrieve the estimated parameters
  double newVal = 0;
  double newPval = 1.0;
  // get the estimated parameters
  vector_t betaMainEffectCoefs;
  vector_t betaMainEffectCoefPvals;
  double mainEffectPval;
  vector_t mainEffectModelSE;
  if(fitModelParameters()) {
    // Obtain estimates and statistics
    betaMainEffectCoefs = currentModel->getCoefs();
    // p-values don't include intercept term
    betaMainEffectCoefPvals = currentModel->getPVals();
    mainEffectPval = betaMainEffectCoefPvals[testParameter - 1];
    mainEffectModelSE = currentModel->getSE();
    double mainEffectValue = failureValue;
    if(par::regainUseBetaValues) {
      mainEffectValue = betaMainEffectCoefs[testParameter];
    } else {
      mainEffectValue = betaMainEffectCoefs[testParameter] /
        mainEffectModelSE[testParameter];
    }
    checkValue("", mainEffectValue, mainEffectPval, newVal, newPval);
#pragma omp critical
    {
      // update the matrices
      regainMatrix[varIndex][varIndex] = newVal;
      regainPMatrix[varIndex][varIndex] = newPval;
      if(par::do_regain_pvalue_threshold) {
        if(newPval > par::regainPvalueThreshold) {
          regainMatrix[varIndex][varIndex] = 0;
        }
      }
      // update main effect betas file
      if(varIsNumeric) {
        MEBETAS << PP->nlistname[varIndex - PP->nl_all];
      } else {
        MEBETAS << PP->locus[varIndex]->name;
      }
      for(uint i = 0; i < betaMainEffectCoefs.size(); ++i) {
        // B0 coefficient doesn't have pval
        if(i == 0) {
          MEBETAS << "\t" << betaMainEffectCoefs[i];
        } else {
          // adjust pvals index since there's no B0 pval
          MEBETAS << "\t" << betaMainEffectCoefs[i]
            << "\t" << betaMainEffectCoefPvals[i - 1];
        }
      }
      MEBETAS << endl;
    }
  } else {
#pragma omp critical
    {
      regainMatrix[varIndex][varIndex] = newVal;
      regainPMatrix[varIndex][varIndex] = newPval;
      MEBETAS << "MODEL FAILED" << endl;
    }
  }

  // free model memory
  delete currentModel;
}

void Regain::addCovariates(Model &m) {
  for(uint i = 0; i < par::clist_number; i++) {
    // add covariate to the model
    m.addCovariate(i);
    m.label.push_back(PP->clistname[i]);
  }
}

void Regain::interactionEffect(uint varIndex1, bool var1IsNumeric,
                               uint varIndex2, bool var2IsNumeric) {
  // labels in regression model
  string coef1Label = "";
  if(var1IsNumeric) {
    coef1Label = PP->nlistname[varIndex1 - PP->nl_all];
  } else {
    coef1Label = PP->locus[varIndex1]->name;
  }
  string coef2Label = "";
  if(var2IsNumeric) {
    coef2Label = PP->nlistname[varIndex2 - PP->nl_all];
  } else {
    coef2Label = PP->locus[varIndex2]->name;
  }
  Model *currentModel = createInteractionModel(varIndex1, var1IsNumeric,
                                               varIndex2, var2IsNumeric);
     // get the estimated parameters
  vector_t betaInteractionCoefs;
  vector_t betaInteractionCoefPVals;
  double interactionVal;
  double interactionPval;
  vector_t interactionModelSE;
  vector_t::const_iterator bIt;
  vector_t::const_iterator sIt;
  vector_t regressTestStatValues;
  double newVal = 0;
  double newPval = 1.0;
  if(fitModelParameters()) {
    // model converged, so get the estimated parameters and statistics
    betaInteractionCoefs = currentModel->getCoefs();
    betaInteractionCoefPVals = currentModel->getPVals();
    interactionPval = 
            betaInteractionCoefPVals[betaInteractionCoefPVals.size() - 1];
    interactionModelSE = currentModel->getSE();
    // calculate statistical test value from beta/SE (t-test or z-test)
    bIt = betaInteractionCoefs.begin();
    sIt = interactionModelSE.begin();
    regressTestStatValues;
    for(; bIt != betaInteractionCoefs.end(); ++bIt, ++sIt) {
      regressTestStatValues.push_back(*bIt / *sIt);
    }
    checkValue("", interactionVal, interactionPval, newVal, newPval);  
    // update class variables and open file
#pragma omp critical
    {
      regainMatrix[varIndex1][varIndex2] = newVal;
      regainMatrix[varIndex2][varIndex1] = newVal;
      regainPMatrix[varIndex1][varIndex2] = newPval;
      regainPMatrix[varIndex2][varIndex1] = newPval;
      // store p-value along with (varIndex1, varIndex2) location of
      // item.  This is used later for FDR pruning
      // TODO: account for invalid values/useFailureValue flag is true
      if(doFdrPrune) {
        pair<int, int> indexPair = make_pair(varIndex1, varIndex2);
        matrixElement interactionPvalElement;
        interactionPvalElement = make_pair(newPval, indexPair);
        gainIntPvals.push_back(interactionPvalElement);
      }
      // update BETAS file
      BETAS << coef1Label << "\t" << coef2Label;
      for(uint  i = 0; i < betaInteractionCoefs.size(); ++i) {
        // B0 coefficient doesn't have pval
        if(i == 0) {
          BETAS << "\t" << betaInteractionCoefs[i];
        } else {
          // adjust pvals index since there's no B0 pval
          BETAS << "\t" << betaInteractionCoefs[i]
            << "\t" << betaInteractionCoefPVals[i - 1];
        }
      }
      BETAS << endl;
      // update SIF files); add to SIF if interaction >= SIF threshold
      if(newVal >= sifThresh) {
        SIF << coef1Label << "\t" << newVal << "\t"
          << coef2Label << endl;
        if(writeComponents) {
          // numeric
          if(var1IsNumeric && var2IsNumeric) {
            NUM_SIF << coef1Label << "\t" << newVal << "\t"
              << coef2Label << endl;
          }// integrative
          else if(var1IsNumeric && !var2IsNumeric) {
            INT_SIF << coef1Label << "\t" << newVal << "\t"
              << coef2Label << endl;
          }// integrative
          else if(!var1IsNumeric && var2IsNumeric) {
            INT_SIF << coef1Label << "\t" << newVal << "\t"
              << coef2Label << endl;
          }// SNP
          else {
            SNP_SIF << coef1Label << "\t" << newVal << "\t"
              << coef2Label << endl;
          }
        }
      }
    }
  } else {
    // update class variables and open file
#pragma omp critical
    {
      regainMatrix[varIndex1][varIndex2] = newVal;
      regainMatrix[varIndex2][varIndex1] = newVal;
      regainPMatrix[varIndex1][varIndex2] = newPval;
      regainPMatrix[varIndex2][varIndex1] = newPval;
      BETAS << "MODEL FAILED" << endl;
    }
  }

  // free model memory
  delete currentModel;
}

void Regain::pureInteractionEffect(uint varIndex1, bool var1IsNumeric,
  uint varIndex2, bool var2IsNumeric) {
  Model* interactionModel;

  // logistic regression for binary phenotypes (traits), linear otherwise
  if(par::bt) {
    LogisticModel* m = new LogisticModel(PP);
    interactionModel = m;
  } else {
    LinearModel* m = new LinearModel(PP);
    interactionModel = m;
  }

  // Set missing data
  interactionModel->setMissing();

  // labels in regression model
  ModelTermType varType1 = ADDITIVE;
  string coef1Label = "";
  uint var1TypeIndex = varIndex1;
  if(var1IsNumeric) {
    varType1 = NUMERIC;
    coef1Label = PP->nlistname[varIndex1 - PP->nl_all];
    var1TypeIndex = varIndex1 - PP->nl_all;
  } else {
    varType1 = ADDITIVE;
    coef1Label = PP->locus[varIndex1]->name;
  }
  ModelTermType varType2 = ADDITIVE;
  string coef2Label = "";
  uint var2TypeIndex = varIndex2;
  if(var2IsNumeric) {
    varType2 = NUMERIC;
    coef2Label = PP->nlistname[varIndex2 - PP->nl_all];
    var2TypeIndex = varIndex2 - PP->nl_all;
  } else {
    varType2 = ADDITIVE;
    coef2Label = PP->locus[varIndex2]->name;
  }

  // add covariates if specified
  if(par::covar_file) addCovariates(*interactionModel);

  // interaction
#if defined(DEBUG_REGAIN)
  cout << "Adding typed interaction for "
    << coef1Label << ", idx: " << var1TypeIndex << ", type: " << varType1
    << " | "
    << coef2Label << ", idx: " << var2TypeIndex << ", type: " << varType2
    << endl;
#endif

  interactionModel->addTypedInteraction(var1TypeIndex, varType1,
    var2TypeIndex, varType2);
  interactionModel->label.push_back("EPI");

  // Build design matrix
  interactionModel->buildDesignMatrix();

  // set test parameter index for interaction regression
  uint testParameter = 1;
  // add # covars to test param to get interaction param
  if(par::covar_file) {
    testParameter += par::clist_number;
  }
  interactionModel->testParameter = testParameter; // interaction

  // fit linear model coefficients
  interactionModel->fitLM();

  // Was the model fitting method successful?
#pragma omp critical
  {
    double interactionValue = 0;
    double interactionPval = 0;
    double interactionValueTransformed = 0;
    bool useFailureValue = false;
    vector_t betaInteractionCoefs;
    vector_t betaInteractionCoefPVals;
    
    if(!interactionModel->isValid()) {
      string failMsg = "WARNING: Invalid regression fit for interaction "
        "variables [" + coef1Label + "], [" + coef2Label + "]";
      failures.push_back(failMsg);
      useFailureValue = true;
    }
    else {
      betaInteractionCoefs = interactionModel->getCoefs();
      betaInteractionCoefPVals = interactionModel->getPVals();
      interactionPval =
        betaInteractionCoefPVals[betaInteractionCoefPVals.size() - 1];
      vector_t interactionModelSE = interactionModel->getSE();
      // calculate statistical test value from beta/SE (t-test or z-test)
      vector_t::const_iterator bIt = betaInteractionCoefs.begin();
      vector_t::const_iterator sIt = interactionModelSE.begin();
      vector_t regressTestStatValues;
      double beta = 0;
      double se = 0;
      double stat = 0;
      for(; bIt != betaInteractionCoefs.end(); ++bIt, ++sIt) {
        beta = *bIt;
        se = *sIt;
        stat = beta / se;
        regressTestStatValues.push_back(stat);
      }

      if(par::regainUseBetaValues) {
        interactionValue = betaInteractionCoefs[betaInteractionCoefs.size() - 1];
        if(interactionPval > par::regainLargeCoefPvalue) {
          stringstream ss;
          ss << "Large p-value [" << interactionPval
            << "] on coefficient for interaction variables ["
            << coef1Label << "][" << coef2Label << "]";
          warnings.push_back(ss.str());
          interactionValue = 0;
        }
        if(std::isinf(interactionValue)) {
          interactionValue = 0;
          ++infCount;
        }
        if(std::isnan(interactionValue)) {
          interactionValue = 0;
          ++nanCount;
        }
      } else {
        interactionValue = regressTestStatValues[regressTestStatValues.size() - 1];
        if(fabs(interactionValue) > par::regainLargeCoefTvalue) {
          stringstream ss;
          ss << "Large test statistic value [" << interactionValue
            << "] on coefficient for interaction variables ["
            << coef1Label << "][" << coef2Label << "]";
          warnings.push_back(ss.str());
          if(interactionValue < 0) {
            interactionValue = -par::regainLargeCoefTvalue;
          } else {
            interactionValue = par::regainLargeCoefTvalue;
          }
          // DEBUG TEST
          interactionValue = 0;
        }
        if(std::isinf(interactionValue)) {
          interactionValue = 0;
          ++infCount;
          stringstream ss;
          ss << "Regression test statistic is +/-infinity on coefficient "
            << "for interaction variables [" << coef1Label << "][" << coef2Label << "]";
          warnings.push_back(ss.str());
        }
        if(std::isnan(interactionValue)) {
          interactionValue = 0;
          ++nanCount;
          stringstream ss;
          ss << "Regression test statistic is not a number NaN on coefficient "
            << "for interaction variables [" << coef1Label << "][" << coef2Label << "]";
          warnings.push_back(ss.str());
        }
      }

      if(useFailureValue) {
        regainMatrix[varIndex1][varIndex2] = failureValue;
        regainMatrix[varIndex2][varIndex1] = failureValue;
        regainPMatrix[varIndex1][varIndex2] = 1.0;
        regainPMatrix[varIndex2][varIndex1] = 1.0;
      } else {
        interactionValueTransformed = interactionValue;
        switch(outputTransform) {
          case REGAIN_OUTPUT_TRANSFORM_NONE:
            break;
          case REGAIN_OUTPUT_TRANSFORM_THRESH:
            if(interactionValue < outputThreshold) {
              interactionValueTransformed = 0.0;
            }
            break;
          case REGAIN_OUTPUT_TRANSFORM_ABS:
            interactionValueTransformed = fabs(interactionValue);
            break;
        }
        regainMatrix[varIndex1][varIndex2] = interactionValueTransformed;
        regainMatrix[varIndex2][varIndex1] = interactionValueTransformed;
        if(par::do_regain_pvalue_threshold) {
          if(interactionPval > par::regainPvalueThreshold) {
            regainMatrix[varIndex1][varIndex2] = 0;
            regainMatrix[varIndex2][varIndex1] = 0;
          }
        }
        regainPMatrix[varIndex1][varIndex2] = interactionPval;
        regainPMatrix[varIndex2][varIndex1] = interactionPval;
      }

      // !!!!! DEBUGGING RAW VALUES !!!!!
#if defined(DEBUG_REGAIN)
      cout << (interactionModel->fitConverged() ? "TRUE" : "FALSE")
        << "\t" << coef1Label << "\t" << coef2Label
        << "\t" << setw(12) << betaInteractionCoefs[1]
        << "\t" << setw(6) << betaInteractionCoefPVals[0]
        << "\t" << setw(12) << interactionModelSE[1]
        << "\t" << setw(12)
        << betaInteractionCoefs[1] / interactionModelSE[1]
        << endl;
#endif

      // store p-value along with (varIndex1, varIndex2) location of
      // item.  This is used later for FDR pruning
      if(doFdrPrune) {
        pair<int, int> indexPair = make_pair(varIndex1, varIndex2);
        matrixElement interactionPvalElement =
          make_pair(interactionPval, indexPair);
        gainIntPvals.push_back(interactionPvalElement);
      }

      // update BETAS file
      BETAS << coef1Label << "\t" << coef2Label;
      for(uint  i = 0; i < betaInteractionCoefs.size(); ++i) {
        // B0 coefficient doesn't have pval
        if(i == 0) {
          BETAS << "\t" << betaInteractionCoefs[i];
        } else {
          // adjust pvals index since there's no B0 pval
          BETAS << "\t" << betaInteractionCoefs[i]
            << "\t" << betaInteractionCoefPVals[i - 1];
        }
      }
      BETAS << endl;

      // update SIF files); add to SIF if interaction >= SIF threshold
      if(interactionValueTransformed >= sifThresh) {
        SIF << coef1Label << "\t" << interactionValueTransformed << "\t"
          << coef2Label << endl;
        if(writeComponents) {
          // numeric
          if(var1IsNumeric && var2IsNumeric) {
            NUM_SIF << coef1Label << "\t" << interactionValueTransformed << "\t"
              << coef2Label << endl;
          }// integrative
          else if(var1IsNumeric && !var2IsNumeric) {
            INT_SIF << coef1Label << "\t" << interactionValueTransformed << "\t"
              << coef2Label << endl;
          }// integrative
          else if(!var1IsNumeric && var2IsNumeric) {
            INT_SIF << coef1Label << "\t" << interactionValueTransformed << "\t"
              << coef2Label << endl;
          }// SNP
          else {
            SNP_SIF << coef1Label << "\t" << interactionValueTransformed << "\t"
              << coef2Label << endl;
          }
        }
      }
    
    } // end if failure else block
    
    // end pragma
  }

  // free model memory
  delete interactionModel;
}

void Regain::writeRegain(bool pvals, bool fdrprune) {
  matrix_t regainMat;
  if(pvals) {
    regainMat = regainPMatrix;
  } else {
    regainMat = regainMatrix;
  }

  // write the reGAIN matrix to file named <output_file_name>.regain
  string snp_f = par::output_file_name;
  string num_f = par::output_file_name;
  string int_f = par::output_file_name;
  string regain_matrix_f = par::output_file_name;

  // additional prefixes/extension for output filename 
  // FDR-pruned
  string prnpre = fdrprune ? ".pruned" : "";
  // p-values file
  string pvpre = pvals ? ".pvals" : "";
  // integrative
  string intpre = integratedAttributes ? ".block" : "";
  // compressed/binary file
  string tail = writeCompressedFormat ? ".gz" : "";

  // additional output text	
  string pvtext = pvals ? "p-value " : "";
  string fdrtext = fdrprune ? "FDR-pruned " : "";

  regain_matrix_f += intpre + pvpre + prnpre + ".regain" + tail;

  PP->printLOG("Writing " + fdrtext + "REGAIN " + pvtext +
    "matrix [ " + regain_matrix_f + " ]\n");
  REGAIN_MATRIX.open(regain_matrix_f, writeCompressedFormat);
  if(writeComponents) {
    snp_f += ".snp" + pvpre + prnpre + ".regain" + tail;
    PP->printLOG("Writing " + fdrtext + "SNP REGAIN " + pvtext +
      "matrix [ " + snp_f + " ]\n");
    SNP_MATRIX.open(snp_f, writeCompressedFormat);

    num_f += ".num" + pvpre + prnpre + ".regain" + tail;
    PP->printLOG("Writing " + fdrtext + "numeric REGAIN " + pvtext +
      "matrix [ " + num_f + " ]\n");
    NUM_MATRIX.open(num_f, writeCompressedFormat);

    int_f += ".int" + pvpre + prnpre + ".regain" + tail;
    PP->printLOG("Writing " + fdrtext + "integrative REGAIN " +
      pvtext + "matrix [ " + int_f + " ]\n");
    INT_MATRIX.open(int_f, writeCompressedFormat);
  }
  // write SNP column names
  for(uint cn = 0; cn < PP->nl_all; ++cn) {
    if(cn) {
      REGAIN_MATRIX << "\t" << PP->locus[cn]->name;
      if(writeComponents) SNP_MATRIX << "\t" << PP->locus[cn]->name;
    } else {
      REGAIN_MATRIX << PP->locus[cn]->name;
      if(writeComponents) SNP_MATRIX << PP->locus[cn]->name;
    }
  }
  // write numeric attribute column names
  for(uint cn = 0; cn < PP->nlistname.size(); ++cn) {
    if(!cn && !PP->nl_all) {
      REGAIN_MATRIX << PP->nlistname[cn];
    } else {
      REGAIN_MATRIX << "\t" << PP->nlistname[cn];
    }
    if(writeComponents) {
      if(cn) {
        NUM_MATRIX << "\t" << PP->nlistname[cn];
        INT_MATRIX << "\t" << PP->nlistname[cn];
      } else {
        NUM_MATRIX << PP->nlistname[cn];
        INT_MATRIX << PP->nlistname[cn];
      }
    }
  }
  REGAIN_MATRIX << "\n";
  if(writeComponents) {
    NUM_MATRIX << "\n";
    INT_MATRIX << "\n";
    SNP_MATRIX << "\n";
  }
  // write matrix entries
  for(uint i = 0; i < numAttributes; ++i) {
    for(uint j = i; j < numAttributes; ++j) {
      if(j == i) {// fill in symmetric entries, replacing j < i with tabs
        // write tabs for upper triangular
        if(outputFormat == REGAIN_OUTPUT_FORMAT_UPPER) {
          string tabs = "";
          for(uint k = 0; k < j; k++) {
            tabs += "\t";
          }
          REGAIN_MATRIX << tabs << dbl2str_fixed(regainMat[i][j], 6);
          if(writeComponents) {
            if(i < PP->nl_all) {
              SNP_MATRIX << tabs << dbl2str_fixed(regainMat[i][j], 6);
            } else {
              tabs = "";
              for(uint k = PP->nl_all; k < j; k++) {
                tabs += "\t";
              }
              NUM_MATRIX << tabs << dbl2str_fixed(regainMat[i][j], 6);
            }
          }
        } else {
          // otherwise write symmetric entries for REGAIN_OUTPUT_FORMAT_FULL
          for(uint k = 0; k <= j; k++) {
            string lineEnd = "";
            if(k != j) {
              lineEnd = "\t";
            }
            REGAIN_MATRIX << dbl2str_fixed(regainMat[i][k], 6) << lineEnd;
            if(writeComponents) {
              if(i < PP->nl_all) {
                SNP_MATRIX << dbl2str_fixed(regainMat[i][k], 6) << lineEnd;
              } else {
                for(uint l = PP->nl_all; l <= j; l++) {
                  if(l != j) {
                    lineEnd = "\t";
                  }
                  NUM_MATRIX << dbl2str_fixed(regainMat[i][l], 6) << lineEnd;
                }
              }
            }
          }
        }
      } else {
        REGAIN_MATRIX << "\t" << dbl2str_fixed(regainMat[i][j], 6);
        if(writeComponents) {
          if(i < PP->nl_all) {
            if(j < PP->nl_all) {
              SNP_MATRIX << "\t" << dbl2str_fixed(regainMat[i][j], 6);
            } else {
              if(j == PP->nl_all) {
                INT_MATRIX << dbl2str_fixed(regainMat[i][j], 6);
              } else {
                INT_MATRIX << "\t" << dbl2str_fixed(regainMat[i][j], 6);
              }
            }
          } else {
            NUM_MATRIX << "\t" << dbl2str_fixed(regainMat[i][j], 6);
          }
        }
      }
    }
    REGAIN_MATRIX << "\n";
    if(writeComponents) {
      if(i < PP->nl_all) {
        SNP_MATRIX << "\n";
        INT_MATRIX << "\n";
      } else {
        NUM_MATRIX << "\n";
      }
    }
  }

  // close output stream
  REGAIN_MATRIX.close();
  if(writeComponents) {
    SNP_MATRIX.close();
    NUM_MATRIX.close();
    INT_MATRIX.close();
  }
}

void Regain::fdrPrune(double fdr) {
  PP->printLOG("Calculating Benjamini Hochberg FDR for pruning\n");
  uint m = gainIntPvals.size();
  // sort gain interaction mal_el type by p-value, maintaining
  // gainPMatrix location (row, col) with sorted values
  sort(gainIntPvals.begin(), gainIntPvals.end(), Regain::mainEffectComparator);

  // use rough FDR (RFDR) to estimate alpha based on input FDR
  double alpha = 2 * m * fdr / (m + 1);
  uint R = -1;
  // BH method
  for(uint i = 0; i < m; i++) {
    double l = (i + 1) * alpha / (double) m;
    // test whether current p-value < current l
    if(gainIntPvals[i].first < l) {
      R = i;
    } else {
      break;
    }
  }

  // BH threshold condition not met with any p-values, so exit
  if(R == -1) {
    PP->printLOG("No p-value meets BH threshold criteria, so nothing pruned\n");
    return;
  }

  // BH rejection threshold
  double T = gainIntPvals[R].first;
  PP->printLOG("BH rejection threshold: T = " + dbl2str(T) + ", R = " +
    int2str(R) + "\n");
  PP->printLOG("Pruning reGAIN interaction terms with p-values > T (" +
    dbl2str(T) + ")\n");

  // now prune (set to 0.0) all values greater than R index
  for(uint i = R + 1; i < m; i++) {
    pair<int, int> p = gainIntPvals[i].second;
    // symmetric matrix, so set [e1][e2] and [e2][e1]
    regainMatrix[p.first][p.second] = 0.0;
    regainMatrix[p.second][p.first] = 0.0;
  }
  PP->printLOG("Pruned " + int2str(m - (R + 1)) +
    " values from reGAIN interaction terms\n");
  // use threshold to write R commands to generate FDR plot 
  writeRcomm(T, fdr);
}

void Regain::writeRcomm(double T, double fdr) {
  ofstream RCOMM;
  RCOMM.precision(6);
  string fdr_r_file = par::output_file_name + ".R";
  string betas_file = par::output_file_name + ".betas";
  PP->printLOG("Writing R commands to generate FDR plot [" + fdr_r_file + "]\n");

  RCOMM.open(fdr_r_file, ios::out);
  RCOMM << "fdrvars <- read.delim(\"" << betas_file << "\")" << endl;
  RCOMM << "library(calibrate)" << endl;
  RCOMM << "betas <- fdrvars$B_3" << endl;
  RCOMM << "pvals <- fdrvars$B_3.P.VAL" << endl;
  RCOMM << "betas <- abs(betas)" << endl;
  RCOMM << "T <- " << T << endl;
  RCOMM << "partition <- " << fdr << endl;
  RCOMM << "plot(betas, -log10(pvals), type=\"n\")" << endl;
  RCOMM << "abline(h=-log10(T), col=\"green4\", lwd=3)" << endl;
  RCOMM << "accept <- which(-log10(pvals) >= -log10(T))" << endl;
  RCOMM << "reject <- which(-log10(pvals) < -log10(T))" << endl;
  RCOMM << "prnidx <- partition * length(betas[accept])" << endl;
  RCOMM << "srtaccbetas <- sort(betas[accept])" << endl;
  RCOMM << "prnval <- srtaccbetas[prnidx]" << endl;
  RCOMM << "if(prnidx%%1!=0){" << endl;
  RCOMM << "prnval <- (srtaccbetas[floor(prnidx)] + srtaccbetas[ceiling(prnidx)]) / 2" << endl;
  RCOMM << "}" << endl;
  RCOMM << "prunex <- which(betas <= prnval)" << endl;
  RCOMM << "pruney <- which(-log10(pvals) >= -log10(T))" << endl;
  RCOMM << "prune <- intersect(prunex, pruney)" << endl;
  RCOMM << "accept <- setdiff(accept, prune)" << endl;
  RCOMM << "points(betas[accept], -log10(pvals[accept]), bg=\"green4\", pch=21)" << endl;
  RCOMM << "snp1 <- fdrvars$SNP1" << endl;
  RCOMM << "snp2 <- fdrvars$SNP2" << endl;
  RCOMM << "textxy(betas[accept], -log10(pvals[accept]), paste(snp1, snp2, sep=\",\")[accept])" << endl;
  RCOMM << "points(betas[reject], -log10(pvals[reject]), bg=\"blue\", pch=22)" << endl;
  RCOMM << "points(betas[prune], -log10(pvals[prune]), bg=\"red\", pch=24)" << endl;
  RCOMM << "abline(v=prnval, col=\"red\", lwd=3)" << endl;
  RCOMM << "title(\"Scatter plot of -log10 transformed p-values vs. regression betas\")" << endl;
  RCOMM << "legend(\"topleft\", inset=.05, title=\"Type\", c(\"Accepted\", \"Rejected\", \"Pruned\"), pch=c(21,22,24), pt.bg=c(\"green4\", \"blue\", \"red\"))" << endl;
  RCOMM.close();
}

bool Regain::mainEffectComparator(const matrixElement &l,
  const matrixElement &r) {
  return l.first < r.first;
}

bool Regain::updateStats() {
  minMainEffect = maxMainEffect = regainMatrix[0][0];
  minInteraction = maxInteraction = regainMatrix[0][1];
  for(uint i = 0; i < numAttributes; ++i) {
    for(uint j = i; j < numAttributes; ++j) {
      if(i == j) {
        if(regainMatrix[i][j] < minMainEffect) {
          minMainEffect = regainMatrix[i][j];
        }
        if(regainMatrix[i][j] > maxMainEffect) {
          maxMainEffect = regainMatrix[i][j];
        }
      } else {
        if(regainMatrix[i][j] < minInteraction) {
          minInteraction = regainMatrix[i][j];
        }
        if(regainMatrix[i][j] > maxInteraction) {
          maxInteraction = regainMatrix[i][j];
        }
      }
    }
  }

  return true;
}

bool Regain::logMatrixStats() {
  updateStats();

  PP->printLOG("reGAIN matrix statistics:\n");
  PP->printLOG("minimum main effect [ " + dbl2str(minMainEffect) + " ]\n");
  PP->printLOG("maximum main effect [ " + dbl2str(maxMainEffect) + " ]\n");
  PP->printLOG("minimum interaction [ " + dbl2str(minInteraction) + " ]\n");
  PP->printLOG("maximum interaction [ " + dbl2str(maxInteraction) + " ]\n");

  return true;
}

void Regain::writeFailures() {
  if(failures.size()) {
    double numCombinations = static_cast<double>(numAttributes * numAttributes);
    double numFailures = (double) failures.size();
    double percentFailures = (numFailures / numCombinations) * 100.0;
    PP->printLOG(dbl2str(numFailures) + " failures in " + 
      dbl2str(numCombinations)+ " regression models "
      + dbl2str(percentFailures) + "%\n");
    string failureFilename = par::output_file_name + ".regression.failures";
    PP->printLOG("Writing failure messages to [ " + failureFilename + " ]\n");
    ofstream failureFile(failureFilename);
    for(vector<string>::const_iterator fIt = failures.begin();
      fIt != failures.end(); ++fIt) {
      failureFile << *fIt << endl;
    }
    failureFile.close();
  }
}
  
  void Regain::writeWarnings() {
  // report warnings to stdout - bcw - 4/30/13
  if(warnings.size()) {
    double numCombinations = (numAttributes * (numAttributes - 1)) / 2.0;
    double numFailures = (double) warnings.size();
    double percentFailures = (numFailures / (numCombinations + numAttributes)) * 100.0;
    PP->printLOG(dbl2str(numFailures) + " warnings in " + 
      dbl2str(numCombinations + numAttributes)+ " regression models "
      + dbl2str(percentFailures) + "%\n");
    string failureFilename = par::output_file_name + ".regression.warnings";
    PP->printLOG("Writing warning messages to [ " + failureFilename + " ]\n");
    ofstream failureFile(failureFilename);
    for(vector<string>::const_iterator wIt = warnings.begin();
      wIt != warnings.end(); ++wIt) {
      failureFile << *wIt << endl;
    }
    failureFile.close();
  }
}
