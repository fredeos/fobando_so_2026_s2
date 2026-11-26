// OpenCV compatibility layer. Algorithms live in the shared C11 core.
#include "filters.hpp"
#include "filters/filters.h"

#include <stdexcept>

namespace {
void* run_worker(void* argument, unsigned filter)
{
    auto* args = static_cast<filters::FilterArgs*>(argument);
    if (!args || !args->img || !args->args || args->workers <= 0 ||
        args->tid < 0 || args->tid >= args->workers)
        throw std::invalid_argument("Invalid filter worker arguments");
    unsigned radius;
    unsigned intensity = 2;
    if (filter == FILTER_BLUR) {
        radius = static_cast<unsigned>(static_cast<filters::BlurArgs*>(args->args)->radius);
    } else if (filter == FILTER_SHARPEN) {
        auto* parameters = static_cast<filters::SharpenArgs*>(args->args);
        radius = static_cast<unsigned>(parameters->radius);
        intensity = static_cast<unsigned>(parameters->intensity);
    } else {
        radius = static_cast<unsigned>(static_cast<filters::SobelArgs*>(args->args)->radius);
    }
    const cv::Mat& input = args->img->input;
    cv::Mat& output = args->img->output;
    if (!filters_valid(filter, radius, intensity) || input.empty() ||
        input.type() != CV_8UC3 || output.size() != input.size() ||
        output.type() != (filter == FILTER_SOBEL ? CV_8UC1 : CV_8UC3))
        throw std::invalid_argument("Invalid filter image or parameters");
    size_t width = static_cast<size_t>(input.cols);
    size_t height = static_cast<size_t>(input.rows);
    size_t area = (width > 2 * radius && height > 2 * radius)
                ? (width - 2 * radius) * (height - 2 * radius) : 0;
    size_t workers = static_cast<size_t>(args->workers);
    size_t tid = static_cast<size_t>(args->tid);
    size_t length = area / workers;
    size_t remainder = area % workers;
    size_t first = tid * length + (tid < remainder ? tid : remainder);
    size_t count = length + (tid < remainder ? 1 : 0);
    if (filters_apply_range(filter, input.data, output.data,
            static_cast<unsigned>(input.cols), static_cast<unsigned>(input.rows),
            input.step[0], output.step[0], radius, intensity, first, count) < 0)
        throw std::invalid_argument("Invalid filter buffers");
    return nullptr;
}
}

void* filters::blur_worker(void* argument) { return run_worker(argument, FILTER_BLUR); }
void* filters::sharpen_worker(void* argument) { return run_worker(argument, FILTER_SHARPEN); }
void* filters::sobel_worker(void* argument) { return run_worker(argument, FILTER_SOBEL); }
