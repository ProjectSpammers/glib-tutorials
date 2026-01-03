#include "screencast-webrtc.h"
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/webrtc/webrtc.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <stdio.h>
#include <string.h>
#include "../common/utils.h"
#include "secrets.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST_INTERFACE "org.freedesktop.portal.ScreenCast"
#define REQUEST_INTERFACE "org.freedesktop.portal.Request"

typedef struct {
  GMainLoop* loop;
  GDBusConnection* connection;
  gchar* sanitized_name;
  gchar* session_path;
  gchar* session_token;
  GstElement* pipeline;
  GstElement* webrtcbin;
  int is_sound_excluded;

  // Networking & State:
  SoupSession* soup_session;
  gchar* room_id;
  guint poll_timeout_id;
  GHashTable* seen_candidates;  // To avoid adding the same candidate twice

} ScreencastWebRTCState;

static void select_sources(ScreencastWebRTCState* state);
static void start_stream(guint32 id, ScreencastWebRTCState* state);

// Callback to check if upload succeeded
static void on_upload_complete(GObject* source, GAsyncResult* res,
                               gpointer user_data) {
  SoupSession* session = SOUP_SESSION(source);
  GError* error = NULL;
  GBytes* body = soup_session_send_and_read_finish(session, res, &error);

  char* path = (char*)user_data;  // We passed the path name to debug

  if (error) {
    g_printerr("[Firebase] NETWORK ERROR on %s: %s\n", path, error->message);
    g_error_free(error);
  } else {
    SoupMessage* msg = soup_session_get_async_result_message(session, res);
    guint status = soup_message_get_status(msg);
    if (status >= 200 && status < 300) {
      // Success - quiet (uncomment to debug)
      // g_print("[Firebase] OK (%d) %s\n", status, path);
    } else {
      g_printerr("[Firebase] API ERROR (%d) on %s\n", status, path);
      if (body) {
        gsize len;
        const char* data = g_bytes_get_data(body, &len);
        g_printerr("Server said: %.*s\n", (int)len, data);
      }
    }
  }
  if (body)
    g_bytes_unref(body);
  g_free(path);
}

static void send_firebase_request(ScreencastWebRTCState* state,
                                  const gchar* method, const gchar* subpath,
                                  const gchar* json_data) {
  gchar* url = g_strdup_printf("%s/rooms/%s/%s.json", FIREBASE_URL,
                               state->room_id, subpath);
  SoupMessage* msg = soup_message_new(method, url);

  GBytes* body = g_bytes_new(json_data, strlen(json_data));
  soup_message_set_request_body_from_bytes(msg, "application/json", body);
  g_bytes_unref(body);

  // Debug callback
  soup_session_send_and_read_async(state->soup_session, msg, G_PRIORITY_DEFAULT,
                                   NULL, on_upload_complete, g_strdup(subpath));

  g_print("[Firebase] Sending %s (%s)...\n", method, subpath);
  g_free(url);
  g_object_unref(msg);
}

static void on_ice_candidate(GstElement* webrtc, guint mline_index,
                             gchar* candidate, gpointer user_data) {
  ScreencastWebRTCState* state = (ScreencastWebRTCState*)user_data;

  // Create JSON: { "candidate": "...", "sdpMLineIndex": 0 }
  JsonBuilder* builder = json_builder_new();
  json_builder_begin_object(builder);
  json_builder_set_member_name(builder, "candidate");
  json_builder_add_string_value(builder, candidate);
  json_builder_set_member_name(builder, "sdpMLineIndex");
  json_builder_add_int_value(builder, mline_index);
  json_builder_end_object(builder);

  JsonGenerator* gen = json_generator_new();
  json_generator_set_root(gen, json_builder_get_root(builder));
  gchar* json_str = json_generator_to_data(gen, NULL);

  // Upload to 'candidates/caller' using POST (append to list)
  send_firebase_request(state, "POST", "candidates/caller", json_str);

  g_free(json_str);
  g_object_unref(gen);
  g_object_unref(builder);
}

static void check_peer_candidates(ScreencastWebRTCState* state) {
  gchar* url = g_strdup_printf("%s/rooms/%s/candidates/callee.json",
                               FIREBASE_URL, state->room_id);
  SoupMessage* msg = soup_message_new("GET", url);
  GBytes* response =
      soup_session_send_and_read(state->soup_session, msg, NULL, NULL);

  if (response) {
    guint status = soup_message_get_status(msg);
    if (status == 200) {
      gsize len;
      const char* data = g_bytes_get_data(response, &len);

      // If data is valid JSON
      if (g_strcmp0(data, "null") != 0 && len > 2) {
        JsonParser* parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, len, NULL)) {
          JsonNode* root = json_parser_get_root(parser);
          JsonObject* obj = json_node_get_object(root);

          // Firebase returns a Map of keys: { "-Key1": {...}, "-Key2": {...} }
          GList* members = json_object_get_members(obj);
          for (GList* l = members; l != NULL; l = l->next) {
            const gchar* key = (const gchar*)l->data;

            // If we haven't seen this candidate yet
            if (!g_hash_table_contains(state->seen_candidates, key)) {
              JsonObject* cand_obj = json_object_get_object_member(obj, key);
              const gchar* cand_str =
                  json_object_get_string_member(cand_obj, "candidate");
              gint mline =
                  json_object_get_int_member(cand_obj, "sdpMLineIndex");

              if (cand_str) {
                g_print("[ICE] Adding Remote Candidate: %s\n", cand_str);
                g_signal_emit_by_name(state->webrtcbin, "add-ice-candidate",
                                      mline, cand_str);
                g_hash_table_add(state->seen_candidates, g_strdup(key));
              }
            }
          }
          g_list_free(members);
        }
        g_object_unref(parser);
      }
    }
    g_bytes_unref(response);
  }
  g_object_unref(msg);
  g_free(url);
}

static void process_sdp_answer(ScreencastWebRTCState* state,
                               const gchar* json_input) {
  // Robust JSON parsing using json-glib
  JsonParser* parser = json_parser_new();
  if (!json_parser_load_from_data(parser, json_input, -1, NULL)) {
    g_printerr("Failed to parse Answer JSON\n");
    g_object_unref(parser);
    return;
  }

  JsonNode* root = json_parser_get_root(parser);
  JsonObject* obj = json_node_get_object(root);
  const gchar* sdp_str = json_object_get_string_member(obj, "sdp");

  if (sdp_str) {
    g_print("[Signaling] Setting Remote Answer...\n");
    GstSDPMessage* sdp_msg;
    if (gst_sdp_message_new_from_text(sdp_str, &sdp_msg) == GST_SDP_OK) {
      GstWebRTCSessionDescription* answer = gst_webrtc_session_description_new(
          GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);
      GstPromise* p = gst_promise_new();
      g_signal_emit_by_name(state->webrtcbin, "set-remote-description", answer,
                            p);
      gst_promise_unref(p);
      gst_webrtc_session_description_free(answer);
    }
  }
  g_object_unref(parser);
}

static gboolean signaling_poll(gpointer user_data) {
  ScreencastWebRTCState* state = (ScreencastWebRTCState*)user_data;

  // 1. Check for Answer (GET)
  gchar* url =
      g_strdup_printf("%s/rooms/%s/answer.json", FIREBASE_URL, state->room_id);
  SoupMessage* msg = soup_message_new("GET", url);
  GBytes* response =
      soup_session_send_and_read(state->soup_session, msg, NULL, NULL);

  if (response) {
    if (soup_message_get_status(msg) == 200) {
      gsize len;
      const char* data = g_bytes_get_data(response, &len);
      // Simple check if SDP exists in string to avoid parsing garbage
      if (data && g_strstr_len(data, len, "sdp")) {
        // Check if we are already stable (don't re-set answer)
        GstWebRTCSignalingState sig_state;
        g_object_get(state->webrtcbin, "signaling-state", &sig_state, NULL);
        if (sig_state != GST_WEBRTC_SIGNALING_STATE_STABLE) {
          process_sdp_answer(state, data);
        }
      }
    }
    g_bytes_unref(response);
  }
  g_object_unref(msg);
  g_free(url);

  // 2. Check for ICE Candidates
  check_peer_candidates(state);

  return TRUE;  // Keep polling
}

static void on_offer_created(GstPromise* promise, gpointer user_data) {
  ScreencastWebRTCState* state = (ScreencastWebRTCState*)user_data;
  GstStructure* reply;
  GstWebRTCSessionDescription* offer = NULL;

  if (gst_promise_wait(promise) != GST_PROMISE_RESULT_REPLIED)
    return;
  reply = gst_promise_get_reply(promise);
  gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer,
                    NULL);
  gst_promise_unref(promise);

  if (offer) {
    g_signal_emit_by_name(state->webrtcbin, "set-local-description", offer,
                          NULL);

    // 1. Get SDP String
    gchar* sdp_string = gst_sdp_message_as_text(offer->sdp);

    // 2. Use JSON-GLib to build valid JSON (Safer than manual printf)
    JsonBuilder* builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "offer");
    json_builder_set_member_name(builder, "sdp");
    json_builder_add_string_value(
        builder, sdp_string);  // Automatically escapes \r\n etc.
    json_builder_end_object(builder);

    JsonGenerator* gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar* json_payload = json_generator_to_data(gen, NULL);

    // 3. Upload
    send_firebase_request(state, "PUT", "offer", json_payload);

    g_print("[Signaling] Offer sent! Room ID: %s\n", state->room_id);
    g_print("[Signaling] Starting poll loop...\n");

    if (state->poll_timeout_id == 0) {
      state->poll_timeout_id = g_timeout_add(1000, signaling_poll, state);
    }

    g_free(sdp_string);
    g_free(json_payload);
    g_object_unref(gen);
    g_object_unref(builder);
    gst_webrtc_session_description_free(offer);
  }
}

static void on_negotiation_needed(GstElement* element, gpointer user_data) {
  ScreencastWebRTCState* state = (ScreencastWebRTCState*)user_data;
  GstPromise* promise =
      gst_promise_new_with_change_func(on_offer_created, state, NULL);
  g_signal_emit_by_name(state->webrtcbin, "create-offer", NULL, promise);
}

// --- Pipeline ---
static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
  ScreencastWebRTCState* state = (ScreencastWebRTCState*)data;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      gchar* debug;
      GError* error;
      gst_message_parse_error(msg, &error, &debug);
      g_printerr("\nERROR: %s\n", error->message);
      if (debug)
        g_printerr("Debug Info: %s\n", debug);
      g_error_free(error);
      g_free(debug);
      g_main_loop_quit(state->loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static gchar* get_default_monitor_source() {
  FILE* fp;
  char path[1024];
  fp = popen("pactl get-default-sink", "r");
  if (!fp)
    return NULL;
  if (fgets(path, sizeof(path) - 1, fp)) {
    path[strcspn(path, "\n")] = 0;
    path[strcspn(path, "\r")] = 0;
  } else {
    pclose(fp);
    return NULL;
  }
  pclose(fp);
  return g_strdup_printf("%s.monitor", path);
}

static void start_stream(guint32 id, ScreencastWebRTCState* state) {
  g_print("\n>>> Starting WebRTC Pipeline... Node ID: %d\n", id);
  gst_init(NULL, NULL);
  gchar* audio_device = get_default_monitor_source();

  char* pipeline_str = g_strdup_printf(
      "webrtcbin name=sendrecv stun-server=stun://stun.l.google.com:19302 "
      "bundle-policy=max-bundle latency=0 "

      // --- VIDEO ---
      "pipewiresrc path=%u do-timestamp=true ! "
      "queue max-size-buffers=3 leaky=downstream ! "
      "videoconvert ! "
      "videoscale ! videorate ! "

      "video/"
      "x-raw(memory:SystemMemory),format=NV12,width=1920,height=1080,framerate="
      "60/1 ! "

      "nvh264enc "
      "bitrate=8000 "
      "rc-mode=cbr "
      "preset=low-latency-hq "
      "tune=ultra-low-latency "
      "gop-size=60 "
      "zerolatency=true "
      "qos=false ! "

      "h264parse ! "
      "video/x-h264,stream-format=byte-stream,profile=constrained-baseline ! "

      "rtph264pay config-interval=1 pt=96 aggregate-mode=zero-latency ! "
      "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
      "queue ! sendrecv. "

      "pulsesrc device=%s do-timestamp=true "
      "buffer-time=200000 ! "
      "audioconvert ! "
      "audioresample ! "
      "opusenc ! "
      "rtpopuspay pt=97 ! "
      "queue ! sendrecv. ",
      id,
      state->is_sound_excluded > 0 ? "GStreamer_Stream.monitor"
      : audio_device               ? audio_device
                                   : "0");

  if (audio_device)
    g_free(audio_device);

  GError* error = NULL;
  state->pipeline = gst_parse_launch(pipeline_str, &error);
  g_free(pipeline_str);

  if (error) {
    g_printerr("Pipeline Error: %s\n", error->message);
    g_error_free(error);
    g_main_loop_quit(state->loop);
    return;
  }

  state->webrtcbin = gst_bin_get_by_name(GST_BIN(state->pipeline), "sendrecv");

  g_signal_connect(state->webrtcbin, "on-negotiation-needed",
                   G_CALLBACK(on_negotiation_needed), state);
  g_signal_connect(state->webrtcbin, "on-ice-candidate",
                   G_CALLBACK(on_ice_candidate), state);

  GstBus* bus = gst_element_get_bus(state->pipeline);
  gst_bus_add_watch(bus, bus_call, state);
  gst_object_unref(bus);

  gst_element_set_state(state->pipeline, GST_STATE_PLAYING);
}

static void on_start_response(GDBusConnection* conn, const gchar* sender,
                              const gchar* path, const gchar* iface,
                              const gchar* signal, GVariant* params,
                              gpointer user_data) {
  ScreencastWebRTCState* state = user_data;
  guint32 response_code;
  GVariant* res;

  g_variant_get(params, "(u@a{sv})", &response_code, &res);
  if (response_code != 0) {
    g_printerr("Start Request Denied.\n");
    g_main_loop_quit(state->loop);
    g_variant_unref(res);
    return;
  }

  GVariant* streams =
      g_variant_lookup_value(res, "streams", G_VARIANT_TYPE("a(ua{sv})"));
  if (streams) {
    GVariantIter iter;
    g_variant_iter_init(&iter, streams);
    guint32 stream_id;
    if (g_variant_iter_next(&iter, "(u@a{sv})", &stream_id, NULL)) {
      start_stream(stream_id, state);
    }
    g_variant_unref(streams);
  }
  g_variant_unref(res);
}

static void start_screencast(ScreencastWebRTCState* state) {
  gchar* token = generate_token("tk_start");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(token));

  g_print("Sending Start Request...\n");

  GError* error = NULL;
  GVariant* ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "Start",
      g_variant_new("(osa{sv})", state->session_path, "", &opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("Start Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar* req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_dbus_connection_signal_subscribe(
      state->connection, PORTAL_BUS_NAME, REQUEST_INTERFACE, "Response",
      req_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, on_start_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

static void on_select_response(GDBusConnection* conn, const gchar* sender,
                               const gchar* path, const gchar* iface,
                               const gchar* signal, GVariant* params,
                               gpointer user_data) {
  ScreencastWebRTCState* state = user_data;
  guint32 response_code;
  g_variant_get(params, "(u@a{sv})", &response_code, NULL);

  if (response_code != 0) {
    g_printerr("Source selection failed.\n");
    g_main_loop_quit(state->loop);
    return;
  }

  g_print("Sources selected. Starting screencast...\n");
  start_screencast(state);
}

static void select_sources(ScreencastWebRTCState* state) {
  gchar* token = generate_token("tk_slct");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(token));
  g_variant_builder_add(&opts, "{sv}", "types", g_variant_new_uint32(1 | 2));
  g_variant_builder_add(&opts, "{sv}", "cursor_mode", g_variant_new_uint32(2));

  g_print("Requesting Source Selection...\n");

  GError* error = NULL;
  GVariant* ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "SelectSources",
      g_variant_new("(oa{sv})", state->session_path, &opts), NULL,
      G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("SelectSources Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar* req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_print("Portal assigned Select path: %s\n", req_path);

  g_dbus_connection_signal_subscribe(state->connection, PORTAL_BUS_NAME,
                                     REQUEST_INTERFACE, "Response", req_path,
                                     NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                     on_select_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

static void on_create_session_response(GDBusConnection* conn,
                                       const gchar* sender, const gchar* path,
                                       const gchar* iface, const gchar* signal,
                                       GVariant* params, gpointer user_data) {
  ScreencastWebRTCState* state = user_data;
  guint32 response_code;
  GVariant* results;
  g_variant_get(params, "(u@a{sv})", &response_code, &results);

  if (response_code != 0) {
    g_printerr("CreateSession Failed.\n");
    g_main_loop_quit(state->loop);
    return;
  }

  gchar* remote_handle = NULL;
  if (g_variant_lookup(results, "session_handle", "s", &remote_handle)) {
    g_free(state->session_path);
    state->session_path = g_strdup(remote_handle);
    g_free(remote_handle);
  }
  g_variant_unref(results);

  g_print("Session created. Proceeding to Select Sources...\n");
  select_sources(state);  // ZİNCİRLEME GEÇİŞ
}

static void create_session(ScreencastWebRTCState* state) {
  state->session_token = generate_token("tk_sess");
  const gchar* unique = g_dbus_connection_get_unique_name(state->connection);
  state->sanitized_name = sanitize_sender_name(unique);
  state->session_path =
      g_strdup_printf("%s/session/%s/%s", PORTAL_OBJECT_PATH,
                      state->sanitized_name, state->session_token);

  gchar* token = generate_token("tk_crt");
  GVariantBuilder opts;
  g_variant_builder_init(&opts, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&opts, "{sv}", "handle_token",
                        g_variant_new_string(token));
  g_variant_builder_add(&opts, "{sv}", "session_handle_token",
                        g_variant_new_string(state->session_token));

  g_print("Creating Session...\n");

  GError* error = NULL;
  GVariant* ret = g_dbus_connection_call_sync(
      state->connection, PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
      SCREENCAST_INTERFACE, "CreateSession", g_variant_new("(a{sv})", &opts),
      NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

  if (error) {
    g_printerr("CreateSession Failed: %s\n", error->message);
    g_error_free(error);
    g_free(token);
    return;
  }

  gchar* req_path;
  g_variant_get(ret, "(o)", &req_path);
  g_variant_unref(ret);

  g_dbus_connection_signal_subscribe(state->connection, PORTAL_BUS_NAME,
                                     REQUEST_INTERFACE, "Response", req_path,
                                     NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                     on_create_session_response, state, NULL);
  g_free(req_path);
  g_free(token);
}

// --- Main ---

void screencast_webrtc_tutorial(int argc, char* argv[]) {
  ScreencastWebRTCState* state = g_new0(ScreencastWebRTCState, 1);
  GError* error = NULL;

  // 1. Initialize Networking
  state->soup_session =
      soup_session_new_with_options("user-agent", "ProjectSpammers/1.0", NULL);
  state->seen_candidates =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  // 2. Set Room ID (Random or Argument)
  if (argc > 1) {
    state->room_id = g_strdup(argv[1]);
  } else {
    state->room_id = g_strdup_printf("%d", g_random_int_range(1000, 9999));
  }

  g_print("\n============================================\n");
  g_print("   ROOM ID: %s (Tell your friend this number)\n", state->room_id);
  g_print("============================================\n");

  // 3. Setup DBus & Main Loop
  state->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  state->is_sound_excluded =
      0;  // Keeping it simple for now, add argc check if you want
  if (error) {
    g_printerr("DBus Error: %s\n", error->message);
    return;
  }

  state->loop = g_main_loop_new(NULL, FALSE);

  // 4. Start Portal Sequence
  create_session(state);

  g_main_loop_run(state->loop);

  // Cleanup
  if (state->poll_timeout_id > 0)
    g_source_remove(state->poll_timeout_id);
  g_hash_table_destroy(state->seen_candidates);
  g_object_unref(state->soup_session);
  g_free(state->room_id);
  g_main_loop_unref(state->loop);
  g_free(state->sanitized_name);
  g_free(state->session_path);
  g_free(state);
}
