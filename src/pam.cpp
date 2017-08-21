#include "clupp/pam.hpp"

#include "clupp/distance.hpp"

namespace clupp {

/**
 * Data used during the PAM algorithm.
 */
struct pam_data {
  std::set<int> medoids;
  std::set<int> nonselected;
  std::vector<int> classification;
  std::vector<int> second_closest_medoid;

  pam_data(int number_of_objects, int initial_medoid)
      : classification(number_of_objects, initial_medoid)
      , second_closest_medoid(number_of_objects, initial_medoid)
  {
    for(int i = 0; i < number_of_objects; ++i) {
      nonselected.insert(nonselected.end(), i);
    }

    medoids.insert(initial_medoid);
    nonselected.erase(initial_medoid);
  }

  void assign_medoid(int object, int medoid)
  {
    classification[object] = medoid;
  }

  void add_medoid(int medoid)
  {
    medoids.insert(medoid);
    nonselected.erase(medoid);

    assign_medoid(medoid, medoid);
  }

  void swap_medoid(int old_medoid, int new_medoid)
  {
    medoids.erase(old_medoid);
    nonselected.insert(old_medoid);

    add_medoid(new_medoid);

    for(auto &medoid : classification) {
      if(medoid == old_medoid) {
        medoid = new_medoid;
      }
    }

    for(auto &medoid : second_closest_medoid) {
      if(medoid == old_medoid) {
        medoid = new_medoid;
      }
    }
  }
};

/**
 * The initial medoid is the object with the minimum sum of dissimilarities to all other objects.
 *
 * @param distances The distance matrix.
 *
 * @return The index of the object that was found to be the medoid.
 */
int find_initial_medoid(Eigen::MatrixXd const &distances)
{
  Eigen::VectorXd sum_of_dissimilarities(distances.rows());

  for(int i = 0; i < distances.rows(); ++i) {
    sum_of_dissimilarities(i) = distances.row(i).sum();
  }

  int initial_medoid;
  sum_of_dissimilarities.minCoeff(&initial_medoid);

  return initial_medoid;
}

/**
 * The next medoid is the the a nonselected object that decreases the objective function the most.
 *
 * @param distances The distance matrix.
 * @param clustering The current clustering state.
 *
 * @return The index of the object that was found to be the medoid.
 */
int find_next_medoid(Eigen::MatrixXd const &distances, pam_data const &clustering)
{
  double maximum_gain = std::numeric_limits<double>::min();
  int next_medoid = 0;

  // consider an object i which has not been selected yet
  for(auto const i : clustering.nonselected) {
    auto nonselected = clustering.nonselected;
    nonselected.erase(i);

    // track the potential gain of selecting i as a new medoid
    double gain = 0.0;

    // consider another nonselected object j
    for(auto const j : nonselected) {
      // calculate the dissimilarity between j and its currently assigned cluster
      double const D_j = distances(j, clustering.classification[j]);
      // calculate the dissimilarity between j and i
      double const d_j_i = distances(j, i);

      // if the difference of these dissimliarities is positive, it contributes to the selection of i
      gain += std::max(D_j - d_j_i, 0.0);
    }

    // choose the nonselected object that maximizes the gain
    if(gain > maximum_gain) {
      maximum_gain = gain;
      next_medoid = i;
    }
  }

  return next_medoid;
}

/**
 * Reassign objects in the current clustering for the new medoid.
 *
 * @param distances The distance matrix.
 * @param clustering The clustering state to modify.
 */
double reclassify_objects(Eigen::MatrixXd const &distances, pam_data *clustering)
{
  double total_dissimilarity = 0.0;

  for(auto const object : clustering->nonselected) {
    for(auto const medoid : clustering->medoids) {
      auto const object_medoid = clustering->classification[object];
      if(object_medoid != medoid) {
        auto const second_closest_medoid = clustering->second_closest_medoid[object];

        auto const current_distance = distances(object, object_medoid);
        auto const second_distance = distances(object, second_closest_medoid);
        auto const potential_distance = distances(object, medoid);

        if(potential_distance < current_distance) {
          // this medoid is closer, reclassify the current object
          clustering->assign_medoid(object, medoid);
          // the old medoid is now the second closest medoid
          clustering->second_closest_medoid[object] = object_medoid;
        } else if(potential_distance < second_distance) {
          // this medoid is closer than the current second closest medoid, update
          clustering->second_closest_medoid[object] = medoid;
        }
      }
    }

    total_dissimilarity += distances(object, clustering->classification[object]);
  }

  return total_dissimilarity;
}

/**
 * The first phase of pam produces an initial clustering for k objects.
 *
 * @param k The number of initial clusters to find.
 * @param matrix The observations.
 *
 * @return An initial clustering of observations to k objects.
 */
pam_data build(int const k, Eigen::MatrixXd const &distances)
{
  // select an initial medoid by finding the observation with the minimum sum of dissimilarities
  int const initial_medoid = find_initial_medoid(distances);

  // create the initial clustering based on the initial medoid
  pam_data initial_clustering(static_cast<int>(distances.rows()), initial_medoid);

  // refine the initial clustering with an additional k - 1 medoids
  for(int i = 0; i < k - 1; ++i) {
    initial_clustering.add_medoid(find_next_medoid(distances, initial_clustering));
    reclassify_objects(distances, &initial_clustering);
  }

  return initial_clustering;
}

double calculate_swap_cost(Eigen::MatrixXd const &distances,
    int const i,
    int const h,
    pam_data const &clustering)
{
  auto nonselected = clustering.nonselected;
  nonselected.erase(h);

  double total_contribution = 0.0;
  for(auto const j : nonselected) {
    auto const D_j = distances(j, clustering.classification[j]);
    auto const d_j_i = distances(j, i);
    auto const d_j_h = distances(j, h);

    double contribution = 0.0;
    if(D_j >= d_j_i) {
      // j is closer to i than its current (and therefore any other) medoid

      auto const E_j = distances(j, clustering.second_closest_medoid[j]);
      if(d_j_h < E_j) {
        contribution = d_j_h - d_j_i;
      } else {
        //contribution = E_j - D_j;
        contribution = E_j - d_j_i;
      }
    } else if(D_j < d_j_i && D_j > d_j_h) {
      contribution = d_j_h - D_j;
    }

    total_contribution += contribution;
  }

  return total_contribution;
}

void refine(Eigen::MatrixXd const &distances, pam_data *clustering)
{
  bool perform_swaps = true;

  while(perform_swaps) {
    double minimum_contribution = std::numeric_limits<double>::max();
    int old_medoid = -1;
    int new_medoid = -1;

    for(auto const i : clustering->medoids) {
      for(auto const h : clustering->nonselected) {
        auto const contribution = calculate_swap_cost(distances, i, h, *clustering);

        if(contribution < minimum_contribution) {
          minimum_contribution = contribution;
          old_medoid = i;
          new_medoid = h;
        }
      }
    }

    if(minimum_contribution < 0 && old_medoid >= 0 && new_medoid >= 0) {
      clustering->swap_medoid(old_medoid, new_medoid);
      reclassify_objects(distances, clustering);
    } else {
      perform_swaps = false;
    }
  }
}

pam_result partition_around_medoids(int k, Eigen::MatrixXd const &matrix)
{
  if(k < 2) {
    throw std::runtime_error("Error: less than two partitions were requested.");
  } else if(matrix.rows() < k) {
    throw std::runtime_error("Error: not enough rows to create k partitions.");
  }

  // calculate the distances between observations
  Eigen::MatrixXd const distances = calculate_distance_matrix(matrix);

  // build an initial clustering based on the minimum dissimilarity between objects
  auto initial_clustering = build(k, distances);

  // refine the initial clustering by swapping medoids and optimizing the objective function
  refine(distances, &initial_clustering);

  // copy the intermediate data into the final result
  pam_result final_clustering;
  final_clustering.medoids = initial_clustering.medoids;
  final_clustering.classification = initial_clustering.classification;

  return final_clustering;
}
}