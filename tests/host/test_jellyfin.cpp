// Exercises JellyfinClient (and the real http_client underneath it)
// against a fake Jellyfin server: authentication, browsing, item
// metadata, stream URL construction, and playback reporting.
#include "check.h"
#include "fake_http_server.h"
#include "jellyfin_client.h"

#include <chrono>
#include <thread>

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    FakeHttpServer server;
    CHECK(server.start());

    FakeRoute auth;
    auth.body = "{\"AccessToken\":\"tok123\",\"User\":{\"Id\":\"user1\",\"Name\":\"wiiu\"}}";
    server.addRoute("/Users/AuthenticateByName", auth);

    FakeRoute views;
    views.body = "{\"Items\":[{\"Id\":\"lib1\",\"Name\":\"Movies\",\"Type\":\"CollectionFolder\"},"
                 "{\"Id\":\"lib2\",\"Name\":\"Music\",\"Type\":\"CollectionFolder\"}],\"TotalRecordCount\":2}";
    server.addRoute("/Users/user1/Views", views);

    FakeRoute items;
    items.body = "{\"Items\":[{\"Id\":\"m1\",\"Name\":\"Some Movie\",\"Type\":\"Movie\",\"RunTimeTicks\":72000000000},"
                 "{\"Id\":\"a1\",\"Name\":\"Caf\xc3\xa9 Song\",\"Type\":\"Audio\",\"RunTimeTicks\":1850000000}]}";
    server.addRoute("/Users/user1/Items", items);

    FakeRoute movie;
    movie.body = "{\"Id\":\"m1\",\"Name\":\"Some Movie\",\"Width\":1920,\"Height\":804,\"RunTimeTicks\":72000000000,"
                 "\"MediaStreams\":[{\"Type\":\"Audio\",\"Codec\":\"ac3\"},"
                 "{\"Type\":\"Video\",\"Width\":1920,\"Height\":804,\"AspectRatio\":\"2.40:1\"}]}";
    server.addRoute("/Users/user1/Items/m1", movie);

    FakeRoute oldMovie; // no item-level aspect, only inside MediaSources
    oldMovie.body = "{\"Id\":\"m2\",\"Width\":640,\"Height\":480,"
                    "\"MediaSources\":[{\"MediaStreams\":[{\"Type\":\"Video\",\"Width\":640,\"Height\":480}]}]}";
    server.addRoute("/Users/user1/Items/m2", oldMovie);

    FakeRoute anamorphic; // AspectRatio disagrees with Width/Height -> AspectRatio wins
    anamorphic.body = "{\"Id\":\"m3\",\"Width\":720,\"Height\":576,"
                      "\"MediaStreams\":[{\"Type\":\"Video\",\"Width\":720,\"Height\":576,\"AspectRatio\":\"16:9\"}]}";
    server.addRoute("/Users/user1/Items/m3", anamorphic);

    FakeRoute ok;
    ok.body = "";
    ok.status = 204;
    server.addRoute("/Sessions/Playing", ok);
    server.addRoute("/Sessions/Playing/Progress", ok);
    server.addRoute("/Sessions/Playing/Stopped", ok);

    JellyfinClient client("127.0.0.1", server.port());

    // Authentication failure is reported, not swallowed.
    {
        JellyfinClient bad("127.0.0.1", server.port());
        FakeRoute denied;
        denied.status = 401;
        denied.body = "{\"error\":\"nope\"}";
        FakeHttpServer badServer;
        CHECK(badServer.start());
        badServer.addRoute("/Users/AuthenticateByName", denied);
        JellyfinClient bad2("127.0.0.1", badServer.port());
        CHECK(!bad2.authenticate("u", "p"));
        CHECK(contains(bad2.lastError(), "401"));
    }

    CHECK(client.authenticate("wiiu", "secret"));
    {
        auto reqs = server.requests();
        CHECK(!reqs.empty());
        CHECK_STR(reqs.back().method, "POST");
        CHECK(contains(reqs.back().body, "\"Username\":\"wiiu\""));
        CHECK(contains(reqs.back().body, "\"Pw\":\"secret\""));
        CHECK(contains(reqs.back().headers, "X-Emby-Authorization: MediaBrowser Client=\"Ufin\""));
    }

    std::vector<JellyfinItem> list;
    CHECK(client.getViews(list));
    CHECK_EQ((int)list.size(), 2);
    CHECK_STR(list[0].name, "Movies");
    CHECK_STR(list[1].type, "CollectionFolder");
    {
        auto reqs = server.requests();
        CHECK(contains(reqs.back().headers, "Token=\"tok123\""));
    }

    // getItems replaces the vector contents rather than appending.
    CHECK(client.getItems("lib1", list));
    CHECK_EQ((int)list.size(), 2);
    CHECK_STR(list[0].id, "m1");
    CHECK_EQ(list[0].runTimeTicks, (int64_t)72000000000LL);
    CHECK_STR(list[1].type, "Audio");
    CHECK_EQ(list[1].runTimeTicks, (int64_t)1850000000LL);
    {
        auto reqs = server.requests();
        CHECK(contains(reqs.back().path, "ParentId=lib1"));
    }

    VideoInfo info;
    CHECK(client.getVideoInfo("m1", info));
    CHECK_EQ(info.width, 1920);
    CHECK_EQ(info.height, 804);
    CHECK_NEAR(info.displayAspect, 2.40, 0.001);
    CHECK_EQ(info.runTimeTicks, (int64_t)72000000000LL);

    CHECK(client.getVideoInfo("m2", info));
    CHECK_NEAR(info.displayAspect, 4.0 / 3.0, 0.001);
    CHECK_EQ(info.runTimeTicks, (int64_t)0);

    CHECK(client.getVideoInfo("m3", info));
    CHECK_NEAR(info.displayAspect, 16.0 / 9.0, 0.001);

    CHECK(!client.getVideoInfo("missing", info));
    CHECK(contains(client.lastError(), "404"));

    // Stream URLs.
    StreamTarget audio = client.buildAudioStreamUrl("a1");
    CHECK_STR(audio.host, "127.0.0.1");
    CHECK_EQ(audio.port, server.port());
    CHECK(contains(audio.path, "/Audio/a1/stream.mp4?"));
    CHECK(contains(audio.path, "AudioCodec=aac"));
    CHECK(contains(audio.path, "api_key=tok123"));

    StreamTarget video = client.buildVideoStreamUrl("m1");
    CHECK(contains(video.path, "/Videos/m1/stream.mp4?"));
    CHECK(contains(video.path, "&Width=1280&Height=720&"));
    CHECK(!contains(video.path, "MaxWidth"));
    CHECK(contains(video.path, "VideoBitrate=2500000"));
    CHECK(contains(video.path, "Profile=baseline"));
    CHECK(contains(video.path, "Level=41"));
    CHECK(contains(video.path, "MaxFramerate=30"));
    CHECK(contains(video.path, "MaxAudioChannels=2"));
    CHECK(contains(video.path, "AllowVideoStreamCopy=false"));
    CHECK(contains(video.path, "AllowAudioStreamCopy=false"));
    CHECK(contains(video.path, "api_key=tok123"));

    VideoStreamOptions opts;
    opts.videoBitrate = 4000000;
    opts.profile = "High";
    video = client.buildVideoStreamUrl("m1", opts);
    CHECK(contains(video.path, "VideoBitrate=4000000"));
    CHECK(contains(video.path, "Profile=high"));

    opts.videoBitrate = 10;                    // clamped up
    opts.profile = "ultra&evil=1";             // unknown -> baseline, nothing injected
    video = client.buildVideoStreamUrl("m1", opts);
    CHECK(contains(video.path, "VideoBitrate=300000"));
    CHECK(contains(video.path, "Profile=baseline"));
    CHECK(!contains(video.path, "evil"));

    opts.videoBitrate = 999999999;             // clamped down
    video = client.buildVideoStreamUrl("m1", opts);
    CHECK(contains(video.path, "VideoBitrate=20000000"));

    // Playback reporting posts JSON with the position in ticks.
    CHECK(client.reportPlaybackStart("m1"));
    CHECK(client.reportPlaybackProgress("m1", 123450000));
    CHECK(client.reportPlaybackStopped("m1", 987650000));
    {
        auto reqs = server.requests();
        CHECK((int)reqs.size() >= 3);
        const FakeRequest& start = reqs[reqs.size() - 3];
        const FakeRequest& progress = reqs[reqs.size() - 2];
        const FakeRequest& stopped = reqs[reqs.size() - 1];
        CHECK_STR(start.path, "/Sessions/Playing");
        CHECK(contains(start.body, "\"ItemId\":\"m1\""));
        CHECK(!contains(start.body, "PositionTicks"));
        CHECK_STR(progress.path, "/Sessions/Playing/Progress");
        CHECK(contains(progress.body, "\"PositionTicks\":123450000"));
        CHECK(contains(progress.body, "\"PlayMethod\":\"Transcode\""));
        CHECK_STR(stopped.path, "/Sessions/Playing/Stopped");
        CHECK(contains(stopped.body, "\"PositionTicks\":987650000"));
    }

    server.stop();
    return check::finish("test_jellyfin");
}
