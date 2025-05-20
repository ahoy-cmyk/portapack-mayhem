#ifndef __UI_KEYFOB_SCAN_H__
#define __UI_KEYFOB_SCAN_H__

#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_freq_field.hpp"
#include "ui_rssi.hpp"
#include "string_format.hpp"
#include "rtc_time.hpp"
#include "baseband_api.hpp"
#include "capture_thread.hpp"
#include "replay_thread.hpp"
#include "io_convert.hpp"
#include "oversample.hpp"
#include "file_path.hpp"
#include "transmitter_model.hpp"

namespace ui {

class KeyfobScanView : public View {
   public:
    KeyfobScanView(NavigationView& nav);
    ~KeyfobScanView();
    void focus() override;
    std::string title() const override { return "Keyfob Scan"; }

   private:
    void on_tick_second();
    void toggle_scanning();
    void start_capture();
    void stop_capture();
    void transmit_last();
    void stop_transmit();

    NavigationView& nav_;
    bool scanning_{false};
    bool capturing_{false};
    bool transmitting_{false};
    size_t freq_index_{0};
    uint32_t capture_seconds_left_{0};
    uint32_t capture_rate_{2'000'000};
    rf::Frequency current_freq_{0};
    std::filesystem::path last_capture_path_{};
    bool ready_signal_{false};
    std::unique_ptr<CaptureThread> capture_thread_{};
    std::unique_ptr<ReplayThread> replay_thread_{};
    rtc_time::SignalToken signal_token_tick_second{};
    MessageHandlerRegistration message_handler_replay_done{
        Message::ID::ReplayThreadDone,
        [this](const Message* const p) {
            (void)p;
            this->stop_transmit();
        }};
    MessageHandlerRegistration message_handler_fifo_signal{
        Message::ID::RequestSignal,
        [this](const Message* const p) {
            const auto message = static_cast<const RequestSignalMessage*>(p);
            if (message->signal == RequestSignalMessage::Signal::FillRequest)
                ready_signal_ = true;
        }};

    std::array<rf::Frequency, 6> freqs_{
        {305000000, 315000000, 390000000, 433920000, 868300000, 915000000}};

    RSSI rssi{{8, 3 * 16, 30 * 8, 16}, false};
    RxFrequencyField field_frequency{{2 * 8, 1 * 16}, nav};
    Button button_toggle{{2 * 8, 5 * 16, 8 * 16, 32}, "Start"};
    Button button_transmit{{14 * 8, 5 * 16, 8 * 16, 32}, "Transmit"};
    Text text_status{{2 * 8, 7 * 16, 30 * 8, 16}, "Stopped"};
};

}  // namespace ui

#endif  // __UI_KEYFOB_SCAN_H__
