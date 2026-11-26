#ifndef FILTERS_HPP
#define FILTERS_HPP

// > Library includes
#include <string>
#include <opencv4/opencv2/opencv.hpp>

/// @brief Namespace for image filters
namespace filters {

    /// @brief Structure to represent the input & output images for a filter
    struct FilterImages {
        cv::Mat input;    // [FILTER] input image
        cv::Mat output;   // [FILTER] output image
    };

    /// @brief Structure to represent the arguments for any filter operation
    struct FilterArgs {
        int tid = -1;        // [WORKER] thread worker id
        int workers = 0;     // [WORKER] number of workers
        FilterImages* img;   // [FILTER] input/output images for filter
        void* args = nullptr;// [FILTER] pointer to filter arguments
    };

    /// @brief Structure to represent the arguments for a blur filter operation
    struct BlurArgs {
        int radius = 1; // [FILTER] blur radius
    };

    /// @brief Structure to represent arguments for a sharpen filter operation
    struct SharpenArgs {
        int radius = 1;
        int intensity = 2;
    };

    /// @brief Structure to represent arguments for a sobel filter operation
    struct SobelArgs {
        int radius = 1;
    };

    // Workers receive FilterArgs* directly (no missing ThreadData dependency).
    // Initialize output borders before starting workers: copy BGR input for
    // blur/sharpen, or zero CV_8UC1 for Sobel. Workers write disjoint interiors.
    // Numerical kernels are shared with filters/filters.h; radius is 1..4.
    /// @brief Worker to apply a blur filter to an image
    void* blur_worker(void* arg);

    /// @brief Worker to apply a sharpen filter to an image
    void* sharpen_worker(void* arg);

    /// @brief Worker to apply a sobel filter to an image
    void* sobel_worker(void* arg);
}

#endif // FILTERS_HPP
