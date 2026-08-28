#include "http_stream_io.h"

HttpStreamIO::HttpStreamIO(std::string host, int port, std::string path)
    : reader_(std::move(host), port, std::move(path)) {}

HttpStreamIO::~HttpStreamIO() {
    if (avio_ctx_) {
        // avio_ctx_->buffer may have been reallocated internally by
        // FFmpeg since we handed it avio_internal_buffer_ -- always free
        // via the context's own pointer, not our original one.
        av_free(avio_ctx_->buffer);
        avio_context_free(&avio_ctx_);
    }
}

bool HttpStreamIO::open() {
    if (!reader_.open()) {
        last_error_ = "HttpStreamReader::open failed: " + reader_.lastError();
        return false;
    }

    const int avioBufferSize = 32 * 1024; // FFmpeg's own conventional default
    avio_internal_buffer_ = (uint8_t*)av_malloc(avioBufferSize);

    avio_ctx_ = avio_alloc_context(
        avio_internal_buffer_, avioBufferSize,
        0, // write_flag: 0 = read-only
        this,
        &HttpStreamIO::readPacketTrampoline,
        nullptr, // no write callback, we're read-only
        &HttpStreamIO::seekTrampoline
    );

    if (!avio_ctx_) {
        last_error_ = "avio_alloc_context failed";
        return false;
    }

    // See the header comment: this is a genuinely non-seekable live
    // stream, not a workaround.
    avio_ctx_->seekable = 0;

    return true;
}

int HttpStreamIO::readPacket(uint8_t* buf, int bufSize) {
    int n = reader_.read(buf, bufSize);
    if (n < 0) return AVERROR(EIO);
    if (n == 0) return AVERROR_EOF;
    return n;
}

int64_t HttpStreamIO::seek(int64_t offset, int whence) {
    // Not supported -- see header comment. Returning -1 tells FFmpeg
    // "can't do this," including for AVSEEK_SIZE (we genuinely don't
    // know the total size either, since there's no Content-Length on a
    // chunked live-transcode response).
    return -1;
}

int HttpStreamIO::readPacketTrampoline(void* opaque, uint8_t* buf, int bufSize) {
    return static_cast<HttpStreamIO*>(opaque)->readPacket(buf, bufSize);
}

int64_t HttpStreamIO::seekTrampoline(void* opaque, int64_t offset, int whence) {
    return static_cast<HttpStreamIO*>(opaque)->seek(offset, whence);
}
