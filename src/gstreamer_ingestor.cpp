// Copyright (c) 2019 Intel Corporation.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM,OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

/**
 * @file
 * @brief Gstreamer Ingestor implementation
 */

#ifdef WITH_PROFILE
#include <chrono>
#endif

#include<string>

#include "eis/utils/logger.h"
#include "eis/vi/gstreamer_ingestor.h"
#include "eis/vi/gva_roi_meta.h"
#include "eis/utils/frame.h"
#include <sstream>
#include <random>
#include <string>

#define UUID_LENGTH 5
#define PIPELINE "pipeline"

using namespace eis::vi;

// Prototypes
static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data);

GstreamerIngestor::GstreamerIngestor(config_t* config, FrameQueue* frame_queue):
    Ingestor(config, frame_queue) {

    config_value_t* cvt_pipeline = config->get_config_value(config->cfg, PIPELINE);
    LOG_INFO("cvt_pipeline initialized");
    if(cvt_pipeline == NULL) {
        LOG_ERROR("JSON missing key \'%s\'", PIPELINE);
    } else if(cvt_pipeline->type != CVT_STRING) {
        LOG_ERROR("JSON value for \'%s\' must be a string", PIPELINE);
        config_value_destroy(cvt_pipeline);
    }
    m_pipeline = std::string(cvt_pipeline->body.string);
    config_value_destroy(cvt_pipeline);

    config_value_t* cvt_poll_interval = config->get_config_value(
            config->cfg, POLL_INTERVAL);
    if(cvt_poll_interval != NULL) {
        if(cvt_poll_interval->type != CVT_FLOATING) {
            LOG_INFO_0("Poll interval must be a number");
            config_value_destroy(cvt_poll_interval);
        }
        m_poll_interval = cvt_poll_interval->body.floating;
        config_value_destroy(cvt_poll_interval);
    }

    LOG_INFO("Pipeline: %s", m_pipeline.c_str());
    LOG_INFO("Poll interval: %lf", m_poll_interval);

#ifdef WITH_PROFILE
    m_frame_count = 0;
#endif

    int argc = 1;
    m_loop = NULL ;
    m_gst_pipeline = NULL ;
    m_sink = NULL;
    gulong ret;
    char** argv = new char*[1];
    gst_init(&argc, &argv);

    // Initialize Glib loop
    m_loop = g_main_loop_new(NULL, FALSE);
    // TODO: Verify correctly initialized

    // Load Gstreamer pipeline
    m_gst_pipeline = gst_parse_launch((char*)&m_pipeline[0], NULL);

    // TODO: Verify correctly loaded

    // Get and configure the sink element
    m_sink = gst_bin_get_by_name(GST_BIN(m_gst_pipeline), "sink");
    // TODO: Check that the sink was correctly found
    g_object_set(m_sink, "emit-signals", TRUE, NULL);
    ret = g_signal_connect(m_sink, "new-sample", G_CALLBACK(this->new_sample), this);
    if (!ret){
        LOG_ERROR_0("Connection to GCallback not successfull");
    }
    // Get the GST bus
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(m_gst_pipeline));
    if (bus == NULL){
    	LOG_ERROR_0("Failed to initialize GST bus");
    }
    m_bus_watch_id = gst_bus_add_watch(bus, bus_call, m_loop);
    gst_object_unref(bus);
    // TODO: Verify bus actions happened correctly
}

GstreamerIngestor::~GstreamerIngestor() {
    if(m_gst_pipeline != NULL)
        gst_object_unref(GST_OBJECT(m_gst_pipeline));
    if(m_bus_watch_id != 0)
        g_source_remove(m_bus_watch_id);
    if(m_loop != NULL)
        g_main_loop_unref(m_loop);
    // TODO: What about the m_sink? TO BE VERIFIED...
}

void GstreamerIngestor::stop() {
    g_main_loop_quit(m_loop);
    // TODO: Should there be a wait here???
    gst_element_set_state(m_gst_pipeline, GST_STATE_NULL);
}

// This method does nothing in this implementation since the frames are
// retrieved via an async call from GStreamer
void GstreamerIngestor::read(Frame*& frame) {}

void GstreamerIngestor::run() {
#ifdef WITH_PROFILE
    auto start = std::chrono::system_clock::now();
#endif

    LOG_INFO_0("Gstreamer ingestor thread started");
    gst_element_set_state(m_gst_pipeline, GST_STATE_PLAYING);
    g_main_loop_run(m_loop);
    LOG_INFO_0("Gstreamer ingestor thread stopped");

#ifdef WITH_PROFILE
    auto end = std::chrono::system_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            end - start).count();
    LOG_INFO("GStreamer FPS: %d", m_frame_count / elapsed);
#endif
}

/**
 * Gstreamer bus event callback
 */
static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
            LOG_INFO_0("End of stream");
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_ERROR: {
            gchar  *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free (debug);

            LOG_ERROR("Gst Bus Error: %s", error->message);
            g_error_free(error);

            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }

    return TRUE;
}

/**
 * Wrapper around gstreamer pointers to a given frame.
 */
class GstreamerFrame {
public:
    GstSample* sample;
    GstBuffer* buf;
    GstMapInfo* info;

    GstreamerFrame(GstSample* sample, GstBuffer* buf, GstMapInfo* info) :
        sample(sample), buf(buf), info(info)
    {}

    ~GstreamerFrame() {
        gst_buffer_unmap(buf, info);
        gst_sample_unref(sample);
    }
};

/**
 * Method to free a @c GstreamerFrame object
 */
void free_gst_frame(void* obj) {
    GstreamerFrame* frame = (GstreamerFrame*) obj;
    delete frame;
}

/**
 * A new sample has been received in the appsink
 */
GstFlowReturn GstreamerIngestor::new_sample(GstElement *sink, GstreamerIngestor* ctx) {
    GstSample* sample;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if(sample) {
        GstBuffer* buf = gst_sample_get_buffer(sample); // no lifetime transfer
        if(buf) {
            //GstMapInfo info = {};
            GstMapInfo* info = (GstMapInfo*) malloc(sizeof(GstMapInfo));
            if(!gst_buffer_map(buf, info, GST_MAP_READ)) {
                // Taken from OpenCV ???
                LOG_ERROR_0("Failed to map GStreamer buffer to system memory");
            } else {
                //LOG_INFO("Got frame of size: %ld", info.size);
                GstCaps * frame_caps = gst_sample_get_caps(sample);
                GstStructure* structure = gst_caps_get_structure(frame_caps, 0);  // no lifetime transfer
                gint width;
                gint height;
                gst_structure_get_int(structure, "width", &width);
                gst_structure_get_int(structure, "height", &height);
                //const gchar* name = gst_structure_get_name(structure);
                //const gchar* format = gst_structure_get_string(structure, "format");
                //LOG_DEBUG("Name: %s, Format: %s, Size: %dx%d", name, format, width, height);

                // TODO: Check BGRx
                GstreamerFrame* gst_frame = new GstreamerFrame(
                        sample, buf, info);

                Frame* frame = new Frame(
                        (void*) gst_frame, (int) width, (int) height, 4,
                        info->data, free_gst_frame);

                //Get the GVA metadata from the GST buffer
                GVA::RegionOfInterestList roi_list(buf);

                msgbus_ret_t ret = MSG_SUCCESS;

                msg_envelope_elem_body_t* gva_meta_arr = msgbus_msg_envelope_new_array();
		        if(gva_meta_arr == NULL) {
                        LOG_ERROR_0("Failed to initialize gva metadata");
                        return GST_FLOW_ERROR;
                    }

                for(GVA::RegionOfInterest& roi : roi_list) {
                    GstVideoRegionOfInterestMeta* meta = roi.meta();

                    LOG_DEBUG("Object Bounding Box: [%d, %d] [%d, %d]",
                            meta->x, meta->y, meta->w, meta->h)

                    msg_envelope_elem_body_t* roi_obj= msgbus_msg_envelope_new_object();
                    if(roi_obj == NULL) {
                        LOG_ERROR_0("Failed to initialize roi metadata");
                        return GST_FLOW_ERROR;
                    }

		            msg_envelope_elem_body_t* x = msgbus_msg_envelope_new_integer(meta->x);
                    if(x == NULL) {
                        LOG_ERROR_0("Failed to initialize horizontal offset metadata");
                        return GST_FLOW_ERROR;
                    }

		            msg_envelope_elem_body_t* y = msgbus_msg_envelope_new_integer(meta->y);
                    if(y == NULL) {
                        LOG_ERROR_0("Failed to initialize vertical offset metadata");
                        return GST_FLOW_ERROR;
                    }


		            msg_envelope_elem_body_t* w = msgbus_msg_envelope_new_integer(meta->w);
                    if(w == NULL) {
                        LOG_ERROR_0("Failed to initialize width metadata");
                        return GST_FLOW_ERROR;
                    }

		            msg_envelope_elem_body_t* h = msgbus_msg_envelope_new_integer(meta->h);
		            if(h == NULL) {
                        LOG_ERROR_0("Failed to initialize height metadata");
                        return GST_FLOW_ERROR;
                    }

                    ret = msgbus_msg_envelope_elem_object_put(roi_obj, "x", x);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to put horizontal offset metadata");
                        return GST_FLOW_ERROR;
                    }

                    ret = msgbus_msg_envelope_elem_object_put(roi_obj, "y", y);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to put vertical offset metadata");
                        return GST_FLOW_ERROR;
                    }

                    ret = msgbus_msg_envelope_elem_object_put(roi_obj, "width", w);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to put width metadata");
                        return GST_FLOW_ERROR;
                    }

                    ret = msgbus_msg_envelope_elem_object_put(roi_obj, "height", h);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to put height metadata");
                        return GST_FLOW_ERROR;
                    }

                    msg_envelope_elem_body_t* tensor_arr = msgbus_msg_envelope_new_array();
                    if(tensor_arr == NULL) {
                        LOG_ERROR_0("Failed to initialize tensor metadata");
                        return GST_FLOW_ERROR;
                    }

                    for(GVA::Tensor& tensor : roi) {
                        LOG_DEBUG("Attribute: %s, Label: %s, Confidence: %f",
                                tensor.name().c_str(), tensor.label().c_str(),
                                tensor.confidence())

                        msg_envelope_elem_body_t* tensor_obj= msgbus_msg_envelope_new_object();
                        if(tensor_obj == NULL) {
                            LOG_ERROR_0("Failed to initialize tensor metadata for each roi");
                            return GST_FLOW_ERROR;
                        }

                        msg_envelope_elem_body_t* attribute = msgbus_msg_envelope_new_string(tensor.name().c_str());
                        if(attribute == NULL) {
                            LOG_ERROR_0("Failed to initialize attribute metadata");
                            return GST_FLOW_ERROR;
                        }

			            msg_envelope_elem_body_t* label = msgbus_msg_envelope_new_string(tensor.label().c_str());
                        if(label == NULL) {
                            LOG_ERROR_0("Failed to initialize label metadata");
                            return GST_FLOW_ERROR;
                        }

			            msg_envelope_elem_body_t* confidence = msgbus_msg_envelope_new_integer(tensor.confidence());
                        if(confidence == NULL) {
                            LOG_ERROR_0("Failed to initialize confidence metadata");
                            return GST_FLOW_ERROR;
                        }

                        ret = msgbus_msg_envelope_elem_object_put(tensor_obj, "attribute", attribute);
                        if(ret != MSG_SUCCESS) {
                            LOG_ERROR_0("Failed to put attribute metadata");
                            return GST_FLOW_ERROR;
                        }

                        ret = msgbus_msg_envelope_elem_object_put(tensor_obj, "label", label);
                        if(ret != MSG_SUCCESS) {
                            LOG_ERROR_0("Failed to put label metadata");
                            return GST_FLOW_ERROR;
                        }

                        ret = msgbus_msg_envelope_elem_object_put(tensor_obj, "confidence", confidence);
                        if(ret != MSG_SUCCESS) {
                            LOG_ERROR_0("Failed to put confidence metadata");
                            return GST_FLOW_ERROR;
                        }

                        ret = msgbus_msg_envelope_elem_array_add(tensor_arr, tensor_obj);
                        if(ret != MSG_SUCCESS) {
                            LOG_ERROR_0("Failed to add tensor object to tensor array metadata");
                            return GST_FLOW_ERROR;
                        }
                    }

                    ret = msgbus_msg_envelope_elem_object_put(roi_obj, "tensor", tensor_arr);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to put tensor array to roi object metadata")
                        return GST_FLOW_ERROR;
                    }

                    ret = msgbus_msg_envelope_elem_array_add(gva_meta_arr,roi_obj);
                    if(ret != MSG_SUCCESS) {
                        LOG_ERROR_0("Failed to add roi object to gva metadata")
                        return GST_FLOW_ERROR;
                    }
                }

                msg_envelope_t* gva_meta_data = frame->get_meta_data();
                if(gva_meta_arr == NULL) {
                    LOG_ERROR_0("Failed to initialize frame metadata");
                    return GST_FLOW_ERROR;
                }

	            ret = msgbus_msg_envelope_put(gva_meta_data, "gva_meta", gva_meta_arr);
                if(ret != MSG_SUCCESS) {
                    LOG_ERROR_0("Failed to put gva metadata")
                    return GST_FLOW_ERROR;
                }

                // Adding image handle to frame
                std::string randuuid = ctx->generate_image_handle(UUID_LENGTH);
                msg_envelope_t* meta_data = frame->get_meta_data();
                msg_envelope_elem_body_t* elem = msgbus_msg_envelope_new_string(randuuid.c_str());
                if (elem == NULL) {
                    throw "Failed to create image handle element";
                }
                msgbus_msg_envelope_put(meta_data, "img_handle", elem);
                if(ret != MSG_SUCCESS) {
                    throw "Failed to put image handle meta-data";
                }

                ctx->m_udf_input_queue->push(frame);
#ifdef WITH_PROFILE
                ctx->m_frame_count++;
#endif
            }
        } else {
            LOG_ERROR_0("Failed to get GstBuffer");
        }
        return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
}
