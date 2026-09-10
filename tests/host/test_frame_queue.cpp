#include "check.h"
#include "media/frame_queue.h"

#include <atomic>
#include <thread>

static AVFrame* makeFrame() {
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_NV12;
    f->width = 64;
    f->height = 32;
    av_frame_get_buffer(f, 0);
    return f;
}

int main() {
    // Order and timestamps survive the trip; finish() still delivers.
    {
        FrameQueue q;
        for (int i = 0; i < 3; i++) q.push(makeFrame(), i * 0.5);
        CHECK_EQ((int)q.size(), 3);
        q.finish();
        CHECK(!q.isDrained());
        for (int i = 0; i < 3; i++) {
            QueuedVideoFrame f;
            CHECK(q.pop(f, 10));
            CHECK_NEAR(f.pts, i * 0.5, 1e-9);
            CHECK(f.frame != nullptr);
            av_frame_free(&f.frame);
        }
        QueuedVideoFrame none;
        CHECK(!q.pop(none, 10));
        CHECK(q.isDrained());
        // Pushing after finish is harmless (frame is freed, not queued).
        q.push(makeFrame(), 9.0);
        CHECK_EQ((int)q.size(), 0);
    }

    // pop() times out on an empty, still-open queue.
    {
        FrameQueue q;
        QueuedVideoFrame f;
        CHECK(!q.pop(f, 20));
        CHECK(!q.isDrained());
    }

    // stop() drops everything and unblocks a producer stuck on a full queue.
    {
        FrameQueue q;
        for (size_t i = 0; i < FrameQueue::MAX_SIZE; i++) q.push(makeFrame(), 0.0);
        std::atomic<bool> producerDone{false};
        std::thread producer([&] {
            q.push(makeFrame(), 1.0); // blocks: queue is full
            producerDone = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(!producerDone);
        q.stop();
        producer.join();
        CHECK(producerDone);
        CHECK_EQ((int)q.size(), 0);
        QueuedVideoFrame f;
        CHECK(!q.pop(f, 10));
        CHECK(q.isDrained());
    }

    // A blocked producer resumes when the consumer pops.
    {
        FrameQueue q;
        for (size_t i = 0; i < FrameQueue::MAX_SIZE; i++) q.push(makeFrame(), (double)i);
        std::atomic<int> pushed{0};
        std::thread producer([&] {
            q.push(makeFrame(), 100.0);
            pushed = 1;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK_EQ(pushed.load(), 0);
        QueuedVideoFrame f;
        CHECK(q.pop(f, 10));
        CHECK_NEAR(f.pts, 0.0, 1e-9);
        av_frame_free(&f.frame);
        producer.join();
        CHECK_EQ(pushed.load(), 1);
        CHECK_EQ((int)q.size(), (int)FrameQueue::MAX_SIZE);
        q.stop();
    }

    return check::finish("test_frame_queue");
}
