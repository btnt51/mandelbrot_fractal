#include <gtest/gtest.h>

#include "mandelbrot.hpp"
#include "mandelbrot_renderer.hpp"
#include "mandelbrot_sender.hpp"
#include "types.hpp"

#include <gtest/gtest.h>
#include <stdexec/execution.hpp>
#include <future>
#include <chrono>

template <class Sender, class Rep, class Period>
auto sync_wait_for(Sender&& s, const std::chrono::duration<Rep, Period>& d) {
    using R = decltype(stdexec::sync_wait(std::forward<Sender>(s)));
    auto fut = std::async(std::launch::async,
                          [snd = std::forward<Sender>(s)]() mutable -> R {
                              return stdexec::sync_wait(std::move(snd));
                          });
    if (fut.wait_for(d) != std::future_status::ready) {
        ADD_FAILURE() << "Sender did not complete within timeout.";
        throw std::runtime_error("sync_wait_for timeout");
    }
    return fut.get();
}

static constexpr auto kTimeout = std::chrono::seconds(2);

TEST(Mandelbrot, MandelbrotSender) {
    const RenderSettings settings{.width = 64, .height = 32};
    const PixelRegion region{
        .start_row = 0, .end_row = 32,
        .start_col = 0, .end_col = 64
    };

    auto result = sync_wait_for(
        MakeMandelbrotSender(mandelbrot::ViewPort{}, settings, region),
        kTimeout
    );


    ASSERT_TRUE(result.has_value()) << "sync_wait returned empty optional";
    auto& [pixel_matrix] = *result;

    ASSERT_EQ(pixel_matrix.size(), settings.height) << "Unexpected rows count";
    ASSERT_FALSE(pixel_matrix.empty());
    ASSERT_EQ(pixel_matrix.front().size(), settings.width) << "Unexpected cols count";
}

TEST(Mandelbrot, MandelbrotRenderer) {
    const RenderSettings settings{.width = 64, .height = 32};

    auto result = sync_wait_for(
        MandelbrotRenderer{8}.RenderAsync<4>(mandelbrot::ViewPort{}, settings),
        kTimeout
    );

    ASSERT_TRUE(result.has_value()) << "sync_wait returned empty optional";
    auto& [render_result] = *result;


    ASSERT_EQ(render_result.color_data.size(), settings.height);
    ASSERT_FALSE(render_result.color_data.empty());
    ASSERT_EQ(render_result.color_data.front().size(), settings.width);

    ASSERT_EQ(render_result.pixel_data.size(), settings.height);
    ASSERT_FALSE(render_result.pixel_data.empty());
    ASSERT_EQ(render_result.pixel_data.front().size(), settings.width);
}

TEST(Mandelbrot, CalculateMandelbrotAsync_Behavior) {
    const RenderSettings cfg{.width = 64, .height = 32};
    AppState state{};
    MandelbrotRenderer renderer{8};

    auto run_once = [&](const char* phase) -> RenderResult {
        SCOPED_TRACE(phase);
        auto opt = sync_wait_for(CalculateMandelbrotAsyncSender{state, cfg, renderer}, kTimeout);

        if (!opt.has_value()) {
            ADD_FAILURE() << "sender returned empty optional";
            return RenderResult{}; // безопасный дефолт, чтобы не ломать контроль потоков
        }

        // opt: std::optional<std::tuple<RenderResult>>
        return std::get<0>(*opt);
    };

    auto expect_full_frame = [&](const RenderResult& rr) {
        ASSERT_EQ(rr.pixel_data.size(),  cfg.height) << "pixel_data rows mismatch";
        ASSERT_EQ(rr.color_data.size(),  cfg.height) << "color_data rows mismatch";
        ASSERT_FALSE(rr.pixel_data.empty());
        ASSERT_FALSE(rr.color_data.empty());
        ASSERT_EQ(rr.pixel_data.front().size(), cfg.width) << "pixel_data cols mismatch";
        ASSERT_EQ(rr.color_data.front().size(), cfg.width) << "color_data cols mismatch";
    };

    auto expect_empty_frame = [&](const RenderResult& rr) {
        EXPECT_TRUE(rr.pixel_data.empty()) << "pixel_data must be empty when no rerender requested";
        EXPECT_TRUE(rr.color_data.empty()) << "color_data must be empty when no rerender requested";
    };

    {
        auto rr = run_once("phase#1");
        expect_full_frame(rr);
        EXPECT_FALSE(state.need_rerender) << "flag must be cleared after successful render";
    }

    {
        auto rr = run_once("phase#2");
        expect_empty_frame(rr);
        EXPECT_FALSE(state.need_rerender);
    }

    state.need_rerender = true;
    {
        auto rr = run_once("phase#3");
        expect_full_frame(rr);
        EXPECT_FALSE(state.need_rerender);
    }
}


TEST(Mandelbrot, MandelbrotRenderer_TilingRemainder) {
    const RenderSettings settings{.width = 33, .height = 10};
    auto result = sync_wait_for(
        MandelbrotRenderer{8}.RenderAsync<3>(mandelbrot::ViewPort{}, settings),
        kTimeout
    );
    ASSERT_TRUE(result.has_value());
    auto& [render_result] = *result;

    ASSERT_EQ(render_result.pixel_data.size(), settings.height);
    ASSERT_EQ(render_result.pixel_data.front().size(), settings.width);
    ASSERT_EQ(render_result.color_data.size(), settings.height);
    ASSERT_EQ(render_result.color_data.front().size(), settings.width);

    for (const auto& row : render_result.pixel_data) {
        EXPECT_EQ(row.size(), settings.width);
    }
    for (const auto& row : render_result.color_data) {
        EXPECT_EQ(row.size(), settings.width);
    }
}

TEST(Mandelbrot, DeterministicOutput) {
    const RenderSettings settings{.width = 64, .height = 32};
    auto r1 = sync_wait_for(MandelbrotRenderer{8}.RenderAsync<4>(mandelbrot::ViewPort{}, settings), kTimeout);
    auto r2 = sync_wait_for(MandelbrotRenderer{8}.RenderAsync<4>(mandelbrot::ViewPort{}, settings), kTimeout);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    auto& [a] = *r1;
    auto& [b] = *r2;

    ASSERT_EQ(a.pixel_data.size(), b.pixel_data.size());
    ASSERT_EQ(a.color_data.size(), b.color_data.size());
    if (!a.pixel_data.empty()) {
        EXPECT_EQ(a.pixel_data.front(), b.pixel_data.front());
        EXPECT_EQ(a.pixel_data.back(),  b.pixel_data.back());
    }
    if (!a.color_data.empty()) {
        EXPECT_EQ(a.color_data.front(), b.color_data.front());
        EXPECT_EQ(a.color_data.back(),  b.color_data.back());
    }
}

TEST(Mandelbrot, ZeroSizeIsEmptyAndFast) {
    const RenderSettings settings{.width = 0, .height = 0};
    auto result = sync_wait_for(
        MandelbrotRenderer{4}.RenderAsync<2>(mandelbrot::ViewPort{}, settings),
        kTimeout
    );
    ASSERT_TRUE(result.has_value());
    auto& [rr] = *result;
    EXPECT_TRUE(rr.pixel_data.empty());
    EXPECT_TRUE(rr.color_data.empty());
}

TEST(Mandelbrot, RerenderOnViewportChange) {
    AppState app_state;
    MandelbrotRenderer renderer{8};
    const RenderSettings settings{.width = 64, .height = 32};

    auto r1 = sync_wait_for(CalculateMandelbrotAsyncSender{app_state, settings, renderer}, kTimeout);
    ASSERT_TRUE(r1.has_value());
    auto& [rr1] = *r1;
    ASSERT_FALSE(rr1.pixel_data.empty());
    ASSERT_FALSE(rr1.color_data.empty());
    EXPECT_FALSE(app_state.need_rerender);

    auto r2 = sync_wait_for(CalculateMandelbrotAsyncSender{app_state, settings, renderer}, kTimeout);
    ASSERT_TRUE(r2.has_value());
    auto& [rr2] = *r2;
    EXPECT_TRUE(rr2.pixel_data.empty());
    EXPECT_TRUE(rr2.color_data.empty());
    EXPECT_FALSE(app_state.need_rerender);

    app_state.viewport.x_min += 0.1;
    app_state.viewport.x_max += 0.1;
    app_state.viewport.y_min -= 0.1;
    app_state.viewport.y_max -= 0.1;

    app_state.need_rerender = true;

    auto r3 = sync_wait_for(CalculateMandelbrotAsyncSender{app_state, settings, renderer}, kTimeout);
    ASSERT_TRUE(r3.has_value());
    auto& [rr3] = *r3;
    EXPECT_FALSE(rr3.pixel_data.empty());
    EXPECT_FALSE(rr3.color_data.empty());
    EXPECT_FALSE(app_state.need_rerender);
}

TEST(Mandelbrot, MakeRegionPartitioning) {
    const RenderSettings s{.width = 64, .height = 19};
    constexpr std::size_t N = 4;

    const auto r0 = detail::make_region<0, N>(s);
    const auto r1 = detail::make_region<1, N>(s);
    const auto r2 = detail::make_region<2, N>(s);
    const auto r3 = detail::make_region<3, N>(s);

    EXPECT_EQ(r0.start_col, 0u); EXPECT_EQ(r0.end_col, s.width);
    EXPECT_EQ(r1.start_col, 0u); EXPECT_EQ(r1.end_col, s.width);
    EXPECT_EQ(r2.start_col, 0u); EXPECT_EQ(r2.end_col, s.width);
    EXPECT_EQ(r3.start_col, 0u); EXPECT_EQ(r3.end_col, s.width);

    EXPECT_EQ(r0.end_row,   r1.start_row);
    EXPECT_EQ(r1.end_row,   r2.start_row);
    EXPECT_EQ(r2.end_row,   r3.start_row);
    EXPECT_EQ(r3.end_row,   s.height);

    EXPECT_LE(r0.start_row, r0.end_row);
    EXPECT_LE(r1.start_row, r1.end_row);
    EXPECT_LE(r2.start_row, r2.end_row);
    EXPECT_LE(r3.start_row, r3.end_row);

    const auto sum =
        (r0.end_row - r0.start_row) +
        (r1.end_row - r1.start_row) +
        (r2.end_row - r2.start_row) +
        (r3.end_row - r3.start_row);
    EXPECT_EQ(sum, s.height);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
