/* 
 * File:   DcVar.cpp
 * Author: bwhite 10/19/17
 */

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>
#include <omp.h>

// Armadillo Linear Algebra/Matrices/Vectors
#include <armadillo>

// Boost lexical cast
#include <boost/lexical_cast.hpp>

// PLINK
#include "options.h"
#include "plink.h"
#include "helper.h"
#include "zed.h"
#include "stats.h"

// inbix
#include "DcVar.h"
#include "Insilico.h"
#include "StringUtils.h"
#include "ArmadilloFuncs.h"

using namespace std;
using namespace insilico;
using namespace boost;
using namespace arma;

bool pvalComparatorAscending(const matrixElement &l, const matrixElement &r) {
  return l.first < r.first;
}

DcVar::DcVar(SNP_INPUT_TYPE snpInputTypeParam, bool hasChipSeq, bool debugFlag) {
  PP->printLOG("dcVar initializing\n");
  snpInputType = snpInputTypeParam;
  chipSeq = hasChipSeq;
  debugMode = debugFlag;
  if(snpInputTypeParam == SNP_SRC_FILE) {
    // OMRF data files gzipped and tab-delimited
    if(!ReadGenotypesFile()) {
      error("Reading genotypes failed. Exiting.");
    }
//    if(!ReadSnpLocationsFile()) {
//      error("Reading SNP location information file failed. Exiting.");
//    }
    if(!ReadGeneExpressionFile()) {
      error("Reading gene expression file failed. Exiting.");
    }
    chipSeqMode = hasChipSeq;
    if(chipSeqMode) {
      if(!ReadChipSeqFile()) {
        error("Reading ChIP-seq file failed. Exiting.");
      }
    }
    PP->printLOG("Using separate tab-delimited files for input data sets\n");
  } else {
    // assume PLINK data structures accessible through PP pointer
    PP->printLOG("Using PLINK files for input data sets\n");
  }
  SetDebugMode(debugFlag);
  if(!CheckInputs()) {
    error("Checking data sets compatability failed. Exiting.");
  }
  // OpenMP parallelization
  uint numThreads = omp_get_num_threads();
  uint numProcs = omp_get_num_procs();
  PP->printLOG("OpenMP: " + int2str(numThreads) + " threads available\n");
  PP->printLOG("OpenMP: " + int2str(numProcs) + " processors available\n");
}

DcVar::~DcVar() {
}

bool DcVar::Run(bool debugFlag) {
  if(snpInputType == SNP_SRC_PLINK) {
    RunPlink(debugFlag);
  }
  if(snpInputType == SNP_SRC_FILE) {
    RunOMRF(debugFlag);
  }
  return true;
}

bool DcVar::RunPlink(bool debugFlag) {
  if(chipSeqMode) {
    PP->printLOG("ChIP-seq not supported with PLINK files (yet)\n");
    return false;
  }
  PP->printLOG("Preparing dcVar analysis on PLINK files\n");
  // NOTE: THE SNP2Ind() CALL IS CRITICAL!!! 2/24/15
  PP->SNP2Ind();
  int numSnps = PP->nl_all;
  int numGenes = PP->nlistname.size();
  // make sure we have variants
  if(numSnps < 1) {
    error("Variants file must specified at least one variant for this analysis!");
  }
  // make sure we have genes
  if(numGenes < 2) {
    error("Gene expression file must specified for this analysis!");
  }
  PP->printLOG(int2str(numSnps) + " variants, and " + int2str(numGenes) + " genes\n");

  // for all variants
  unsigned int snpIdx;
  for(snpIdx=0; snpIdx < numSnps; ++snpIdx) {
    string variantName = PP->locus[snpIdx]->name;
    // PP->printLOG("Variant: " + variantName + "\n");
    // get variant info as case-control phenotype based on variant model
    // cout << "PP->n: " << PP->n << endl;
    // cout << "sample size: " << PP->sample.size() << endl;
    for(int sampleIdx=0; sampleIdx < PP->n; sampleIdx++) {
      Individual* person = PP->sample[sampleIdx];
      // cout << "variantIdx: " << variantIdx << endl;
      // cout << "sampleIdx: " << sampleIdx << endl;
      // cout << "phenotype: " << person->phenotype << endl;
      // cout << "aff: " << person->aff << endl;
      // cout << "locus size: " << PP->locus.size() << endl;
      // cout << "SNP allele1 size: " << person->one.size() << endl;
      // cout << "SNP allele2 size: " << person->two.size() << endl;
      bool i1 = person->one[snpIdx];
      bool i2 = person->two[snpIdx];
      // cout << "i1: "<< i1 << ", i2:  " << i2 << endl;
      // see Caleb's email of 2/24/15 for phenotype assignment based on var model param
      // and bit-wise genotype encoding
      double thisPheno = -9;
      bool thisAff = false;
      if(i1) {
        if(!i2) {
          // 10 het
          thisPheno = 1;
          thisAff = true;
        } else {
          // 11
          thisPheno = 1;
          thisAff = true;
        }
      } else {
        // 01 // het 
        if(i2) {
          if(par::dcvar_var_model == "rec") {
            thisPheno = 1;
            thisAff = true;
          }
          else {
            if(par::dcvar_var_model == "dom") {
              thisPheno = 0;
              thisAff = false;
            }
            // else "hom" missing pheno = -9
          }
        }
        // 00
        else {
          thisPheno = 0; // hom
          thisAff = false;
        }
      }
      // cout 
      // 	<< "Variant index: " << variantIdx << "\t" 
      // 	<< "[ " << i1 << ", " << i2 << " ]" << "\t"
      // 	<< "Sample: " << sampleIdx << "\t" 
      // 	<< "Phenotype: " << thisPheno << "\t" 
      // 	<< "Affected: " << thisAff
      // 	<< endl;
      person->phenotype = thisPheno;
      person->aff = thisAff;
    }

    // run dcGAIN for this variant phenotype
    zVals.zeros(numGenes, numGenes);
    pVals.zeros(numGenes, numGenes);
    armaDcgain(zVals, pVals);
    // DEBUG
    // cout << "results" << endl << results.submat(0,0,4,4) << endl;
    // cout << "interactionPvals" << endl << interactionPvals.submat(0,0,4,4) << endl;
    // armaWriteMatrix(results, "DEBUG.dcgain", PP->nlistname);
    // armaWriteMatrix(interactionPvals, "DEBUG.interactionPvals", PP->nlistname);

    // save p-values that pass BH rejection threshold
    // setup output file
    string dcvarFilename = variantName + ".dcVarTest.txt";
    ofstream dcvarFile;
    PP->printLOG("Writing results to [ " + dcvarFilename + " ]\n");
    dcvarFile.open(dcvarFilename);
    if(dcvarFile.fail()) {
      error("Cannot open dcVar test results file for writing.");
    }

    double nVars = (double) numGenes;
    numCombs = (nVars * (nVars - 1.0)) / 2.0;
    // get all p-values
    vector_t testPvals;
    FlattenPvals(testPvals);
    int numPvals = testPvals.size();
    double minP = 1.0;
    double maxP = 0.0;
    int goodPvalCount = 0;
    if(par::do_dcvar_pfilter) {
      if(par::dcvar_pfilter_type == "fdr") {
        // ------------------------------------------------------------------------
        PP->printLOG("Filtering using Benjamini-Hochberg FDR threshold\n");
        // sort the array of matrix elements
        sort(testPvals.begin(), testPvals.end());
        // use rough FDR (RFDR) to estimate alpha based on input FDR
        double m = (double) numPvals * (double) numSnps;
        double alpha = 2 * m * par::dcvar_pfilter_value  / (m + 1);
        int thresholdIndex = -1;
        minP = testPvals[0];
        maxP = testPvals[numPvals - 1];
        // BH method
        for(int i = 0; i < numPvals; i++) {
          double l = (i + 1) * alpha / (double) numPvals;
          // test whether current p-value < current l
          if(testPvals[i] < l) {
            thresholdIndex = i;
          } else {
            break;
          }
        }
        if(thresholdIndex == -1) {
          PP->printLOG("No p-value meets BH threshold criteria, so nothing saved\n");
        } else {
          // BH rejection threshold
          double T = testPvals[thresholdIndex];
          PP->printLOG("BH rejection threshold T = " + dbl2str(T) + ", R = " +
            int2str(thresholdIndex) + "\n");
          ++goodPvalCount;
          // now prune (set to 0.0) all values greater than R index
          for(int i = 0; i <= thresholdIndex; i++) {
            double p = testPvals[i];
            uint pRow = i / numPvals;
            uint pCol = i % numPvals;
            string gene1 = PP->nlistname[pRow];
            string gene2 = PP->nlistname[pCol];
#pragma omp critical 
{
            dcvarFile << gene1 << "\t" << gene2 << "\t" << p << endl;
}
          }
        }
      } else {
        PP->printLOG("Filtering using Bonferroni threshold\n");
        // insure doubles used in all intermediate calculations
        double correctedP = par::dcvar_pfilter_value / (numCombs * numSnps);
        double minP = testPvals[0];
        double maxP = testPvals[numPvals - 1];
        for(int i=0; i < numPvals; ++i) {
          double p = testPvals[i];
          uint pRow = i / numPvals;
          uint pCol = i % numPvals;
          string gene1 = PP->nlistname[pRow];
          string gene2 = PP->nlistname[pCol];
          if(p < minP) minP = p;
          if(p > maxP) maxP = p;
          if(p <  correctedP) {
            ++goodPvalCount;
#pragma omp critical 
{
            dcvarFile << gene1 << "\t" << gene2 << "\t" << p << endl;
}
            }
          } // end interactionPvals
          PP->printLOG("Found [" + int2str(goodPvalCount) + "] tested p-values, min/max: " + 
            dbl2str(minP) + " / " + dbl2str(maxP) + "\n");
      }
    } else {
      // no p-value filtering
      PP->printLOG("Saving ALL p-values\n");
      for(int i=0; i < numPvals; ++i) {
        double p = testPvals[i];
        uint pRow = i / numPvals;
        uint pCol = i % numPvals;
        string gene1 = PP->nlistname[pRow];
        string gene2 = PP->nlistname[pCol];
#pragma omp critical 
{
        dcvarFile << gene1 << "\t" << gene2 << "\t" << p << endl;
}
      }
    }
    dcvarFile.close();
  } // END all snps loop

  return true;
}

bool DcVar::RunOMRF(bool debugFlag) {
  // ---------------------------------------------------------------------------
  PP->printLOG("DcVar::RunOMRF: Performing dcVar analysis on .gz and .tab files\n");
  uint numSnps = snpNames.size();
  // expression
  uint numGenes = geneExprNames.size();
  // chipseq
  uint numChipSeq = 0;
//  if(chipSeq) {
//    numChipSeq = chipSeqExpression.size();
//  }
  // make sure we have variants
  if(numSnps < 1) {
    error("SNP genotypes file must include at least one SNP for analysis!");
  }
  // make sure we have genes
  if(numGenes < MIN_NUM_GENES) {
    error("Gene expression data must include at least [ " + int2str(MIN_NUM_GENES) + " ]\n");
  }
  PP->printLOG("Read [ " + int2str(numSnps) + " ] variants, and [ " + 
               int2str(numGenes) + " ] genes\n");

  if(par::do_dcvar_pfilter) {
    PP->printLOG("Filtering p-values using [ " +  par::dcvar_pfilter_type +  " ] correction\n");
    PP->printLOG("Filtering p-values parameter [ " +  dbl2str(par::dcvar_pfilter_value) +  " ]\n");
  }
  
  // ---------------------------------------------------------------------------
  // for all genotypes/SNPs across all subjects, make genotype into binary 
  // phenotype and run differential correlation on the RNA-Seq gene pairs
  uint initSnpIdx = 0;
  if(par::dcvar_resume_snp) {
    pair <uint, string> snpInfo;
    ReadCheckpoint(snpInfo);
    initSnpIdx = snpInfo.first;
  }
  for(uint snpIdx = initSnpIdx; snpIdx < numSnps; ++snpIdx) {
    string snpName = snpNames[snpIdx];
    if(par::verbose) PP->printLOG("--------------------------------------------------------\n");
    PP->printLOG("SNP [ " + snpName + " ] " + int2str(snpIdx + 1) + " of " + 
                 int2str(numSnps) + "\n");
    // ------------------------------------------------------------------------
    if(par::verbose) PP->printLOG("\tCreating phenotype from SNP genotypes\n");
    vector<uint> snpGenotypes;
    for(uint colIdx=0; colIdx < genotypeSubjects.size(); ++colIdx) {
      snpGenotypes.push_back(static_cast<uint>(genotypeMatrix[snpIdx][colIdx]));
    }
    // get variant genotypes for all subject and map to a genetic model
    if(par::verbose) PP->printLOG("\tGenotypes case-control status\n");    
    vector<uint> mappedPhenos;
    MapPhenosToModel(snpGenotypes, 
                     par::dcvar_var_model, 
                     mappedPhenos);
    if(par::verbose) cout << "\tCases:    " << caseIdxCol.size() << "\t";
    if(par::verbose) cout << "Controls: " << ctrlIdxCol.size() << endl;
    // ------------------------------------------------------------------------
    if(par::verbose) PP->printLOG("\tSplitting into case-control groups\n");
    uint numCases = caseIdxCol.size();
    uint numCtrls = ctrlIdxCol.size();
    if((numCases < MIN_NUM_SUBJ_PER_GROUP) || (numCtrls < MIN_NUM_SUBJ_PER_GROUP)) {
      if(par::verbose) PP->printLOG("\tWARNING: groups sizes must be greater than [ " + 
                   int2str(MIN_NUM_SUBJ_PER_GROUP - 1) + " ], skipping SNP\n");
      continue;
    }
    mat casesMatrix(numCases, numGenes);
    mat ctrlsMatrix(numCtrls, numGenes);
    // split into case-control groups for testing DC
    if(!SplitExpressionCaseControl(casesMatrix, ctrlsMatrix)) {
      error("Could not split on case control status");
    }
    // ------------------------------------------------------------------------
    if(par::verbose) PP->printLOG("\tComputeDifferentialCorrelationZals " 
                 "and first pass p-value filter [ " + 
                 dbl2str(DEFAULT_PVALUE_THRESHOLD) + " ]\n");
    // sparse matrix of significant p-values
    if(!ComputeDifferentialCorrelationZsparse(snpName, 
                                              casesMatrix, 
                                              ctrlsMatrix)) {
      error("ComputeDifferentialCorrelationZvals failed");
    }
    // ------------------------------------------------------------------------
    // adjust p-values
    if(par::do_dcvar_pfilter) {
      if(par::verbose) PP->printLOG("\tp-value filtering requested\n");
      FilterPvalues();
      if(par::verbose) PP->printLOG("\t[ " + int2str(zVals.n_nonzero) + 
                   " ] values pass filtering\n");
    } else {
      if(par::verbose) PP->printLOG("\tNo p-value filtering requested so skipping filter\n");
    }
    // ------------------------------------------------------------------------
    // write results, if there are any to write
    if(zVals.n_nonzero) {
      string resultsFilename = 
              par::output_file_name + "." + 
              par::dcvar_pfilter_type + "." +
              snpName + 
              ".pass.tab";
      WriteResults(resultsFilename);
    } else {
      PP->printLOG("\tWARNING: nothing to write for [ " + snpName + " ]\n");
    }

    // write in case the job fails in this loop; resume with command line flag
    WriteCheckpoint(snpIdx, snpName);
  } // end for all SNPs
  
//  zout.close();
  
  return true;
}

bool DcVar::CheckInputs() {
  return true;
}

// TODO: remove this? what was the thinking? public interface? delete?
bool DcVar::SetDebugMode(bool debugFlag) {
  debugMode = debugFlag;
  return true;
}

void DcVar::PrintState() {
  PP->printLOG("-----------------------------------------------------------\n");
  string debugFlag = debugMode? "on": "off";
  PP->printLOG("debug mode:                     " + debugFlag + "\n");
  PP->printLOG("SNPs file:                      " + par::dcvar_genotypes_file + "\n");
  PP->printLOG("SNP locations file:             " + par::dcvar_snp_locations_file + "\n");
  PP->printLOG("CHiP-seq expression file:       " + par::dcvar_chip_seq_file + "\n");
  PP->printLOG("p-value adjust method:          " + par::dcvar_pfilter_type + "\n");
  PP->printLOG("p-value cutoff for file output: " + dbl2str(par::dcvar_pfilter_value) + "\n");
  PP->printLOG("-----------------------------------------------------------\n");
}

bool DcVar::ReadGenotypesFile() {
  checkFileExists(par::dcvar_genotypes_file);
  PP->printLOG("Reading genotypes input from [ " + par::dcvar_genotypes_file + " ]\n");
  ZInput zin(par::dcvar_genotypes_file, compressed(par::dcvar_genotypes_file));
  // read header line
  PP->printLOG("Getting genotype subject names from first line header\n");
  vector<string> tok = zin.tokenizeLine();
	for(int i=1; i < tok.size(); i++) {
    genotypeSubjects.push_back(tok[i]);
  }
  uint lineCounter = 1;
  while(!zin.endOfFile()) {
    ++lineCounter;
	  vector<string> tok = zin.tokenizeLine();
    if(tok.size() < 2) {
      cerr << "WARNING: line [ " << lineCounter 
              << " ] from [ " << par::dcvar_genotypes_file 
              << " ] . . . skipping" << endl;
      continue;
    }
    snpNames.push_back(tok[0]);
    vector<double> lineGenotypes;
	  for(int j=1; j < tok.size(); j++) {
      lineGenotypes.push_back(lexical_cast<double>(tok[j]));
	  }
    genotypeMatrix.push_back(lineGenotypes);
	}
  zin.close();

  PP->printLOG("Read genotypes for [ " + 
  int2str(genotypeSubjects.size()) + " ] subjects and [ " + 
  int2str(snpNames.size()) + " ] SNPs\n");
  
  return true;
}

bool DcVar::ReadSnpLocationsFile() {
  checkFileExists(par::dcvar_snp_locations_file);
  PP->printLOG("Reading SNP locations input from [ " + par::dcvar_snp_locations_file + " ]\n");
  ZInput zin(par::dcvar_snp_locations_file, compressed(par::dcvar_snp_locations_file));
  PP->printLOG("Reading and discarding first line header\n");
  zin.tokenizeLine();
  uint lineCounter = 0;
  while(!zin.endOfFile()) {
    ++lineCounter;
	  vector<string> tok = zin.tokenizeLine();
    if(tok.size() != 5) {
      cerr << "WARNING: reading line [ " << lineCounter 
              << " ] from " << par::dcvar_snp_locations_file 
              << " should have 5 columns, found " << tok.size()
              << ". Blank line(s)?"
              << endl;
      continue;
    }
    SNP_INFO thisSnpInfo;
    thisSnpInfo.chrom = tok[1];
    thisSnpInfo.location = lexical_cast<uint>(tok[2]);
    thisSnpInfo.refAllele = tok[4][0];
    snpLocations[tok[0]] = thisSnpInfo;
	}
  zin.close();
  PP->printLOG("Read subject SNP location info for [ " + 
               int2str(lineCounter) + " ] SNPs\n");

  return true;
}

bool DcVar::ReadGeneExpressionFile() {
  checkFileExists(par::dcvar_gene_expression_file);

  PP->printLOG("Reading gene expression input from [ " + par::dcvar_gene_expression_file + " ]\n");
  ifstream exprFile(par::dcvar_gene_expression_file);
  PP->printLOG("Getting gene expression subject names from first line header\n");
  string header;
  getline(exprFile, header);
  vector<string> headerParts;
  split(headerParts, header, "\t");
  for(uint i=1; i < headerParts.size(); ++i) {
    geneExprSubjects.push_back(headerParts[i]);
  }

  PP->printLOG("Getting gene names from first column, remaining columns gene expression\n");
  string line;
  uint lineCounter = 0;
  while(getline(exprFile, line)) {
    ++lineCounter;
	  vector<string> tok;
    split(tok, line, "\t");
    if(tok.size() < 2) {
      cerr << "Error reading line [ " << lineCounter 
              << " ] from " << par::dcvar_gene_expression_file 
              << " should have more than 2 columns (subjects)"
              << endl;
      continue;
    }
    geneExprNames.push_back(tok[0]);
    vector_t thisExprRec;
    for(uint i=1; i < tok.size(); ++i) {
      thisExprRec.push_back(lexical_cast<double>(tok[i]));
    }
    expressionMatrix.push_back(thisExprRec);
	}
  exprFile.close();
   
  PP->printLOG("Read gene expression for [ " + 
  int2str(geneExprSubjects.size()) + " ] subjects and [ " + 
  int2str(geneExprNames.size()) + " ] genes\n");
  
  double numGenes = (double) geneExprNames.size();
  numCombs = (numGenes * (numGenes - 1.0)) / 2.0;
  PP->printLOG("Number of gene interactions [ " + dbl2str(numCombs) + " ]\n");

  return true;
}

bool DcVar::ReadChipSeqFile() {
  checkFileExists(par::dcvar_chip_seq_file);
  PP->printLOG("Reading ChIP-seq input from [ " + par::dcvar_chip_seq_file + " ]\n");
  ifstream chipSeqFile(par::dcvar_chip_seq_file);
  PP->printLOG("Reading and discarding first line header\n");
  string header;
  getline(chipSeqFile, header);
  uint lineCounter = 0;
  string line;
  while(getline(chipSeqFile, line)) {
    ++lineCounter;
	  vector<string> tok;
    split(tok, line, "\t");
    if(tok.size() != (CHIP_SEQ_SNP + 1)) {
      cerr << "WARNING: reading line [ " << lineCounter 
              << " ] from " << par::dcvar_chip_seq_file 
              << " should have 16 columns, found " << tok.size()
              << ". Blank line(s)?" << endl;
      continue;
    }
    CHIP_SEQ_INFO thisChipSeqInfo;
    thisChipSeqInfo.chrom = tok[CHIP_SEQ_CHROM];
    thisChipSeqInfo.position = lexical_cast<uint>(tok[CHIP_SEQ_POS]);
    thisChipSeqInfo.totalRegionReads = lexical_cast<double>(tok[CHIP_SEQ_EXPR]);
    // rs28469609:38367404:C:T
    vector<string> rsnumParts;
    split(rsnumParts, tok[CHIP_SEQ_SNP], ":");
    chipSeqExpression[rsnumParts[0]] = thisChipSeqInfo;
	}
  chipSeqFile.close();
  PP->printLOG("Read ChIP-seq expression for " + int2str(lineCounter) + " SNPs\n");
  
  return true;
}

// build a new phenotype from variant genotypes
bool DcVar::MapPhenosToModel(vector<uint> phenos, string varModel,
                             vector<uint>& mappedPhenos) {
  caseIdxCol.clear();
  ctrlIdxCol.clear();
  for(uint phenoIdx=0; phenoIdx < phenos.size(); ++phenoIdx) {
    uint thisPheno = phenos[phenoIdx];
    int thisMappedPheno = -9;
    string varModel = par::dcvar_var_model;
    if(varModel == "dom") {
      thisMappedPheno = (thisPheno == 2)? 1: 0;
    } else {
      if(varModel == "rec") {
        thisMappedPheno = (thisPheno == 0)? 1: 0;
      } else {
        // hom
        thisMappedPheno = (thisPheno == 1)? 1: -9;
      }
    }
    if(thisMappedPheno) {
      caseIdxCol.push_back(phenoIdx);
    } else {
      ctrlIdxCol.push_back(phenoIdx);
    }
    mappedPhenos.push_back(thisMappedPheno);
  }
  return true;
}

bool DcVar::SplitExpressionCaseControl(mat& caseMatrix, 
                                       mat& ctrlMatrix) {
  uint nGenes = geneExprNames.size();
  uint nCases = caseIdxCol.size();
  uint nCtrls = ctrlIdxCol.size();
  for(uint col=0; col < nCases; ++col) {
    uint thisIndexCol = caseIdxCol[col];
    for(uint row=0; row < nGenes; ++row) {
      caseMatrix(col, row) = expressionMatrix[row][thisIndexCol];
    }
  }
  for(uint col=0; col < nCtrls; ++col) {
    uint thisIndexCol = ctrlIdxCol[col];
    for(uint row=0; row < nGenes; ++row) {
      ctrlMatrix(col, row) = expressionMatrix[row][thisIndexCol];
    }
  }
  
  return true;
}

bool DcVar::ComputeDifferentialCorrelationZvals(string snp, 
                                                mat& cases, 
                                                mat& ctrls) {
  if(par::verbose) PP->printLOG("\tPerforming Z-tests for all RNA-seq interactions\n");
  double n1 = static_cast<double>(caseIdxCol.size());
  double n2 = static_cast<double>(ctrlIdxCol.size());
  uint numGenes = geneExprNames.size();
  double minP = 1.0;
  double maxP = 0.0;
  uint goodPvalCount = 0;
  uint badPvalCount = 0;
  uint infCount = 0;
  double pThreshold = DEFAULT_PVALUE_THRESHOLD;
  if(par::dcvar_pfilter_type == "custom") {
    pThreshold = par::dcvar_pfilter_value;
  }
  if(par::verbose) PP->printLOG("\tFirst pass filter threshold [ " + 
     dbl2str(pThreshold) + " ]\n");
  if(par::verbose) PP->printLOG("\tEntering OpenMP parallel section for [ ");
  if(par::verbose) PP->printLOG(int2str(numCombs) + " ] dcvar combination\n");
  uint i=0, j=0;
#pragma omp parallel for schedule(dynamic, 1) private(i, j)
  for(i=0; i < numGenes; ++i) {
    for(j=i + 1; j < numGenes; ++j) {
      // correlation between this interaction pair (i, j) in cases and controls
      vec caseVarVals1 = cases.col(i);
      vec caseVarVals2 = cases.col(j);
      vec r_ij_1_v = cor(caseVarVals1, caseVarVals2);
      double r_ij_1 = (double) r_ij_1_v[0];
      vec ctrlVarVals1 = ctrls.col(i);
      vec ctrlVarVals2 = ctrls.col(j);
      vec r_ij_2_v = cor(ctrlVarVals1, ctrlVarVals2);
      double r_ij_2 = (double) r_ij_2_v[0];
      // differential correlation Z
      double z_ij_1 = 0.5 * log((abs((1 + r_ij_1) / (1 - r_ij_1))));
      double z_ij_2 = 0.5 * log((abs((1 + r_ij_2) / (1 - r_ij_2))));
      double Z_ij = abs(z_ij_1 - z_ij_2) / sqrt((1.0 / (n1 - 3.0) + 1.0 / (n2 - 3.0)));
      #pragma omp critical 
      {
        // !NOTE! critical section for writing to an already opened file 'zout'!
        // and to keep track of counts, min/max p-values in public scope
        double p = DEFAULT_PVALUE;
        if(std::isinf(Z_ij)) {
          // bad Z
          ++infCount;
        } else {
          //matrixElement interactionPvalElement;
          //pair<uint, uint> indexPair = make_pair(i, j);
          p = 2 * normdist(-abs(Z_ij));

          if(p < minP) minP = p;
          if(p > maxP) maxP = p;
          if(p <= pThreshold) {
            ++goodPvalCount;
            string outString = snp +  "\t"  +
                    geneExprNames[i] + "\t"  +
                    geneExprNames[j] + "\t"  +
                    dbl2str(p);
            //zout.writeLine(outString);
          } else {
            ++badPvalCount;
          } // end else good p
        } // end else good Z
      } // end openmp critical section
    } // end for j cols
  } // end for i rows
  if(par::verbose) PP->printLOG("End OpenMP parallel section");
  if(par::verbose) PP->printLOG("\tminp [" + dbl2str(minP) + " ] "
                                "maxp [ " + dbl2str(maxP) + " ]\n");
  
  if(par::verbose) PP->printLOG("\t[ " + int2str(infCount) + " ] infinite Z values, no p-values\n");
  if(par::verbose) PP->printLOG("\t[ " + int2str(badPvalCount) + " ] p-values failed threshold test\n");
  if(par::verbose) PP->printLOG("\t[ " + int2str(goodPvalCount) + " ] p-values passed threshold test\n");
  totalTests = goodPvalCount + badPvalCount + infCount;
  if(par::verbose) PP->printLOG("\t[ " + int2str(totalTests) + " ] total tests\n");
  
  return true;
}

bool DcVar::ComputeDifferentialCorrelationZ(string snp, 
                                            mat& cases, 
                                            mat& ctrls,
                                            double correctedP) {
  PP->printLOG("\tPerforming Z-tests for all rna-seq interactions\n");
  double n1 = static_cast<double>(caseIdxCol.size());
  double n2 = static_cast<double>(ctrlIdxCol.size());
  uint numVars = geneExprNames.size();
  double minP = 1.0;
  double maxP = 0.0;
  uint goodPvalCount = 0;
#pragma omp parallel for schedule(dynamic, 1)
  for(uint i=0; i < numVars; ++i) {
    for(uint j=i + 1; j < numVars; ++j) {
      // correlation between this interaction pair (i, j) in cases and controls
      vec caseVarVals1 = cases.col(i);
      vec caseVarVals2 = cases.col(j);
      vec r_ij_1_v = cor(caseVarVals1, caseVarVals2);
      double r_ij_1 = (double) r_ij_1_v[0];
      vec ctrlVarVals1 = ctrls.col(i);
      vec ctrlVarVals2 = ctrls.col(j);
      vec r_ij_2_v = cor(ctrlVarVals1, ctrlVarVals2);
      double r_ij_2 = (double) r_ij_2_v[0];
      // differential correlation Z
      double z_ij_1 = 0.5 * log((abs((1 + r_ij_1) / (1 - r_ij_1))));
      double z_ij_2 = 0.5 * log((abs((1 + r_ij_2) / (1 - r_ij_2))));
      double Z_ij = abs(z_ij_1 - z_ij_2) / sqrt((1.0 / (n1 - 3.0) + 1.0 / (n2 - 3.0)));
      double p = 2 * normdist(-abs(Z_ij)); 
      if(std::isinf(Z_ij)) {
        cerr << "InfiniteZ" << "\t"
                << snp << "\t"
                << geneExprNames[i] << "\t" 
                << geneExprNames[j] << "\t" 
                << Z_ij << "\t"
                << p 
                << endl;
        continue;
      }
      bool writeResults = false;
      if(par::do_dcvar_pfilter) {
        if(p < correctedP) {
          writeResults = true;
        }
      } else {
        writeResults = true;
      }
      if(writeResults) {
        ++goodPvalCount;
#pragma omp critical 
{
      cout << snp << "\t"
              << geneExprNames[i] << "\t" 
              << geneExprNames[j] << "\t" 
              << Z_ij << "\t"
              << p 
              << endl;
}
      }
    }
  }

  PP->printLOG("\t[ " + int2str(goodPvalCount) + " ] p-values passed threshold test\n");
  
  return true;
}

bool DcVar::ComputeDifferentialCorrelationZsparse(string snp, 
                                                  mat& cases, 
                                                  mat& ctrls) {
  if(par::verbose) PP->printLOG("\tPerforming Z-tests for all RNA-seq interactions\n");
  double n1 = static_cast<double>(cases.n_rows);
  double n2 = static_cast<double>(ctrls.n_rows);
  uint numGenes = geneExprNames.size();
  double minP = 1.0;
  double maxP = 0.0;
  uint goodPvalCount = 0;
  uint badPvalCount = 0;
  uint infCount = 0;
  double pThreshold = DEFAULT_PVALUE_THRESHOLD;
  if(par::dcvar_pfilter_type == "custom") {
    pThreshold = par::dcvar_pfilter_value;
  }
  if(par::verbose) PP->printLOG("\tFirst pass filter threshold [ " + 
     dbl2str(pThreshold) + " ]\n");
  if(par::verbose) PP->printLOG("\tEntering OpenMP parallel section for [ ");
  if(par::verbose) PP->printLOG(int2str(numCombs) + " ] dcvar combination\n");
  uint i=0, j=0;
  zVals.set_size(numGenes, numGenes);
  pVals.ones(numGenes, numGenes);
#pragma omp parallel for schedule(dynamic, 1) private(i, j)
  for(i=0; i < numGenes; ++i) {
    for(j=i + 1; j < numGenes; ++j) {
      // correlation between this interaction pair (i, j) in cases and controls
      vec caseVarVals1 = cases.col(i);
      vec caseVarVals2 = cases.col(j);
      vec r_ij_1_v = cor(caseVarVals1, caseVarVals2);
      double r_ij_1 = (double) r_ij_1_v[0];
      vec ctrlVarVals1 = ctrls.col(i);
      vec ctrlVarVals2 = ctrls.col(j);
      vec r_ij_2_v = cor(ctrlVarVals1, ctrlVarVals2);
      double r_ij_2 = (double) r_ij_2_v[0];
      // differential correlation Z
      double z_ij_1 = 0.5 * log((abs((1 + r_ij_1) / (1 - r_ij_1))));
      double z_ij_2 = 0.5 * log((abs((1 + r_ij_2) / (1 - r_ij_2))));
      double Z_ij = abs(z_ij_1 - z_ij_2) / sqrt((1.0 / (n1 - 3.0) + 1.0 / (n2 - 3.0)));
      #pragma omp critical 
      {
        // !NOTE! critical section for writing to an already opened file 'zout'!
        // and to keep track of counts, min/max p-values in public scope
        double p = DEFAULT_PVALUE;
        if(std::isinf(Z_ij)) {
          // bad Z
          ++infCount;
        } else {
          //matrixElement interactionPvalElement;
          //pair<uint, uint> indexPair = make_pair(i, j);
          p = 2 * normdist(-abs(Z_ij));
          if(p < minP) minP = p;
          if(p > maxP) maxP = p;
          if(p <= pThreshold) {
            ++goodPvalCount;
//            string outString = snp +  "\t"  +
//                    geneExprNames[i] + "\t"  +
//                    geneExprNames[j] + "\t"  +
//                    dbl2str(p);
//            zout.writeLine(outString);
            zVals(i, j) = Z_ij;
            pVals(i, j) = p;
          } else {
            ++badPvalCount;
          } // end else good p
        } // end else good Z
      } // end openmp critical section
    } // end for j cols
  } // end for i rows
  if(par::verbose) PP->printLOG("End OpenMP parallel section");
  if(par::verbose) PP->printLOG("\tminp [" + dbl2str(minP) + " ] "
                                "maxp [ " + dbl2str(maxP) + " ]\n");
  
  if(par::verbose) PP->printLOG("\t[ " + int2str(infCount) + " ] infinite Z values, no p-values\n");
  if(par::verbose) PP->printLOG("\t[ " + int2str(badPvalCount) + " ] p-values failed threshold test\n");
  if(par::verbose) PP->printLOG("\t[ " + int2str(goodPvalCount) + " ] p-values passed threshold test\n");
  totalTests = goodPvalCount + badPvalCount + infCount;
  if(par::verbose) PP->printLOG("\t[ " + int2str(totalTests) + " ] total tests\n");
  
  return true;
}

bool DcVar::FlattenPvals(vector_t& retPvals) {
  if(par::verbose) PP->printLOG("\tflattening p-values list into a vector\n");
  retPvals.clear();
  for(uint i=0; i < pVals.n_rows; ++i) {
    for(uint j=i + 1; j < pVals.n_rows; ++j) {
      retPvals.push_back(pVals(i, j));
    }
  }
  
  return true;
}

bool DcVar::FilterPvalues() {
  vector_t interactionPvals;
  FlattenPvals(interactionPvals);
  if(par::verbose) PP->printLOG("\tFiltering p-values using [ " +  
               par::dcvar_pfilter_type + " ] correction\n");
  uint numPruned = 0;
  if(par::verbose) PP->printLOG("\t[ " + int2str(interactionPvals.size()) + " ] p-values before pruning\n");
  if(par::dcvar_pfilter_type == "fdr") {
    numPruned = PruneFdrBH();
  } else {
    if(par::dcvar_pfilter_type == "bon") {
      numPruned = PruneBonferroni();
    } else {
      if(par::dcvar_pfilter_type == "custom") {
        numPruned = PruneCustom();
      } else {
        error("Unknown p-value filter type. Expects \"bon\" or \"fdr\" or \"custom\"."   
            "Got [ " + par::dcvar_pfilter_type + " ]");
      }
    }
    if(par::verbose) PP->printLOG("\t[ " + int2str(numPruned) + " ] p-values pruned\n");
    if(par::verbose) PP->printLOG("\t[ " + int2str(interactionPvals.size()) + " ] p-values after pruning\n");
  }

  return true;
}

uint DcVar::PruneFdrBH() {
  vector_t interactionPvals;
  FlattenPvals(interactionPvals);
  uint numPruned = 0;
  // Adapted for code by Nick Davis in Encore, which became inbix.
  if(par::verbose) PP->printLOG("\tCalculating FDR using Benjamini-Hochberg for pruning\n");
  uint m = interactionPvals.size() * snpNames.size();
  // sort interaction by p-value
  sort(interactionPvals.begin(), interactionPvals.end());

  // use rough FDR (RFDR) to estimate alpha based on input FDR
  double alpha = 2 * m * DEFAULT_FDR / (m + 1);
  int R = -1;
  // BH method
  for(int i = 0; i < m; i++) {
    double l = (i + 1) * alpha / (double) m;
    // test whether current p-value < current l
    if(interactionPvals[i] < l) {
      R = i;
    } else {
      break;
    }
  }

  // BH threshold condition not met with any p-values, so exit
  if(R == -1) {
    if(par::verbose) PP->printLOG("\tWARNING: No p-value meets BH threshold criteria, so no pruning\n");
    numPruned = numCombs;
    interactionPvals.clear();
    return numPruned;
  }

  // BH rejection threshold
  double T = interactionPvals[R];
  if(par::verbose) PP->printLOG("\tBH rejection threshold: T = [ " + dbl2str(T) + " ], R = " +
    int2str(R) + "\n");
  if(par::verbose) PP->printLOG("\tPruning interactions with p-values > T [ (" +
    dbl2str(T) + " ])\n");
  // now prune (set to 0.0) all values greater than threshold T
  uint numGenes = geneExprNames.size();
  for(uint i=0; i < numGenes; ++i) {
    for(uint j=i + 1; j < numGenes; ++j) {
      if(pVals(i, j) > T) {
        zVals(i, j) = 0;
        zVals(j, i) = 0;
        ++numPruned;
      }
    }
  }
      
  if(par::verbose) PP->printLOG("\tPruned [ " + int2str(numPruned) + " ] values from interaction terms\n");

  return numPruned;
}

uint DcVar::PruneBonferroni() {
  double correctedP = par::dcvar_pfilter_value / (numCombs * snpNames.size());
  if(par::verbose) PP->printLOG("\tBonferroni pruning with correctedP [ " + 
               dbl2str(correctedP) + " ]\n");
  uint numPruned = 0;
  uint numGenes = geneExprNames.size();
  for(uint i=0; i < numGenes; ++i) {
    for(uint j=i + 1; j < numGenes; ++j) {
      if(pVals(i, j) > correctedP) {
        zVals(i, j) = 0;
        zVals(j, i) = 0;
        ++numPruned;
      }
    }
  }
      
  if(par::verbose) PP->printLOG("\tPruned [ " + int2str(numPruned) + " ] values from interaction terms\n");
  
  return numPruned;
}

uint DcVar::PruneCustom() {
  uint numPruned = 0;
  sp_mat::const_iterator matIt = zVals.begin();
  for(; matIt != zVals.end(); ++matIt) {
    uint row = matIt.row();
    uint col = matIt.col();
    double pvalue = pVals(row, col);
    if(pvalue > par::dcvar_pfilter_value) {
      zVals(row, col) = 0;
      zVals(col, row) = 0;
      ++numPruned;
    }
  }
  if(par::verbose) {
    PP->printLOG("\tz-values pruned [ " + int2str(numPruned) + " ]\n");
    PP->printLOG("\tnon-zero z-values [ " + int2str(zVals.n_nonzero) + " ]\n");
  }
  PP->printLOG("\tnon-zero z-values [ " + int2str(zVals.n_nonzero) + " ]\n");
  
  return numPruned;
}

bool DcVar::WriteCheckpoint(uint snpIndex, string snpName) {
  ofstream checkpointFile(CHECKPOINT_FILENAME);
  checkpointFile << snpIndex << endl << snpName << endl;
  checkpointFile.close();
  
  return true;
}

bool DcVar::ReadCheckpoint(std::pair<uint, string>& lastSnp) {
  ifstream checkpointFile(CHECKPOINT_FILENAME);
  uint snpIndex;
  string snpName;
  string fileLine;
  getline(checkpointFile, fileLine);
  snpIndex = lexical_cast<uint>(fileLine);
  getline(checkpointFile, snpName);
  checkpointFile.close();
  lastSnp.first = snpIndex;
  lastSnp.second = snpName;
  
  return true;
}

bool DcVar::WriteResults(string filename) {
  if(par::verbose) 
    PP->printLOG("\tWriting interactions that passed p-value filter to [ "  + 
                 filename + " ]\n");
  // avoid writing empty matrix/zero-byte file
  // make no assumption that called has checked; display warning and return
  sp_mat::const_iterator zmatItCurrent = zVals.begin();
  sp_mat::const_iterator zmatItEnd = zVals.end();
  // no elements to write for this SNP?
  if(zmatItCurrent == zmatItEnd) {
    PP->printLOG("\tWARNING: DcVar::WriteResults method attempt to write "
                 "empty z-values sparse matrix\n");
    return false;
  }
  if((zVals.n_rows != pVals.n_rows) ||
     (zVals.n_cols != pVals.n_cols)) {
    PP->printLOG("\tWARNING: DcVar::WriteResults method attempt to write "
                 "z-values matrix dimensions not equal to the p-values matrix\n");
    PP->printLOG("\tZ: " + int2str(zVals.n_rows) + " x " + int2str(zVals.n_cols) + "\n");
    PP->printLOG("\tp: " + int2str(pVals.n_rows) + " x " + int2str(pVals.n_cols) + "\n");
    return false;
  }
  ofstream resultsFile(filename);
  resultsFile << "Gene1\tGene2\tZ\tP" << endl;
  for(; zmatItCurrent != zmatItEnd; ++zmatItCurrent) {
    double zvalue = *zmatItCurrent;
    uint row = zmatItCurrent.row();
    uint col = zmatItCurrent.col();
    double pvalue = pVals(row, col);
    resultsFile 
        << geneExprNames[row] << "\t" 
        << geneExprNames[col] << "\t" 
        << zvalue << "\t"
        << pvalue << "\t"
        << endl;
  }
  resultsFile.close();
  
  return true;
}
