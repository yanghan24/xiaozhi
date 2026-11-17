#pragma once

#include <atomic>
#include <cstdint>

// Audio starts hold a lease until their active state is visible. In-flight
// codec/driver work holds one until completion. Maintenance atomically freezes
// admission before sampling idle, so neither kind of activity can cross it.
class AudioActivityGate {
public:
    class Lease {
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : gate_(other.gate_) { other.gate_ = nullptr; }
        ~Lease() {
            if (gate_)
                gate_->state_.fetch_sub(1);
        }
        explicit operator bool() const { return gate_ != nullptr; }

    private:
        friend class AudioActivityGate;
        explicit Lease(AudioActivityGate* gate) : gate_(gate) {}
        AudioActivityGate* gate_;
    };

    Lease TryBegin() {
        auto state = state_.load();
        while (state < kReserved - 1) {
            if (state_.compare_exchange_weak(state, state + 1))
                return Lease(this);
        }
        return Lease(nullptr);
    }

    bool TryReserve() {
        uint32_t idle = 0;
        return state_.compare_exchange_strong(idle, kReserved);
    }

    void Release() {
        uint32_t reserved = kReserved;
        state_.compare_exchange_strong(reserved, 0);
    }

    bool Quiescent() const {
        const auto state = state_.load();
        return state == 0 || state == kReserved;
    }

private:
    static constexpr uint32_t kReserved = uint32_t{1} << 31;
    std::atomic<uint32_t> state_{0};
};
