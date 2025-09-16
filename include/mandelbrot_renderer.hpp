#pragma once

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "mandelbrot_sender.hpp"
#include "types.hpp"


namespace detail {


template <std::size_t I, std::size_t N>
constexpr PixelRegion make_region(const RenderSettings& s) noexcept {
    static_assert(I < N);
    const std::size_t base_rows  = s.height / N;
    const std::size_t remaining  = s.height % N;
    const std::uint32_t start    = static_cast<std::uint32_t>(base_rows * I
                                 + std::min<std::size_t>(I, remaining));

    const auto rows_in_tile = static_cast<std::uint32_t>(base_rows + (I < remaining ? 1 : 0));

    return PixelRegion{
        .start_row = start,
        .end_row   = static_cast<std::uint32_t>(start + rows_in_tile),
        .start_col = 0u,
        .end_col   = s.width
    };
}


inline ColorMatrix colorize(const PixelMatrix& pm, std::uint32_t max_iter) {
    const std::size_t h = pm.size();
    const std::size_t w = h ? pm.front().size() : 0;
    ColorMatrix cm(h, std::vector<mandelbrot::RgbColor>(w));
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x)
            cm[y][x] = mandelbrot::IterationsToColor(pm[y][x], max_iter);
    return cm;
}


template <std::size_t I, std::size_t N, class Scheduler>
auto tile_sender(Scheduler scheduler,
                 mandelbrot::ViewPort viewport,
                 RenderSettings settings) {
    using stdexec::on; using stdexec::then;
    const PixelRegion region = detail::make_region<I, N>(settings);
    return on(scheduler, MandelbrotSender<void>{viewport, settings, region})
         | then([region, settings](PixelMatrix pm) {
               auto cm = detail::colorize(pm, settings.max_iterations);
               return std::tuple{region, std::move(pm), std::move(cm)};
           });
}

template <std::size_t N, class Scheduler, std::size_t... Is>
auto build_when_all_indices(Scheduler scheduler,
                            mandelbrot::ViewPort viewport,
                            RenderSettings settings,
                            std::index_sequence<Is...>) {
    return stdexec::when_all(detail::tile_sender<Is, N>(scheduler, viewport, settings)...);
}

template <std::size_t N, class Scheduler>
auto build_when_all(Scheduler scheduler,
                    mandelbrot::ViewPort viewport,
                    RenderSettings settings) {
    return build_when_all_indices<N>(scheduler, viewport, settings, std::make_index_sequence<N>{});
}


inline RenderResult make_empty_result(mandelbrot::ViewPort viewport, const RenderSettings& s) {
    RenderResult r;
    r.viewport   = viewport;
    r.settings   = s;
    r.pixel_data = PixelMatrix(s.height, std::vector<std::uint32_t>(s.width));
    r.color_data = ColorMatrix(s.height, std::vector<mandelbrot::RgbColor>(s.width));
    return r;
}

template <class Part>
inline void merge_part(RenderResult& out, Part&& part) {
    auto [region, pm, cm] = std::forward<Part>(part);
    const std::size_t tile_h = pm.size();
    for (std::size_t y = 0; y < tile_h; ++y) {
        const std::size_t gy = static_cast<std::size_t>(region.start_row) + y;
        const std::size_t row_w = pm[y].size();
        for (std::size_t x = 0; x < row_w; ++x) {
            const std::size_t gx = static_cast<std::size_t>(region.start_col) + x;
            out.pixel_data[gy][gx] = pm[y][x];
            out.color_data[gy][gx] = cm[y][x];
        }
    }
}

}

class MandelbrotRenderer {
private:
    exec::static_thread_pool thread_pool_;

public:
    explicit MandelbrotRenderer(std::uint32_t num_threads = std::thread::hardware_concurrency())
        : thread_pool_{num_threads} {}

    template <size_t N>
    [[nodiscard]] auto RenderAsync(mandelbrot::ViewPort viewport, RenderSettings settings) {
        using stdexec::just;
        using stdexec::let_value;
        using stdexec::then;
        auto scheduler = thread_pool_.get_scheduler();

        return just(std::chrono::steady_clock::now())
            | stdexec::let_value([viewport, settings, scheduler](auto start_time) mutable {
                auto when_all_sender = detail::build_when_all<N>(scheduler, viewport, settings);
                    return std::move(when_all_sender) | stdexec::then([viewport, settings, start_time](auto&&... parts) {
                        RenderResult result = detail::make_empty_result(viewport, settings);
                        (detail::merge_part(result, std::forward<decltype(parts)>(parts)), ...);
                        result.render_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time);
                        return result;
                });
            });
    }
};
