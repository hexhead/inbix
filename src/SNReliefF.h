/**
 * \class SNReliefF
 *
 * \brief Signal-to-Noise ReliefF attribute ranking algorithm.
 *
 * Designed to handle digital gene expression (DGE) data sets, particularly
 * RNA-Seq high-throughput count data, by accounting for variable-specific
 * variance in counts. Data is known to follow a Poisson or negative binomial
 * distribution. This algorithm is a more computationally practical approach
 * than others that use more sophisticated statistical methods and models.
 * Our approach keeps the ReliefF algorithm general while addressing the
 * variance "dispersion" problem as a special case.
 *
 * \sa ReliefF
 *
 * \author Bill White
 * \version 1.0
 *
 * Contact: bill.c.white@gmail.com
 * Created on: 7/21/12
 */

#ifndef SNRELIEFF_H
#define	SNRELIEFF_H

#include <vector>
#include <map>
#include <string>
#include <fstream>

#include "plink.h"

#include "ReliefF.h"
#include "Dataset.h"
#include "DatasetInstance.h"
#include "Insilico.h"

/// a pair of vectors for hit and miss statistics for each instance
typedef std::vector<std::pair<double, double> > InstanceAttributeStats;
typedef std::vector<std::pair<double, double> >::const_iterator
		InstanceAttributeStatsIt;

typedef std::pair<InstanceAttributeStats, InstanceAttributeStats>
	InstanceHitMissStats;

/// a map from instance ID to neighbor statistics
typedef std::vector<InstanceHitMissStats> NeighborStats;
typedef std::vector<InstanceHitMissStats>::iterator NeighborStatsIt;
typedef std::vector<InstanceHitMissStats>::const_iterator NeighborStatsCIt;

class SNReliefF : public ReliefF
{
public:
  SNReliefF(Dataset* ds, Plink* plinkPtr);
  bool ComputeAttributeScores() override;
  /// Precompute nearest neighbor gene statistics for all instances.
  bool PreComputeNeighborGeneStats();
  /// Print the neighbor statistics data structure
  void PrintNeighborStats();
  virtual ~SNReliefF();
private:
  /// Computes the nearest neighbor statistics for a particular instance.
  bool ComputeInstanceStats(DatasetInstance* dsi,
  		std::vector<unsigned int> hitIndicies,
  		std::vector<unsigned int> missIndicies,
  		InstanceHitMissStats& hitMissStats);
  /// Prints all attribute stats to stdout.
  void PrintInstanceAttributeStats(InstanceAttributeStats stats);
  /// nearest neighbor attribute averages and standard deviations
  NeighborStats neighborStats;
};

#endif	/* SNReliefF_H */
