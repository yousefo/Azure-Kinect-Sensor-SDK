/** \file matroska_read.h
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 * Kinect For Azure Record SDK.
 * Internal MKV Reading Helpers
 */

#ifndef RECORD_READ_H
#define RECORD_READ_H

#include <k4ainternal/matroska_common.h>
#include <functional>
#include <mutex>
#include <future>

namespace k4arecord
{
typedef struct _cluster_info_t
{
    // The cluster size will be 0 until the actual cluster has been read from disk.
    // If cluster size is 0, the timestamp is not guaranteed to be the start of the cluster
    // populate_cluster_info() will update the cluster_size and timestamp to the real values.
    uint64_t timestamp_ns = 0;
    uint64_t file_offset = 0;
    uint64_t cluster_size = 0;
    std::weak_ptr<libmatroska::KaxCluster> cluster;

    bool next_known = false;
    struct _cluster_info_t *next = NULL;
    struct _cluster_info_t *previous = NULL;
} cluster_info_t;

// The cluster cache is a sparse linked-list index that may contain gaps until real data has been read from disk.
// The list is initialized with metadata from the Cues block, which is used as a hint for seeking in the file.
// Once it is known that no gap is present between indexed clusters, next_known is set to true.
typedef std::unique_ptr<cluster_info_t, std::function<void(cluster_info_t *)>> cluster_cache_t;

// A pointer to a cluster that is still being loaded from disk.
typedef std::shared_future<std::shared_ptr<libmatroska::KaxCluster>> future_cluster_t;

typedef struct _loaded_cluster_t
{
    cluster_info_t *cluster_info = NULL;
    std::shared_ptr<libmatroska::KaxCluster> cluster;

#if CLUSTER_READ_AHEAD_COUNT
    // Pointers to previous and next clusters to keep them preloaded in memory.
    future_cluster_t previous_clusters[CLUSTER_READ_AHEAD_COUNT];
    future_cluster_t next_clusters[CLUSTER_READ_AHEAD_COUNT];
#endif
} loaded_cluster_t;

typedef struct _block_info_t
{
    struct _track_reader_t *reader = NULL;
    std::shared_ptr<loaded_cluster_t> cluster;
    libmatroska::KaxInternalBlock *block = NULL;

    uint64_t timestamp_ns = 0;      // The timestamp of the block as written in the file.
    uint64_t sync_timestamp_ns = 0; // The timestamp of the block, including sychronization offsets.
    int index = -1;                 // Index of the block element within the cluster.
} block_info_t;

typedef struct _track_reader_t
{
    libmatroska::KaxTrackEntry *track;
    uint32_t width, height, stride;
    k4a_image_format_t format;
    uint64_t sync_delay_ns;
    BITMAPINFOHEADER *bitmap_header;

    std::shared_ptr<block_info_t> current_block;
} track_reader_t;

typedef struct _k4a_playback_context_t
{
    const char *file_path;
    std::unique_ptr<IOCallback> ebml_file;
    std::mutex io_lock; // Locks access to ebml_file
    bool file_closing;

    logger_t logger_handle;

    uint64_t timecode_scale;
    k4a_record_configuration_t record_config;

    std::unique_ptr<libebml::EbmlStream> stream;
    std::unique_ptr<libmatroska::KaxSegment> segment;

    std::unique_ptr<libmatroska::KaxInfo> segment_info;
    std::unique_ptr<libmatroska::KaxTracks> tracks;
    std::unique_ptr<libmatroska::KaxCues> cues;
    std::unique_ptr<libmatroska::KaxAttachments> attachments;
    std::unique_ptr<libmatroska::KaxTags> tags;

    libmatroska::KaxAttached *calibration_attachment;
    std::unique_ptr<k4a_calibration_t> device_calibration;

    uint64_t sync_period_ns;
    uint64_t seek_timestamp_ns;
    std::shared_ptr<loaded_cluster_t> seek_cluster;

    cluster_cache_t cluster_cache;
    std::recursive_mutex cache_lock; // Locks modification of cluster_cache

    track_reader_t color_track;
    track_reader_t depth_track;
    track_reader_t ir_track;

    track_reader_t imu_track;
    int imu_sample_index;

    uint64_t segment_info_offset;
    uint64_t first_cluster_offset;
    uint64_t tracks_offset;
    uint64_t cues_offset;
    uint64_t attachments_offset;
    uint64_t tags_offset;

    uint64_t last_timestamp_ns;

    // Stats
    uint64_t seek_count, load_count, cache_hits;
} k4a_playback_context_t;

K4A_DECLARE_CONTEXT(k4a_playback_t, k4a_playback_context_t);

std::unique_ptr<EbmlElement> next_child(k4a_playback_context_t *context, EbmlElement *parent);
k4a_result_t skip_element(k4a_playback_context_t *context, EbmlElement *element);

void match_ebml_id(k4a_playback_context_t *context, EbmlId &id, uint64_t offset);
bool seek_info_ready(k4a_playback_context_t *context);
k4a_result_t parse_mkv(k4a_playback_context_t *context);
k4a_result_t populate_cluster_cache(k4a_playback_context_t *context);
k4a_result_t parse_recording_config(k4a_playback_context_t *context);
k4a_result_t read_bitmap_info_header(track_reader_t *track);
void reset_seek_pointers(k4a_playback_context_t *context, uint64_t seek_timestamp_ns);

libmatroska::KaxTrackEntry *get_track_by_name(k4a_playback_context_t *context, const char *name);
libmatroska::KaxTrackEntry *get_track_by_tag(k4a_playback_context_t *context, const char *tag_name);
libmatroska::KaxTag *get_tag(k4a_playback_context_t *context, const char *name);
std::string get_tag_string(libmatroska::KaxTag *tag);
libmatroska::KaxAttached *get_attachment_by_name(k4a_playback_context_t *context, const char *file_name);
libmatroska::KaxAttached *get_attachment_by_tag(k4a_playback_context_t *context, const char *tag_name);

k4a_result_t seek_offset(k4a_playback_context_t *context, uint64_t offset);
void populate_cluster_info(k4a_playback_context_t *context,
                           std::shared_ptr<libmatroska::KaxCluster> &cluster,
                           cluster_info_t *cluster_info);
cluster_info_t *find_cluster(k4a_playback_context_t *context, uint64_t timestamp_ns);
cluster_info_t *next_cluster(k4a_playback_context_t *context, cluster_info_t *current, bool next);
std::shared_ptr<loaded_cluster_t> load_cluster(k4a_playback_context_t *context, cluster_info_t *cluster_info);
std::shared_ptr<loaded_cluster_t> load_next_cluster(k4a_playback_context_t *context,
                                                    loaded_cluster_t *current_cluster,
                                                    bool next);

std::shared_ptr<block_info_t> find_block(k4a_playback_context_t *context,
                                         track_reader_t *reader,
                                         uint64_t timestamp_ns);
std::shared_ptr<block_info_t> next_block(k4a_playback_context_t *context, block_info_t *current, bool next);

k4a_result_t new_capture(k4a_playback_context_t *context, block_info_t *block, k4a_capture_t *capture_handle);
k4a_stream_result_t get_capture(k4a_playback_context_t *context, k4a_capture_t *capture_handle, bool next);
k4a_stream_result_t get_imu_sample(k4a_playback_context_t *context, k4a_imu_sample_t *imu_sample, bool next);

// Template helper functions
template<typename T> T *read_element(k4a_playback_context_t *context, EbmlElement *element)
{
    try
    {
        int upper_level = 0;
        EbmlElement *dummy = nullptr;

        T *typed_element = static_cast<T *>(element);
        typed_element->Read(*context->stream, T::ClassInfos.Context, upper_level, dummy, true);
        return typed_element;
    }
    catch (std::ios_base::failure &e)
    {
        LOG_ERROR("Failed to read element %s in recording '%s': %s",
                  T::ClassInfos.GetName(),
                  context->file_path,
                  e.what());
        return nullptr;
    }
}

/**
 * Find the next element of type T at the current file offset.
 * If \p search is true, this function will keep reading elements until an element of type T is found or EOF is reached.
 * If \p search is false, this function will only return an element if it exists at the current file offset.
 *
 * Example usage: find_next<KaxSegment>(context, true);
 */
template<typename T> std::unique_ptr<T> find_next(k4a_playback_context_t *context, bool search = false)
{
    try
    {
        EbmlElement *element = nullptr;
        do
        {
            if (element)
            {
                if (!element->IsFiniteSize())
                {
                    LOG_ERROR("Failed to read recording: Element Id '%x' has unknown size",
                              EbmlId(*element).GetValue());
                    delete element;
                    return nullptr;
                }
                element->SkipData(*context->stream, element->Generic().Context);
                delete element;
                element = nullptr;
            }
            if (!element)
            {
                element = context->stream->FindNextID(T::ClassInfos, UINT64_MAX);
            }
            if (!search)
            {
                break;
            }
        } while (element && EbmlId(*element) != T::ClassInfos.GlobalId);

        if (!element)
        {
            if (!search)
            {
                LOG_ERROR("Failed to read recording: Element Id '%x' not found", T::ClassInfos.GlobalId.GetValue());
            }
            return nullptr;
        }
        else if (EbmlId(*element) != T::ClassInfos.GlobalId)
        {
            LOG_ERROR("Failed to read recording: Expected element %s (id %x), found id '%x'",
                      T::ClassInfos.GetName(),
                      T::ClassInfos.GlobalId.GetValue(),
                      EbmlId(*element).GetValue());
            delete element;
            return nullptr;
        }

        return std::unique_ptr<T>(static_cast<T *>(element));
    }
    catch (std::ios_base::failure &e)
    {
        LOG_ERROR("Failed to find %s in recording '%s': %s", T::ClassInfos.GetName(), context->file_path, e.what());
        return nullptr;
    }
}

template<typename T>
k4a_result_t read_offset(k4a_playback_context_t *context, std::unique_ptr<T> &element_out, uint64_t offset)
{
    RETURN_VALUE_IF_ARG(K4A_RESULT_FAILED, context == NULL);
    RETURN_VALUE_IF_ARG(K4A_RESULT_FAILED, offset == 0);

    RETURN_IF_ERROR(seek_offset(context, offset));
    element_out = find_next<T>(context);

    if (element_out)
    {
        if (read_element<T>(context, element_out.get()) == NULL)
        {
            LOG_ERROR("Failed to read element: %s at offset %llu", typeid(T).name(), offset);
            return K4A_RESULT_FAILED;
        }
        return K4A_RESULT_SUCCEEDED;
    }
    else
    {
        LOG_ERROR("Element not found at offset: %s at offset %llu", typeid(T).name(), offset);
        return K4A_RESULT_FAILED;
    }
}

template<typename T> bool check_element_type(EbmlElement *element, T **out)
{
    if (EbmlId(*element) == T::ClassInfos.GlobalId)
    {
        *out = static_cast<T *>(element);
        return true;
    }
    *out = nullptr;
    return false;
}

} // namespace k4arecord

#endif /* RECORD_READ_H */
