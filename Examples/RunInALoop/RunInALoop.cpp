
#include <Windows.h>
#include <iostream>
#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <string>

//#define USEAPPSRC

struct state {
#ifdef USEAPPSRC
	GstElement* appsrc;
#endif
	GstBus* bus;
	GMainLoop* loop;
	GstElement* pipeline;
	GstVideoInfo info;
	GstClockTime timestamp;
	GstVideoFrame frame;
	int frameNumber;
};

void needDataCallback(GstElement* appsrc, guint unused, void* context)
{
	GstBuffer* buffer;
	GstFlowReturn ret;
	state* statePtr = (state*)context;

	statePtr->frameNumber++;

	buffer = gst_buffer_new_allocate(NULL, statePtr->info.size, NULL);

	if (gst_video_frame_map(&statePtr->frame, &statePtr->info, buffer, GST_MAP_WRITE))
	{
		guint8* pixels = (guint8*)GST_VIDEO_FRAME_PLANE_DATA(&statePtr->frame, 0);

		for (int y = 0; y < statePtr->info.height; y++)
		{
			for (int x = 0; x < statePtr->info.width; x++)
			{
				int index = (y * statePtr->info.width + x) * 2;

				pixels[index] = (guint8)(statePtr->frameNumber + y * 2 + x * 2);
				pixels[index + 1] = 128;
			}
		}

		gst_video_frame_unmap(&statePtr->frame);
	}

	GST_BUFFER_PTS(buffer) = statePtr->timestamp;
	GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 15);
	statePtr->timestamp += GST_BUFFER_DURATION(buffer);

	g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);

	if (ret != GST_FLOW_OK)
	{
		printf("Error happened.");
	}

	printf(".");

	gst_buffer_unref(buffer);
}

gboolean busCallback(GstBus* bus, GstMessage* msg, gpointer myData)
{
	printf("*");
	switch (GST_MESSAGE_TYPE(msg))
	{
	case GST_MESSAGE_ERROR:
	{
		GError* err = NULL;
		gchar* dbg_info = NULL;

		gst_message_parse_error(msg, &err, &dbg_info);

		if (err)
		{
			printf("%s:%s\n", err->message, dbg_info);
		}
		return FALSE;
	}
	}
	return TRUE;
}

int main(int argc, char* argv[])
{
	/* Initialize GStreamer */
	gst_init(&argc, &argv);

	for (int c = 0;; c++)
	{
		state state;
		memset(&state, 0, sizeof(state));

		GError* err = nullptr;
#ifdef USEAPPSRC
		std::string pipelineString = (std::string("appsrc name=frameSrc is-live=true ! videoconvert ! queue max-size-buffers=1 ! mfxh264enc rate-control=2 bitrate=400 ! h264parse ") +
			std::string("! mp4mux name=mux ! queue ! filesink sync=True location=test") + std::to_string(c) + std::string(".mp4"));
#else
		std::string pipelineString = (std::string("videotestsrc name=frameSrc ! videoconvert ! queue max-size-buffers=1 ! mfxh264enc rate-control=2 bitrate=400 ! h264parse ") +
			std::string("! mp4mux name=mux ! queue ! filesink sync=True location=test") + std::to_string(c) + std::string(".mp4"));
#endif
		state.pipeline = gst_parse_launch(pipelineString.c_str(), &err);

		if (state.pipeline && err == nullptr)
		{
			state.loop = g_main_loop_new(NULL, FALSE);
			state.bus = GST_ELEMENT_BUS(state.pipeline);
			gst_bus_add_watch(state.bus, (GstBusFunc)busCallback, &state);

#ifdef USEAPPSRC
			state.appsrc = gst_bin_get_by_name(GST_BIN(state.pipeline), "frameSrc");
			g_signal_connect(state.appsrc, "need-data", (GCallback)needDataCallback, &state);
			gst_util_set_object_arg(G_OBJECT(state.appsrc), "format", "time");
#endif

			GstCaps* caps = gst_caps_new_simple("video/x-raw",
				"format", G_TYPE_STRING, "YUY2",
				"width", G_TYPE_INT, 320,
				"height", G_TYPE_INT, 240,
				"framerate", GST_TYPE_FRACTION, 15, 1, NULL);

#ifdef USEAPPSRC
			g_object_set(G_OBJECT(state.appsrc), "caps", caps, NULL);
#endif
			gst_video_info_from_caps(&state.info, caps);

#ifdef USEAPPSRC
			gst_object_unref(state.appsrc);
#endif

			gst_element_set_state(state.pipeline, GST_STATE_PLAYING);
			//g_main_loop_run(state.loop);
			printf("Playing...");
			Sleep(10000);
			printf("stopping...");

			printf("sending EOS signal...");
			gst_element_send_event(state.pipeline, gst_event_new_eos());
			printf("waiting for signal...");
			GstMessage* msg = gst_bus_timed_pop_filtered(GST_ELEMENT_BUS(state.pipeline),
				GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));

			gst_element_set_state(state.pipeline, GST_STATE_NULL);

			gst_object_unref(state.bus);
			g_main_loop_unref(state.loop);
			gst_object_unref(state.pipeline);
			printf("all done!\n");
		}
		else
		{
			printf("Pipeline failed: %s", err->message);
		}
	}


	return 0;
}
