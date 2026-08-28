// FrameQueue -- thread-safe bounded queue of decoded video AVFrame*,
// connecting the decode thread (producer) to the render thread
// (consumer). See player.cpp for why this split exists: our earlier
// single-threaded design (decode and render serially on one thread)
// consistently failed to display anything despite every other part of
// the pipeline being individually verified correct, while a known-
// working Wii U media player (CafeMP) uses separate threads for
// reading/decoding and rendering. This is the connective piece between
// them.
//
// Frames are independently ref-counted clones (via av_frame_clone) --
// Decoder reuses a single internal AVFrame buffer across calls, so a
// frame must be cloned before being hhanded across the thread boundary,
// or its contents would be overwritten by the next decode before the
// render thread gets to it. The queue does not own frames long-term:
// whoever calls pop() is responsible for av_frame_free()'ing what they
// get back once done with it.

#pragma once
extern "C" {
#include <libavutil/frame.h>
}
#include <queue>
#include <mutex>
#include <condition_variable>

class FrameQueue {
public:
    static const size_t MAX_SIZE = 3;

    // Blocks if the queue is already full, which naturally paces the
    // decode thread to roughly the render thread's consumption rate
    // instead of decoding arbitrarily far ahead and growing memory
    // unbounded.
    void push(AVFrame* frame) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_full_.wait(lock, [this] { return queue_.size() < MAX_SIZE || stopped_; });
        if (stopped_) {
            av_frame_free(&frame);
            return;
        }
        queue_.push(frame);
        lock.unlock();
        cv_not_empty_.notify_one();
    }

    // Blocks until a frame is available or stop() is called. Returns
    // nullptr once stopped with nothing left to deliver -- the signal
    // to the render loop that it's time to exit.
    AVFrame* pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_not_empty_.wait(lock, [this] { return !queue_.empty() || stopped_; });
        if (queue_.empty()) return nullptr;
        AVFrame* frame = queue_.front();
        queue_.pop();
        lock.unlock();
        cv_not_full_.notify_one();
        return frame;
    }

    // Wakes any threads blocked in push()/pop() so they can exit
    // cleanly, and frees anything still queued.
    void stop() {
        std::unique_lock<std::mutex> lock(mtx_);
        stopped_ = true;
        while (!queue_.empty()) {
            AVFrame* f = queue_.front();
            queue_.pop();
            av_frame_free(&f);
        }
        lock.unlock();
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

private:
    std::queue<AVFrame*> queue_;
    std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    bool stopped_ = false;
};
