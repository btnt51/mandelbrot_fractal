#pragma once

#include <stdexec/execution.hpp>

#include "types.hpp"

template <typename Receiver>
struct MandelbrotOperationState {
    using operation_state_concept = stdexec::operation_state_t;
    void start() noexcept {
        try {
            const auto height_size = region_.end_row - region_.start_row;
            const auto width_size = region_.end_col - region_.start_col;
            PixelMatrix out{height_size, std::vector<uint32_t>(width_size, 0)};
            for (auto pixel_y = region_.start_row; pixel_y < region_.end_row; ++pixel_y) {
                for (auto pixel_x = region_.start_col; pixel_x < region_.end_col; ++pixel_x) {
                    auto complex_number = mandelbrot::Pixel2DToComplex(pixel_x, pixel_y, viewport_, settings_.width, settings_.height);
                    const auto iteration_for_point = mandelbrot::CalculateIterationsForPoint(complex_number, settings_.max_iterations, settings_.escape_radius);
                    out[pixel_y - region_.start_row][pixel_x - region_.start_col] = iteration_for_point;
                }
            }
            stdexec::set_value(std::move(receiver_), std::move(out));
        } catch (...) {
            stdexec::set_error(std::move(receiver_), std::current_exception());
        }
    }
    Receiver receiver_;
    mandelbrot::ViewPort viewport_;
    RenderSettings settings_;
    PixelRegion region_;
};

template <typename Receiver>
struct MandelbrotSender {
    mandelbrot::ViewPort viewport_;
    RenderSettings settings_;
    PixelRegion region_;

    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(PixelMatrix&&),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()
    >;

    auto connect(auto receiver) {
        return MandelbrotOperationState<decltype(receiver)>(std::move(receiver), viewport_, settings_, region_);
    }
};

[[nodiscard]] inline auto MakeMandelbrotSender(mandelbrot::ViewPort viewport, RenderSettings settings,
                                               PixelRegion region) {
    return MandelbrotSender<void>{viewport, settings, region};
}
