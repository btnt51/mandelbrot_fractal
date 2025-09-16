#pragma once

#include "mandelbrot_renderer.hpp"

#include <stdexec/execution.hpp>
#include <chrono>
#include <memory>
#include <utility>

struct AnyOpState {
    virtual void start() noexcept = 0;
    virtual ~AnyOpState() = default;
};

template <class Inner>
struct OpModel : AnyOpState {
    alignas(Inner) unsigned char storage_[sizeof(Inner)];
    Inner* ptr_ = nullptr;

    template <class Sender, class Receiver>
    OpModel(std::in_place_t, Sender&& snd, Receiver&& rcv) {
        ptr_ = ::new (static_cast<void*>(storage_))
            Inner(stdexec::connect(std::forward<Sender>(snd),
                                   std::forward<Receiver>(rcv)));
    }

    void start() noexcept override {
        stdexec::start(*ptr_);
    }

    ~OpModel() {
        if (ptr_) {
            std::destroy_at(ptr_);
        }
    }

    OpModel(const OpModel&) = delete;
    OpModel& operator=(const OpModel&) = delete;
    OpModel(OpModel&&) = delete;
    OpModel& operator=(OpModel&&) = delete;
};


class CalculateMandelbrotAsyncSender {
public:
    explicit CalculateMandelbrotAsyncSender(AppState& state,
                                            RenderSettings render_settings,
                                            MandelbrotRenderer& renderer)
        : render_settings_{render_settings}
        , renderer_{renderer}
        , state_{state} {}

    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(RenderResult),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()
    >;

    template <typename Receiver>
    struct OperationState {
        using operation_state_concept = stdexec::operation_state_t;

        Receiver            receiver_;
        RenderSettings      render_settings_;
        MandelbrotRenderer& renderer_;
        AppState&           state_;

        std::unique_ptr<AnyOpState> inner_;

        OperationState(Receiver&& rcv,
                       RenderSettings rs,
                       MandelbrotRenderer& rend,
                       AppState& st)
            : receiver_(std::move(rcv)), render_settings_(rs)
            , renderer_(rend)
            , state_(st) {}

        template <class S, class R>
        static std::unique_ptr<AnyOpState> make_opstate(S&& s, R&& r) {
            using inner_t = stdexec::connect_result_t<S, R>;
            return std::unique_ptr<AnyOpState>(
                new OpModel<inner_t>(std::in_place,
                                     std::forward<S>(s),
                                     std::forward<R>(r)));
        }

        void start() noexcept {
            try {
                if (state_.need_rerender) {
                    auto snd =
                        renderer_.template RenderAsync<THREAD_POOL_SIZE>(
                            state_.viewport, render_settings_)
                        | stdexec::then([st = &state_](RenderResult r) noexcept {
                              st->need_rerender = false;
                              return r;
                          });

                    inner_ = make_opstate(std::move(snd), std::move(receiver_));
                    inner_->start();
                    return;
                }

                RenderResult out;
                out.viewport    = state_.viewport;
                out.settings    = render_settings_;
                out.render_time = std::chrono::milliseconds{0};
                stdexec::set_value(std::move(receiver_), std::move(out));
            } catch (...) {
                stdexec::set_error(std::move(receiver_), std::current_exception());
            }
        }
    };

    template <typename Receiver>
    auto connect(Receiver receiver) {
        return OperationState<Receiver>(std::move(receiver),
                                        render_settings_,
                                        renderer_,
                                        state_);
    }

private:
    RenderSettings      render_settings_;
    MandelbrotRenderer& renderer_;
    AppState&           state_;
};