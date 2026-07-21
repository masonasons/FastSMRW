#include "check.hpp"

#include "fastsm/fastsm.hpp"
#include "fastsm/net/http_client.hpp"

#include <cstring>

// Test entry point. Individual test functions live in test_*.cpp files and are
// declared + invoked here.

// From test_models.cpp
void test_status_roundtrip();
void test_timeline_item_roundtrip();
void test_codec_corrupt_is_miss();
void test_row_identity_is_stable();

// From test_util.cpp
void test_html_stripping();
void test_strip_quote_url();
void test_entity_decoding();
void test_date_parsing();
void test_relative_dates();
void test_demojify();
void test_truncate_mentions();

// From test_mastodon_map.cpp
void test_mastodon_status_mapping();
void test_mastodon_notification_mapping();
void test_mastodon_notification_group_mapping();
void test_mastodon_quote_mapping();
void test_mark_remote();
void test_remote_timeline_source();
void test_form_encode();

// From test_bluesky_map.cpp
void test_bluesky_feed_mapping();
void test_bluesky_plain_post();
void test_bluesky_notification_mapping();
void test_bluesky_facet_mapping();
void test_bluesky_embed_external_and_video();
void test_bluesky_embed_record_with_media();
void test_bluesky_labels_content_warning();

// From test_bluesky_richtext.cpp
void test_bluesky_facets_link();
void test_bluesky_facets_link_trailing_punct();
void test_bluesky_facets_bare_www();
void test_bluesky_facets_mention();
void test_bluesky_facets_mention_needs_domain();
void test_bluesky_facets_tag();
void test_bluesky_facets_utf8_offsets();
void test_bluesky_facets_mixed();

// From test_auth.cpp
void test_instance_normalization();

// From test_store.cpp
void test_base64();
void test_timeline_cache();

// From test_presentation.cpp
void test_presenter_compact();
void test_presenter_boost_compact();
void test_presenter_accessibility_all_fields();
void test_presenter_accessibility_default_config();
void test_presenter_grouped_notification();
void test_presenter_cw_modes();
void test_presenter_demojify_and_mentions();
void test_presenter_wrap_and_separator();
void test_presenter_stats_nonzero();
void test_presenter_poll();
void test_reply_participants();
void test_post_links();

// From test_speech.cpp
void test_speech_defaults();
void test_speech_normalized();
void test_settings_roundtrip();

// From test_sse.cpp
void test_sse_basic();
void test_sse_split_across_feeds();
void test_sse_multiline_crlf_comments();

// From test_capi.cpp
void test_capi_session_events();

// From test_thread.cpp
void test_mastodon_thread_fetch();
void test_mastodon_thread_folding();
void test_mastodon_instance_max_chars();
void test_mastodon_user_pinned_posts();

// From test_keymap.cpp
void test_keymap_normalize();
void test_keymap_default_and_catalog();
void test_keymap_parse_and_serialize();
void test_keymap_inheritance();

// From test_update.cpp
void test_update_version_compare();
void test_update_stable_branch();
void test_update_latest_branch();
void test_update_installer_asset();

// From test_filters.cpp
void test_client_filter_post_types();
void test_client_filter_media_and_me();
void test_client_filter_text();
void test_server_filter_metadata();

// From test_timeline_refresh.cpp
void test_refresh_fills_gap_below_streamed_top();
void test_refresh_steady_state_stops_early();
void test_refresh_fills_multipage_gap();
void test_refresh_keeps_updated_conversation();
void test_lost_row_keeps_reading_position();
void test_position_hint_falls_back_to_nearest();

static void test_version() {
    CHECK(fastsm::version() != nullptr);
    CHECK(std::strlen(fastsm::version()) > 0);
}

static void test_http_header_lookup() {
    fastsm::net::HttpResponse res;
    res.status = 200;
    res.headers = {{"Content-Type", "application/json"}, {"Link", "<x>; rel=\"next\""}};

    CHECK(res.ok());
    // Case-insensitive lookup.
    CHECK(res.header("content-type").has_value());
    CHECK_EQ(res.header("content-type").value(), std::string("application/json"));
    CHECK(!res.header("missing").has_value());
}

int main() {
    test_version();
    test_http_header_lookup();
    test_status_roundtrip();
    test_timeline_item_roundtrip();
    test_codec_corrupt_is_miss();
    test_row_identity_is_stable();
    test_html_stripping();
    test_strip_quote_url();
    test_entity_decoding();
    test_date_parsing();
    test_relative_dates();
    test_demojify();
    test_truncate_mentions();
    test_mastodon_status_mapping();
    test_mastodon_quote_mapping();
    test_mastodon_notification_mapping();
    test_mastodon_notification_group_mapping();
    test_mark_remote();
    test_remote_timeline_source();
    test_form_encode();
    test_bluesky_feed_mapping();
    test_bluesky_plain_post();
    test_bluesky_notification_mapping();
    test_bluesky_facet_mapping();
    test_bluesky_embed_external_and_video();
    test_bluesky_embed_record_with_media();
    test_bluesky_labels_content_warning();

    test_bluesky_facets_link();
    test_bluesky_facets_link_trailing_punct();
    test_bluesky_facets_bare_www();
    test_bluesky_facets_mention();
    test_bluesky_facets_mention_needs_domain();
    test_bluesky_facets_tag();
    test_bluesky_facets_utf8_offsets();
    test_bluesky_facets_mixed();
    test_instance_normalization();
    test_base64();
    test_timeline_cache();
    test_presenter_compact();
    test_presenter_boost_compact();
    test_presenter_accessibility_all_fields();
    test_presenter_accessibility_default_config();
    test_presenter_grouped_notification();
    test_presenter_cw_modes();
    test_presenter_demojify_and_mentions();
    test_presenter_wrap_and_separator();
    test_presenter_stats_nonzero();
    test_presenter_poll();
    test_reply_participants();
    test_post_links();
    test_speech_defaults();
    test_speech_normalized();
    test_settings_roundtrip();
    test_sse_basic();
    test_sse_split_across_feeds();
    test_sse_multiline_crlf_comments();
    test_capi_session_events();
    test_mastodon_thread_fetch();
    test_mastodon_thread_folding();
    test_mastodon_instance_max_chars();
    test_mastodon_user_pinned_posts();
    test_keymap_normalize();
    test_keymap_default_and_catalog();
    test_keymap_parse_and_serialize();
    test_keymap_inheritance();
    test_update_version_compare();
    test_update_stable_branch();
    test_update_latest_branch();
    test_update_installer_asset();
    test_client_filter_post_types();
    test_client_filter_media_and_me();
    test_client_filter_text();
    test_server_filter_metadata();
    test_refresh_fills_gap_below_streamed_top();
    test_refresh_steady_state_stops_early();
    test_refresh_fills_multipage_gap();
    test_refresh_keeps_updated_conversation();
    test_lost_row_keeps_reading_position();
    test_position_hint_falls_back_to_nearest();

    std::printf("%d checks, %d failures\n", fastsmtest::checks(), fastsmtest::failures());
    return fastsmtest::failures() == 0 ? 0 : 1;
}
