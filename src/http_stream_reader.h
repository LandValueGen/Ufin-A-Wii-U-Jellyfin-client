// HttpStreamReader -- opens ONE persistent HTTP connection and lets the
// caller pull decoded body bytes incrementally as they arrive.
//
// Why this exists (separate from http_client.cpp): http_client.cpp reads
// an entire response into memory before returning anything, which is
// fine for small JSON API replies but was actively broken for this use
// case -- confirmed via curl that Jellyfin's transcode endpoint (a) does
// not support Range requests at all (Accept-Ranges: none) and (b)
// streams the response as an indefinite "200 OK" +
// "Transfer-Encoding: chunked" body with no Content-Length, since the
// video is being generated live as you watch. Trying to fully buffer
// that before returning is effectively "wait for the whole movie to
// finish transcoding," which looked exactly like a hang.
//
// This class instead keeps one connection open and hands back decoded
// chunk data as it arrives, blocking on the socket only as long as it
// takes for the next piece of data to show up -- which is what actual
// streaming playback needs. Tradeoff: no seeking, since a live chunked
// stream has no random access -- see media/http_stream_io.h for how
// that's surfaced to FFmpeg (as a non-seekable input, same handling
// FFmpeg already has for e.g. live broadcasts).

#pragma once
#include <cstdint>
#include <string>
#include <vector>

class HttpStreamReader {
public:
    HttpStreamReader(std::string host, int port, std::string path);
    ~HttpStreamReader();

    // Opens the connection, sends the GET request, and parses the
    // response status line + headers. Must return true (and see a 200)
    // before read() is usable.
    bool open();

    // Reads up to `len` bytes of decoded body data into buf, blocking on
    // the socket as needed for more data to arrive. Returns the number
    // of bytes actually read (may be less than len -- callers should
    // loop if they need an exact amount), 0 on a clean end of stream, or
    // -1 on a genuine error.
    int read(uint8_t* buf, int len);

    const std::string& lastError() const { return last_error_; }

private:
    std::string host_;
    int port_;
    std::string path_;
    int sock_ = -1;
    std::string last_error_;

    bool is_chunked_ = false;
    bool stream_ended_ = false;

    // Chunked-decoding state: bytes remaining in the chunk currently
    // being consumed, and whether we're at a boundary needing to read a
    // fresh "<hex size>\r\n" line before more data is available.
    int64_t chunk_remaining_ = 0;
    bool need_chunk_header_ = true;

    // Small raw-socket read buffer. recv() can (and will) return more
    // bytes than a single readLine()/readRawBytes() call asked for --
    // e.g. the tail of the header block and the start of the first
    // chunk often arrive in the same recv() -- so leftover bytes are
    // held here until subsequent calls consume them.
    std::vector<uint8_t> sock_buf_;
    size_t sock_buf_pos_ = 0;
    size_t sock_buf_len_ = 0;

    bool fillSockBuf();
    bool readLine(std::string& outLine);
    bool readRawBytes(uint8_t* buf, int n);
};
