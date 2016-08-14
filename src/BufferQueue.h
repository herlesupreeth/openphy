#ifndef _BUFFER_QUEUE_H_
#define _BUFFER_QUEUE_H_

#include <memory>
#include <mutex>
#include <queue>

#include "LteBuffer.h"

class BufferQueue {
public:
    BufferQueue() = default;
    BufferQueue(const BufferQueue &q);
    ~BufferQueue() = default;

    BufferQueue &operator=(const BufferQueue &q);

    size_t size();

    std::shared_ptr<LteBuffer> read();
    bool write(std::shared_ptr<LteBuffer> buf);

private:
    std::mutex mutex;
    std::queue<std::shared_ptr<LteBuffer>> q;
};
#endif /* _BUFFER_QUEUE_ */
