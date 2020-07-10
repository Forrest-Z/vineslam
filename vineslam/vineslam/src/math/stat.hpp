#pragma once

#include <stdlib.h>
#include <iostream>
#include <cmath>

namespace vineslam
{

struct Covariance {
  // Default constructor
  Covariance() = default;

  // Main constructor
  Covariance(const float& m_xx,
             const float& m_yy,
             const float& m_tt,
             const float& m_xy,
             const float& m_xt,
             const float& m_yt)
  {
    xx = m_xx;
    xy = m_xy;
    yy = m_yy;
    tt = m_tt;
    xy = m_xy;
    xt = m_xt;
    yt = m_yt;
  }

  // Assignment operator
  Covariance operator=(const Covariance& other)
  {
    xx = other.xx;
    yy = other.yy;
    tt = other.tt;
    xy = other.xy;
    xt = other.xt;
    yt = other.yt;

    return *this;
  }

  float xx, yy, tt, xy, xt, yt;
};

// Gaussian uses a template for the covariance so that it can be
// - the Covariance structure
// - a simple float representing the standard deviation
template <typename T1, typename T2> struct Gaussian {
  // Default constructor
  Gaussian() = default;

  // Mean & Covariance constructor
  Gaussian(const T1& m_mean, const T2& m_stdev)
  {
    mean  = m_mean;
    stdev = m_stdev;
  }

  // Mean & Covariance & orientation constructor
  Gaussian(const T1& m_mean, const T2& m_stdev, const float& m_theta)
  {
    mean  = m_mean;
    stdev = m_stdev;
    theta = m_theta;
  }

  // Assignment operator
  Gaussian& operator=(const Gaussian& other)
  {
    mean  = other.mean;
    stdev = other.stdev;
    theta = other.theta;

    return *this;
  }

  T1    mean;
  T2    stdev;
  float theta; // TO USE IN REPRESENTATION: the angle of the corresponding ellipse
};

// Samples a zero mean Gaussian
// See https://www.taygeta.com/random/gaussian.html
static float sampleGaussian(const float& sigma, const unsigned long int& S = 0)
{
  if (S != 0)
    srand48(S);
  if (sigma == 0)
    return 0.;

  float x1, x2, w;
  float r;

  do {
    do {
      r = drand48();
    } while (r == 0.0);
    x1 = 2.0 * r - 1.0;
    do {
      r = drand48();
    } while (r == 0.0);
    x2 = 2.0 * drand48() - 1.0;
    w  = x1 * x1 + x2 * x2;
  } while (w > 1.0 || w == 0.0);

  return (sigma * x2 * sqrt(-2.0 * log(w) / w));
}

}; // namespace vineslam