#pragma once

#include <SFML/Graphics.hpp>
#include <print>
#include <stdexec/execution.hpp>
#include <utility>
#include <utility>

#include "types.hpp"

class SFMLRender {
public:
    template <typename Receiver>
    struct OperationState {
        using operation_state_concept = stdexec::operation_state_t;
        Receiver receiver_;
        RenderResult render_result_;
        sf::Image &image_;
        sf::Texture &texture_;
        sf::Sprite &sprite_;
        sf::RenderWindow &window_;
        RenderSettings render_settings_;

        void start() noexcept {
            try {
                exec();
                stdexec::set_value(std::move(receiver_));
            } catch (...) {
                stdexec::set_error(std::move(receiver_), std::current_exception());
            }
        }
    private:
        void exec() {
            if (not render_result_.color_data.empty()) {
                if (render_result_.color_data.size() != render_result_.pixel_data.size()) {
                    stdexec::set_error(std::move(receiver_),
                               std::make_exception_ptr(std::runtime_error{"Size of color data is not equal to size of pixel data"}));
                }
                for (uint32_t y = 0; y < render_settings_.height; ++y) {
                    for (uint32_t x = 0; x < render_settings_.width; ++x) {
                        auto color = sf::Color{
                            render_result_.color_data[y][x].r, 
                            render_result_.color_data[y][x].g, 
                            render_result_.color_data[y][x].b
                        };
                        image_.setPixel(x, y, color);
                    }
                }
                texture_.update(image_);
                window_.clear(sf::Color::Black);
                window_.draw(sprite_);
                window_.display();
            }
        }

    };

    RenderResult render_result_;
    sf::Image &image_;
    sf::Texture &texture_;
    sf::Sprite &sprite_;
    sf::RenderWindow &window_;
    RenderSettings render_settings_;

    SFMLRender(RenderResult render_result, sf::Image &image, sf::Texture &texture, sf::Sprite &sprite,
               sf::RenderWindow &window, const RenderSettings &render_settings)
        : render_result_(std::move(std::move(render_result))), image_{image}, texture_{texture}, sprite_{sprite}, window_{window},
          render_settings_{render_settings} {}

    auto connect(auto receiver) {
        return OperationState<decltype(receiver)>(std::move(receiver),
            render_result_, image_, texture_, sprite_, window_, render_settings_);
    }

    using sender_concept = stdexec::sender_t;
    using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::exception_ptr),
        stdexec::set_stopped_t()
    >;
};
