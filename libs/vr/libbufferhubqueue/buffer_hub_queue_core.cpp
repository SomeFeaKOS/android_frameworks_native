#include "include/private/dvr/buffer_hub_queue_core.h"

#include <log/log.h>

namespace android {
namespace dvr {

/* static */
std::shared_ptr<BufferHubQueueCore> BufferHubQueueCore::Create() {
  auto core = std::shared_ptr<BufferHubQueueCore>(new BufferHubQueueCore());
  core->producer_ = ProducerQueue::Create<BufferMetadata>();
  return core;
}

/* static */
std::shared_ptr<BufferHubQueueCore> BufferHubQueueCore::Create(
    const std::shared_ptr<ProducerQueue>& producer) {
  if (producer->metadata_size() != sizeof(BufferMetadata)) {
    ALOGE(
        "BufferHubQueueCore::Create producer's metadata size is different than "
        "the size of BufferHubQueueCore::BufferMetadata");
    return nullptr;
  }

  auto core = std::shared_ptr<BufferHubQueueCore>(new BufferHubQueueCore());
  core->producer_ = producer;
  return core;
}

BufferHubQueueCore::BufferHubQueueCore()
    : generation_number_(0),
      dequeue_timeout_ms_(BufferHubQueue::kNoTimeOut),
      unique_id_(getUniqueId()) {}

status_t BufferHubQueueCore::AllocateBuffer(uint32_t width, uint32_t height,
                                            PixelFormat format, uint32_t usage,
                                            size_t slice_count) {
  size_t slot;

  // Allocate new buffer through BufferHub and add it into |producer_| queue for
  // bookkeeping.
  if (producer_->AllocateBuffer(width, height, format, usage, slice_count,
                                &slot) < 0) {
    ALOGE("Failed to allocate new buffer in BufferHub.");
    return NO_MEMORY;
  }

  auto buffer_producer = producer_->GetBuffer(slot);

  LOG_ALWAYS_FATAL_IF(buffer_producer == nullptr,
                      "Failed to get buffer producer at slot: %zu", slot);

  // Allocating a new buffer, |buffers_[slot]| should be in initial state.
  LOG_ALWAYS_FATAL_IF(buffers_[slot].mGraphicBuffer != nullptr,
                      "AllocateBuffer: slot %zu is not empty.", slot);

  // Create new GraphicBuffer based on the newly created |buffer_producer|. Here
  // we have to cast |buffer_handle_t| to |native_handle_t|, it's OK because
  // internally, GraphicBuffer is still an |ANativeWindowBuffer| and |handle|
  // is still type of |buffer_handle_t| and bears const property.
  sp<GraphicBuffer> graphic_buffer(new GraphicBuffer(
      buffer_producer->width(), buffer_producer->height(),
      buffer_producer->format(),
      1, /* layer count */
      buffer_producer->usage(),
      buffer_producer->stride(),
      const_cast<native_handle_t*>(buffer_producer->buffer()->handle()),
      false));

  LOG_ALWAYS_FATAL_IF(NO_ERROR != graphic_buffer->initCheck(),
                      "Failed to init GraphicBuffer.");
  buffers_[slot].mBufferProducer = buffer_producer;
  buffers_[slot].mGraphicBuffer = graphic_buffer;

  return NO_ERROR;
}

status_t BufferHubQueueCore::DetachBuffer(size_t slot) {
  // Detach the buffer producer via BufferHubRPC.
  int ret = producer_->DetachBuffer(slot);
  if (ret < 0) {
    ALOGE("BufferHubQueueCore::DetachBuffer failed through RPC, ret=%s",
          strerror(-ret));
    return ret;
  }

  // Reset in memory objects related the the buffer.
  buffers_[slot].mBufferProducer = nullptr;
  buffers_[slot].mGraphicBuffer = nullptr;
  buffers_[slot].mBufferState.detachProducer();
  return NO_ERROR;
}

}  // namespace dvr
}  // namespace android