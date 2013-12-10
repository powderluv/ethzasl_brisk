/*
 * HarrisScoreCalculator.cpp
 *
 *  Created on: Aug 3, 2012
 *      Author: lestefan
 */

#include <iostream>

#include <brisk/HarrisScoreCalculatorFloat.hpp>
#include <brisk/sseFilters.hpp>
#include <opencv2/highgui/highgui.hpp>

#include <sys/time.h>

namespace brisk {

void HarrisScoreCalculatorFloat::initializeScores() {
  cv::Mat DxDx1, DyDy1, DxDy1;
  cv::Mat DxDx, DyDy, DxDy;
  // Pipeline.
  getCovarEntries(_img, DxDx1, DyDy1, DxDy1);
  filterGauss3by332F(DxDx1, DxDx);
  filterGauss3by332F(DyDy1, DyDy);
  filterGauss3by332F(DxDy1, DxDy);
  cornerHarris(DxDx, DyDy, DxDy, _scores);
}

void HarrisScoreCalculatorFloat::get2dMaxima(
    std::vector<PointWithScore>& points, float absoluteThreshold) {
  // Do the 8-neighbor nonmax suppression.
  const int stride = _scores.cols;
  const int rows_end = _scores.rows - 2;
  for (int j = 2; j < rows_end; ++j) {
    const float* p = &_scores.at<float>(j, 0);
    const float* const p_begin = p;
    const float* const p_end = &_scores.at<float>(j, stride - 2);
    bool last = false;
    while (p < p_end) {
      const float* const center = p;
      ++p;
      if (last) {
        last = false;
        continue;
      }
      if (*center < absoluteThreshold)
        continue;
      if (*(center + 1) > *center)
        continue;
      if (*(center - 1) > *center)
        continue;
      const float* const p1 = (center + stride);
      const float* const p2 = (center - stride);
      if (*p1 > *center)
        continue;
      if (*p2 > *center)
        continue;
      if (*(p1 + 1) > *center)
        continue;
      if (*(p1 - 1) > *center)
        continue;
      if (*(p2 + 1) > *center)
        continue;
      if (*(p2 - 1) > *center)
        continue;
      const int i = p - p_begin - 1;
#ifdef USE_SIMPLE_POINT_WITH_SCORE
      points.push_back(PointWithScore(*center, i, j));
#else
#error
      points.push_back(PointWithScore(cv::Point2i(i,j),*center));
#endif
    }
  }
}

// X and Y denote the size of the mask.
void HarrisScoreCalculatorFloat::getCovarEntries(const cv::Mat& src,
                                                 cv::Mat& dxdx, cv::Mat& dydy,
                                                 cv::Mat& dxdy) {
  int jump = 0;  // Number of bytes.
  if (src.type() == CV_8U)
    jump = 1;
  else if (src.type() == CV_16U)
    jump = 2;
  else
    assert(0 && "Unsupported type");

  cv::Mat kernel = cv::Mat::zeros(3, 3, CV_32F);
  kernel.at<float>(0, 0) = 0.09375;
  kernel.at<float>(1, 0) = 0.3125;
  kernel.at<float>(2, 0) = 0.09375;
  kernel.at<float>(0, 2) = -0.09375;
  kernel.at<float>(1, 2) = -0.3125;
  kernel.at<float>(2, 2) = -0.09375;

  const unsigned int X = 3;
  const unsigned int Y = 3;

  // Dest will be floats.
  dxdx = cv::Mat::zeros(src.rows, src.cols, CV_32F);
  dydy = cv::Mat::zeros(src.rows, src.cols, CV_32F);
  dxdy = cv::Mat::zeros(src.rows, src.cols, CV_32F);

  const unsigned int maxJ = ((src.cols - 2) / 4) * 4;
  const unsigned int maxI = src.rows - 2;
  const unsigned int stride = src.cols;

  for (unsigned int i = 0; i < maxI; ++i) {
    bool end = false;
    for (unsigned int j = 0; j < maxJ;) {
      __m128 zeros;
      zeros = _mm_setr_ps(0.0, 0.0, 0.0, 0.0);
      __m128 result_dx = zeros;
      __m128 result_dy = zeros;
      // Enter convolution with kernel.
      for (unsigned int x = 0; x < X; ++x) {
        for (unsigned int y = 0; y < Y; ++y) {
          const float m_dx = kernel.at<float>(y, x);
          const float m_dy = kernel.at<float>(x, y);
          __m128 mult_dx = _mm_setr_ps(m_dx, m_dx, m_dx, m_dx);
          __m128 mult_dy = _mm_setr_ps(m_dy, m_dy, m_dy, m_dy);
          __m128 i0;
          if (jump == 1) {
            const uchar* p = &src.at < uchar > (i + y, x + j);
            i0 = _mm_setr_ps(float(*p), float(*(p + 1)), float(*(p + 2)),
                             float(*(p + 3)));
          } else {
            const uint16_t* p = &src.at < uint16_t > (i + y, x + j);
            i0 = _mm_setr_ps(float(*p), float(*(p + 1)), float(*(p + 2)),
                             float(*(p + 3)));
          }

          if (m_dx != 0) {
            __m128 i_dx = _mm_mul_ps(i0, mult_dx);
            result_dx = _mm_add_ps(result_dx, i_dx);
          }

          if (m_dy != 0) {
            __m128 i_dy = _mm_mul_ps(i0, mult_dy);
            result_dy = _mm_add_ps(result_dy, i_dy);
          }
        }
      }

      // Calculate covariance entries - remove precision (ends up being 4 bit),
      // then remove 4 more bits.
      __m128 i_dx_dx = _mm_mul_ps(result_dx, result_dx);
      __m128 i_dx_dy = _mm_mul_ps(result_dy, result_dx);
      __m128 i_dy_dy = _mm_mul_ps(result_dy, result_dy);

      // Store.
      _mm_storeu_ps(&dxdx.at<float>(i, j + 1), i_dx_dx);
      _mm_storeu_ps(&dxdy.at<float>(i, j + 1), i_dx_dy);
      _mm_storeu_ps(&dydy.at<float>(i, j + 1), i_dy_dy);

      // Take care about end.
      j += 4;
      if (j >= maxJ && !end) {
        j = stride - 2 - 4;
        end = true;
      }
    }
  }
}

void HarrisScoreCalculatorFloat::cornerHarris(const cv::Mat& dxdxSmooth,
                                              const cv::Mat& dydySmooth,
                                              const cv::Mat& dxdySmooth,
                                              cv::Mat& dst) {

  // Dest will be float.
  dst = cv::Mat::zeros(dxdxSmooth.rows, dxdxSmooth.cols, CV_32F);
  const unsigned int maxJ = ((dxdxSmooth.cols - 2) / 8) * 8;
  const unsigned int maxI = dxdxSmooth.rows - 2;
  const unsigned int stride = dxdxSmooth.cols;

  for (unsigned int i = 0; i < maxI; ++i) {
    bool end = false;
    for (unsigned int j = 0; j < maxJ;) {
      __m128 dxdx = _mm_loadu_ps(&dxdxSmooth.at<float>(i, j));
      __m128 dydy = _mm_loadu_ps(&dydySmooth.at<float>(i, j));
      __m128 dxdy = _mm_loadu_ps(&dxdySmooth.at<float>(i, j));

      // Determinant terms.
      __m128 prod1 = _mm_mul_ps(dxdx, dydy);
      __m128 prod2 = _mm_mul_ps(dxdy, dxdy);

      // Calculate the determinant.
      __m128 det = _mm_sub_ps(prod1, prod2);

      // Trace - uses kappa = 1 / 16.
      __m128 trace = _mm_add_ps(dxdx, dydy);
      __m128 trace_sq = _mm_mul_ps(trace, trace);
      __m128 trace_sq_00625 = _mm_mul_ps(
          trace_sq, _mm_setr_ps(0.0625, 0.0625, 0.0625, 0.0625));

      // Form score.
      __m128 score = _mm_sub_ps(det, trace_sq_00625);

      // Store.
      _mm_storeu_ps(&dst.at<float>(i, j), score);

      // Take care about end.
      j += 4;
      if (j >= maxJ && !end) {
        j = stride - 2 - 4;
        end = true;
      }
    }
  }
}
}
