#include "ui_keyfob_scan.hpp"
#include "portapack.hpp"
#include "audio.hpp"
#include "file.hpp"
#include "file_path.hpp"
#include "io_convert.hpp"
#include "capture_thread.hpp"
#include "replay_thread.hpp"
#include "oversample.hpp"
#include "transmitter_model.hpp"

using namespace portapack;

namespace ui {

KeyfobScanView::KeyfobScanView(NavigationView& nav)
    : nav_{nav} {
    add_children({&rssi, &field_frequency, &button_toggle, &button_transmit, &text_status});

    baseband::run_image(portapack::spi_flash::image_tag_subghzd);
    baseband::set_subghzd_config(0, receiver_model.sampling_rate());
    receiver_model.enable();

    field_frequency.set_step(100000);
    field_frequency.set_value(freqs_[freq_index_]);

    button_toggle.on_select = [this](Button&) { toggle_scanning(); };
    button_transmit.on_select = [this](Button&) { transmit_last(); };
    button_transmit.disabled(true);

    signal_token_tick_second = rtc_time::signal_tick_second += [this]() {
        this->on_tick_second();
    };
}

KeyfobScanView::~KeyfobScanView() {
    rtc_time::signal_tick_second -= signal_token_tick_second;
    stop_capture();
    stop_transmit();
    receiver_model.disable();
    baseband::shutdown();
}

void KeyfobScanView::focus() {
    button_toggle.focus();
}

void KeyfobScanView::toggle_scanning() {
    if (scanning_) {
        scanning_ = false;
        button_toggle.set_text("Start");
        text_status.set("Stopped");
    } else {
        stop_capture();
        stop_transmit();
        scanning_ = true;
        button_toggle.set_text("Stop");
        text_status.set("Scanning");
    }
}

void KeyfobScanView::on_tick_second() {
    if (capturing_) {
        if (capture_seconds_left_ > 0 && --capture_seconds_left_ == 0) {
            stop_capture();
        }
        return;
    }

    if (transmitting_)
        return;

    if (!scanning_)
        return;

    freq_index_ = (freq_index_ + 1) % freqs_.size();
    current_freq_ = freqs_[freq_index_];
    receiver_model.set_target_frequency(current_freq_);
    field_frequency.set_value(current_freq_);

    if (rssi.get_max() > 50) {
        text_status.set("Signal " + to_string_short_freq(current_freq_));
        baseband::request_audio_beep(2000, 24000, 60);
        start_capture();
    }
}

void KeyfobScanView::start_capture() {
    if (capture_thread_)
        return;

    ensure_directory(captures_dir);
    rtc::RTC now;
    rtc_time::now(now);
    last_capture_path_ = captures_dir / (std::string("KF_") + to_string_timestamp(now) + ".C8");

    auto writer = std::make_unique<FileConvertWriter>();
    auto error = writer->create(last_capture_path_);
    if (error) {
        text_status.set("SD error");
        return;
    }

    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_capture);
    baseband::set_sample_rate(capture_rate_, get_oversample_rate(capture_rate_));

    capture_thread_ = std::make_unique<CaptureThread>(
        std::move(writer), 0x4000, 3,
        []() {
            CaptureThreadDoneMessage message{};
            EventDispatcher::send_message(message);
        },
        [](File::Error error) {
            CaptureThreadDoneMessage message{error.code()};
            EventDispatcher::send_message(message);
        });

    capture_seconds_left_ = 1;
    capturing_ = true;
    text_status.set("Capturing");
}

void KeyfobScanView::stop_capture() {
    if (!capture_thread_)
        return;

    capture_thread_.reset();
    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_subghzd);
    baseband::set_subghzd_config(0, receiver_model.sampling_rate());
    receiver_model.enable();
    capturing_ = false;
    button_transmit.disabled(false);
    text_status.set("Captured");
}

void KeyfobScanView::transmit_last() {
    if (transmitting_ || last_capture_path_.empty())
        return;

    auto reader = std::make_unique<FileConvertReader>();
    auto error = reader->open(last_capture_path_);
    if (error) {
        text_status.set("Open error");
        return;
    }

    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_replay);
    baseband::set_sample_rate(capture_rate_, get_oversample_rate(capture_rate_));

    transmitter_model.set_target_frequency(current_freq_);
    transmitter_model.set_sampling_rate(get_actual_sample_rate(capture_rate_));
    transmitter_model.set_baseband_bandwidth(2'500'000);
    transmitter_model.enable();

    replay_thread_ = std::make_unique<ReplayThread>(
        std::move(reader), 0x4000, 3, &ready_signal_,
        [](uint32_t return_code) {
            ReplayThreadDoneMessage message{return_code};
            EventDispatcher::send_message(message);
        });

    transmitting_ = true;
    text_status.set("Transmitting");
}

void KeyfobScanView::stop_transmit() {
    if (!transmitting_)
        return;

    replay_thread_.reset();
    transmitter_model.disable();
    baseband::shutdown();
    baseband::run_image(portapack::spi_flash::image_tag_subghzd);
    baseband::set_subghzd_config(0, receiver_model.sampling_rate());
    receiver_model.enable();
    transmitting_ = false;
    ready_signal_ = false;
    text_status.set("Done");
}

}  // namespace ui
