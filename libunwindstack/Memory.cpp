/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <7zCrc.h>
#include <Xz.h>
#include <XzCrc64.h>

#include <algorithm>
#include <memory>
#include <mutex>

#include <android-base/unique_fd.h>

#include <unwindstack/Log.h>
#include <unwindstack/Memory.h>

#include "Check.h"
#include "MemoryBuffer.h"
#include "MemoryCache.h"
#include "MemoryFileAtOffset.h"
#include "MemoryLocal.h"
#include "MemoryOffline.h"
#include "MemoryOfflineBuffer.h"
#include "MemoryRange.h"
#include "MemoryRemote.h"
#include "MemoryXz.h"

namespace unwindstack {

// Statistics (used only for optional debug log messages).
static constexpr bool kLogMemoryXzUsage = false;
std::atomic_size_t MemoryXz::total_used_ = 0;
std::atomic_size_t MemoryXz::total_size_ = 0;
std::atomic_size_t MemoryXz::total_open_ = 0;

static size_t ProcessVmRead(pid_t pid, uint64_t remote_src, void* dst, size_t len) {

  // Split up the remote read across page boundaries.
  // From the manpage:
  //   A partial read/write may result if one of the remote_iov elements points to an invalid
  //   memory region in the remote process.
  //
  //   Partial transfers apply at the granularity of iovec elements.  These system calls won't
  //   perform a partial transfer that splits a single iovec element.
  constexpr size_t kMaxIovecs = 64;
  struct iovec src_iovs[kMaxIovecs];

  uint64_t cur = remote_src;
  size_t total_read = 0;
  while (len > 0) {
    struct iovec dst_iov = {
        .iov_base = &reinterpret_cast<uint8_t*>(dst)[total_read], .iov_len = len,
    };

    size_t iovecs_used = 0;
    while (len > 0) {
      if (iovecs_used == kMaxIovecs) {
        break;
      }

      // struct iovec uses void* for iov_base.
      if (cur >= UINTPTR_MAX) {
        errno = EFAULT;
        return total_read;
      }

      src_iovs[iovecs_used].iov_base = reinterpret_cast<void*>(cur);

      uintptr_t misalignment = cur & (getpagesize() - 1);
      size_t iov_len = getpagesize() - misalignment;
      iov_len = std::min(iov_len, len);

      len -= iov_len;
      if (__builtin_add_overflow(cur, iov_len, &cur)) {
        errno = EFAULT;
        return total_read;
      }

      src_iovs[iovecs_used].iov_len = iov_len;
      ++iovecs_used;
    }

    ssize_t rc = process_vm_readv(pid, &dst_iov, 1, src_iovs, iovecs_used, 0);
    if (rc == -1) {
      return total_read;
    }
    total_read += rc;
  }
  return total_read;
}

static bool PtraceReadLong(pid_t pid, uint64_t addr, long* value) {
  // ptrace() returns -1 and sets errno when the operation fails.
  // To disambiguate -1 from a valid result, we clear errno beforehand.
  errno = 0;
  *value = ptrace(PTRACE_PEEKTEXT, pid, reinterpret_cast<void*>(addr), nullptr);
  if (*value == -1 && errno) {
    return false;
  }
  return true;
}

static size_t PtraceRead(pid_t pid, uint64_t addr, void* dst, size_t bytes) {
  // Make sure that there is no overflow.
  uint64_t max_size;
  if (__builtin_add_overflow(addr, bytes, &max_size)) {
    return 0;
  }

  size_t bytes_read = 0;
  long data;
  size_t align_bytes = addr & (sizeof(long) - 1);
  if (align_bytes != 0) {
    if (!PtraceReadLong(pid, addr & ~(sizeof(long) - 1), &data)) {
      return 0;
    }
    size_t copy_bytes = std::min(sizeof(long) - align_bytes, bytes);
    memcpy(dst, reinterpret_cast<uint8_t*>(&data) + align_bytes, copy_bytes);
    addr += copy_bytes;
    dst = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(dst) + copy_bytes);
    bytes -= copy_bytes;
    bytes_read += copy_bytes;
  }

  for (size_t i = 0; i < bytes / sizeof(long); i++) {
    if (!PtraceReadLong(pid, addr, &data)) {
      return bytes_read;
    }
    memcpy(dst, &data, sizeof(long));
    dst = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(dst) + sizeof(long));
    addr += sizeof(long);
    bytes_read += sizeof(long);
  }

  size_t left_over = bytes & (sizeof(long) - 1);
  if (left_over) {
    if (!PtraceReadLong(pid, addr, &data)) {
      return bytes_read;
    }
    memcpy(dst, &data, left_over);
    bytes_read += left_over;
  }
  return bytes_read;
}

bool Memory::ReadFully(uint64_t addr, void* dst, size_t size) {
  size_t rc = Read(addr, dst, size);
  return rc == size;
}

bool Memory::ReadString(uint64_t addr, std::string* dst, size_t max_read) {
  char buffer[256];  // Large enough for 99% of symbol names.
  size_t size = 0;   // Number of bytes which were read into the buffer.
  for (size_t offset = 0; offset < max_read; offset += size) {
    // Look for null-terminator first, so we can allocate string of exact size.
    // If we know the end of valid memory range, do the reads in larger blocks.
    size_t read = std::min(sizeof(buffer), max_read - offset);
    size = Read(addr + offset, buffer, read);
    if (size == 0) {
      return false;  // We have not found end of string yet and we can not read more data.
    }
    size_t length = strnlen(buffer, size);  // Index of the null-terminator.
    if (length < size) {
      // We found the null-terminator. Allocate the string and set its content.
      if (offset == 0) {
        // We did just single read, so the buffer already contains the whole string.
        dst->assign(buffer, length);
        return true;
      } else {
        // The buffer contains only the last block. Read the whole string again.
        dst->assign(offset + length, '\0');
        return ReadFully(addr, dst->data(), dst->size());
      }
    }
  }
  return false;
}

std::unique_ptr<Memory> Memory::CreateFileMemory(const std::string& path, uint64_t offset,
                                                 uint64_t size) {
  auto memory = std::make_unique<MemoryFileAtOffset>();

  if (memory->Init(path, offset, size)) {
    return memory;
  }

  return nullptr;
}

std::shared_ptr<Memory> Memory::CreateProcessMemory(pid_t pid) {
  if (pid == getpid()) {
    return std::shared_ptr<Memory>(new MemoryLocal());
  }
  return std::shared_ptr<Memory>(new MemoryRemote(pid));
}

std::shared_ptr<Memory> Memory::CreateProcessMemoryCached(pid_t pid) {
  if (pid == getpid()) {
    return std::shared_ptr<Memory>(new MemoryCache(new MemoryLocal()));
  }
  return std::shared_ptr<Memory>(new MemoryCache(new MemoryRemote(pid)));
}

std::shared_ptr<Memory> Memory::CreateOfflineMemory(const uint8_t* data, uint64_t start,
                                                    uint64_t end) {
  return std::shared_ptr<Memory>(new MemoryOfflineBuffer(data, start, end));
}

size_t MemoryBuffer::Read(uint64_t addr, void* dst, size_t size) {
  if (addr >= size_) {
    return 0;
  }

  size_t bytes_left = size_ - static_cast<size_t>(addr);
  const unsigned char* actual_base = static_cast<const unsigned char*>(raw_) + addr;
  size_t actual_len = std::min(bytes_left, size);

  memcpy(dst, actual_base, actual_len);
  return actual_len;
}

uint8_t* MemoryBuffer::GetPtr(size_t offset) {
  if (offset < size_) {
    return &raw_[offset];
  }
  return nullptr;
}

MemoryFileAtOffset::~MemoryFileAtOffset() {
  Clear();
}

void MemoryFileAtOffset::Clear() {
  if (data_) {
    munmap(&data_[-offset_], size_ + offset_);
    data_ = nullptr;
  }
}

bool MemoryFileAtOffset::Init(const std::string& file, uint64_t offset, uint64_t size) {
  // Clear out any previous data if it exists.
  Clear();

  android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(file.c_str(), O_RDONLY | O_CLOEXEC)));
  if (fd == -1) {
    return false;
  }
  struct stat buf;
  if (fstat(fd, &buf) == -1) {
    return false;
  }
  if (offset >= static_cast<uint64_t>(buf.st_size)) {
    return false;
  }

  offset_ = offset & (getpagesize() - 1);
  uint64_t aligned_offset = offset & ~(getpagesize() - 1);
  if (aligned_offset > static_cast<uint64_t>(buf.st_size) ||
      offset > static_cast<uint64_t>(buf.st_size)) {
    return false;
  }

  size_ = buf.st_size - aligned_offset;
  uint64_t max_size;
  if (!__builtin_add_overflow(size, offset_, &max_size) && max_size < size_) {
    // Truncate the mapped size.
    size_ = max_size;
  }
  void* map = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, aligned_offset);
  if (map == MAP_FAILED) {
    return false;
  }

  data_ = &reinterpret_cast<uint8_t*>(map)[offset_];
  size_ -= offset_;

  return true;
}

size_t MemoryFileAtOffset::Read(uint64_t addr, void* dst, size_t size) {
  if (addr >= size_) {
    return 0;
  }

  size_t bytes_left = size_ - static_cast<size_t>(addr);
  const unsigned char* actual_base = static_cast<const unsigned char*>(data_) + addr;
  size_t actual_len = std::min(bytes_left, size);

  memcpy(dst, actual_base, actual_len);
  return actual_len;
}

size_t MemoryRemote::Read(uint64_t addr, void* dst, size_t size) {
#if !defined(__LP64__)
  // Cannot read an address greater than 32 bits in a 32 bit context.
  if (addr > UINT32_MAX) {
    return 0;
  }
#endif

  size_t (*read_func)(pid_t, uint64_t, void*, size_t) =
      reinterpret_cast<size_t (*)(pid_t, uint64_t, void*, size_t)>(read_redirect_func_.load());
  if (read_func != nullptr) {
    return read_func(pid_, addr, dst, size);
  } else {
    // Prefer process_vm_read, try it first. If it doesn't work, use the
    // ptrace function. If at least one of them returns at least some data,
    // set that as the permanent function to use.
    // This assumes that if process_vm_read works once, it will continue
    // to work.
    size_t bytes = ProcessVmRead(pid_, addr, dst, size);
    if (bytes > 0) {
      read_redirect_func_ = reinterpret_cast<uintptr_t>(ProcessVmRead);
      return bytes;
    }
    bytes = PtraceRead(pid_, addr, dst, size);
    if (bytes > 0) {
      read_redirect_func_ = reinterpret_cast<uintptr_t>(PtraceRead);
    }
    return bytes;
  }
}

size_t MemoryLocal::Read(uint64_t addr, void* dst, size_t size) {
  return ProcessVmRead(getpid(), addr, dst, size);
}

MemoryRange::MemoryRange(const std::shared_ptr<Memory>& memory, uint64_t begin, uint64_t length,
                         uint64_t offset)
    : memory_(memory), begin_(begin), length_(length), offset_(offset) {}

size_t MemoryRange::Read(uint64_t addr, void* dst, size_t size) {
  if (addr < offset_) {
    return 0;
  }

  uint64_t read_offset = addr - offset_;
  if (read_offset >= length_) {
    return 0;
  }

  uint64_t read_length = std::min(static_cast<uint64_t>(size), length_ - read_offset);
  uint64_t read_addr;
  if (__builtin_add_overflow(read_offset, begin_, &read_addr)) {
    return 0;
  }

  return memory_->Read(read_addr, dst, read_length);
}

void MemoryRanges::Insert(MemoryRange* memory) {
  uint64_t last_addr;
  if (__builtin_add_overflow(memory->offset(), memory->length(), &last_addr)) {
    // This should never happen in the real world. However, it is possible
    // that an offset in a mapped in segment could be crafted such that
    // this value overflows. In that case, clamp the value to the max uint64
    // value.
    last_addr = UINT64_MAX;
  }
  maps_.emplace(last_addr, memory);
}

size_t MemoryRanges::Read(uint64_t addr, void* dst, size_t size) {
  auto entry = maps_.upper_bound(addr);
  if (entry != maps_.end()) {
    return entry->second->Read(addr, dst, size);
  }
  return 0;
}

bool MemoryOffline::Init(const std::string& file, uint64_t offset) {
  auto memory_file = std::make_shared<MemoryFileAtOffset>();
  if (!memory_file->Init(file, offset)) {
    return false;
  }

  // The first uint64_t value is the start of memory.
  uint64_t start;
  if (!memory_file->ReadFully(0, &start, sizeof(start))) {
    return false;
  }

  uint64_t size = memory_file->Size();
  if (__builtin_sub_overflow(size, sizeof(start), &size)) {
    return false;
  }

  memory_ = std::make_unique<MemoryRange>(memory_file, sizeof(start), size, start);
  return true;
}

size_t MemoryOffline::Read(uint64_t addr, void* dst, size_t size) {
  if (!memory_) {
    return 0;
  }

  return memory_->Read(addr, dst, size);
}

MemoryOfflineBuffer::MemoryOfflineBuffer(const uint8_t* data, uint64_t start, uint64_t end)
    : data_(data), start_(start), end_(end) {}

void MemoryOfflineBuffer::Reset(const uint8_t* data, uint64_t start, uint64_t end) {
  data_ = data;
  start_ = start;
  end_ = end;
}

size_t MemoryOfflineBuffer::Read(uint64_t addr, void* dst, size_t size) {
  if (addr < start_ || addr >= end_) {
    return 0;
  }

  size_t read_length = std::min(size, static_cast<size_t>(end_ - addr));
  memcpy(dst, &data_[addr - start_], read_length);
  return read_length;
}

MemoryOfflineParts::~MemoryOfflineParts() {
  for (auto memory : memories_) {
    delete memory;
  }
}

size_t MemoryOfflineParts::Read(uint64_t addr, void* dst, size_t size) {
  if (memories_.empty()) {
    return 0;
  }

  // Do a read on each memory object, no support for reading across the
  // different memory objects.
  for (MemoryOffline* memory : memories_) {
    size_t bytes = memory->Read(addr, dst, size);
    if (bytes != 0) {
      return bytes;
    }
  }
  return 0;
}

size_t MemoryCache::Read(uint64_t addr, void* dst, size_t size) {
  // Only bother caching and looking at the cache if this is a small read for now.
  if (size > 64) {
    return impl_->Read(addr, dst, size);
  }

  uint64_t addr_page = addr >> kCacheBits;
  auto entry = cache_.find(addr_page);
  uint8_t* cache_dst;
  if (entry != cache_.end()) {
    cache_dst = entry->second;
  } else {
    cache_dst = cache_[addr_page];
    if (!impl_->ReadFully(addr_page << kCacheBits, cache_dst, kCacheSize)) {
      // Erase the entry.
      cache_.erase(addr_page);
      return impl_->Read(addr, dst, size);
    }
  }
  size_t max_read = ((addr_page + 1) << kCacheBits) - addr;
  if (size <= max_read) {
    memcpy(dst, &cache_dst[addr & kCacheMask], size);
    return size;
  }

  // The read crossed into another cached entry, since a read can only cross
  // into one extra cached page, duplicate the code rather than looping.
  memcpy(dst, &cache_dst[addr & kCacheMask], max_read);
  dst = &reinterpret_cast<uint8_t*>(dst)[max_read];
  addr_page++;

  entry = cache_.find(addr_page);
  if (entry != cache_.end()) {
    cache_dst = entry->second;
  } else {
    cache_dst = cache_[addr_page];
    if (!impl_->ReadFully(addr_page << kCacheBits, cache_dst, kCacheSize)) {
      // Erase the entry.
      cache_.erase(addr_page);
      return impl_->Read(addr_page << kCacheBits, dst, size - max_read) + max_read;
    }
  }
  memcpy(dst, cache_dst, size - max_read);
  return size;
}

MemoryXz::MemoryXz(Memory* memory, uint64_t addr, uint64_t size, const std::string& name)
    : compressed_memory_(memory), compressed_addr_(addr), compressed_size_(size), name_(name) {
  total_open_ += 1;
}

bool MemoryXz::Init() {
  static std::once_flag crc_initialized;
  std::call_once(crc_initialized, []() {
    CrcGenerateTable();
    Crc64GenerateTable();
  });
  if (compressed_size_ >= kMaxCompressedSize) {
    return false;
  }
  if (!ReadBlocks()) {
    return false;
  }

  // All blocks (except the last one) must have the same power-of-2 size.
  if (blocks_.size() > 1) {
    size_t block_size_log2 = __builtin_ctz(blocks_.front().decompressed_size);
    auto correct_size = [=](XzBlock& b) { return b.decompressed_size == (1 << block_size_log2); };
    if (std::all_of(blocks_.begin(), std::prev(blocks_.end()), correct_size) &&
        blocks_.back().decompressed_size <= (1 << block_size_log2)) {
      block_size_log2_ = block_size_log2;
    } else {
      // Inconsistent block-sizes.  Decompress and merge everything now.
      std::unique_ptr<uint8_t[]> data(new uint8_t[size_]);
      size_t offset = 0;
      for (XzBlock& block : blocks_) {
        if (!Decompress(&block)) {
          return false;
        }
        memcpy(data.get() + offset, block.decompressed_data.get(), block.decompressed_size);
        offset += block.decompressed_size;
      }
      blocks_.clear();
      blocks_.push_back(XzBlock{
          .decompressed_data = std::move(data),
          .decompressed_size = size_,
      });
      block_size_log2_ = 31;  // Because 32 bits is too big (shift right by 32 is not allowed).
    }
  }

  return true;
}

MemoryXz::~MemoryXz() {
  total_used_ -= used_;
  total_size_ -= size_;
  total_open_ -= 1;
}

size_t MemoryXz::Read(uint64_t addr, void* buffer, size_t size) {
  if (addr >= size_) {
    return 0;  // Read past the end.
  }
  uint8_t* dst = reinterpret_cast<uint8_t*>(buffer);  // Position in the output buffer.
  for (size_t i = addr >> block_size_log2_; i < blocks_.size(); i++) {
    XzBlock* block = &blocks_[i];
    if (block->decompressed_data == nullptr) {
      if (!Decompress(block)) {
        break;
      }
    }
    size_t offset = (addr - (i << block_size_log2_));  // Start inside the block.
    size_t copy_bytes = std::min<size_t>(size, block->decompressed_size - offset);
    memcpy(dst, block->decompressed_data.get() + offset, copy_bytes);
    dst += copy_bytes;
    addr += copy_bytes;
    size -= copy_bytes;
    if (size == 0) {
      break;
    }
  }
  return dst - reinterpret_cast<uint8_t*>(buffer);
}

bool MemoryXz::ReadBlocks() {
  static ISzAlloc alloc;
  alloc.Alloc = [](ISzAllocPtr, size_t size) { return malloc(size); };
  alloc.Free = [](ISzAllocPtr, void* ptr) { return free(ptr); };

  // Read the compressed data, so we can quickly scan through the headers.
  std::unique_ptr<uint8_t[]> compressed_data(new (std::nothrow) uint8_t[compressed_size_]);
  if (compressed_data.get() == nullptr) {
    return false;
  }
  if (!compressed_memory_->ReadFully(compressed_addr_, compressed_data.get(), compressed_size_)) {
    return false;
  }

  // Implement the required interface for communication
  // (written in C so we can not use virtual methods or member functions).
  struct XzLookInStream : public ILookInStream, public ICompressProgress {
    static SRes LookImpl(const ILookInStream* p, const void** buf, size_t* size) {
      auto* ctx = reinterpret_cast<const XzLookInStream*>(p);
      *buf = ctx->data + ctx->offset;
      *size = std::min(*size, ctx->size - ctx->offset);
      return SZ_OK;
    }
    static SRes SkipImpl(const ILookInStream* p, size_t len) {
      auto* ctx = reinterpret_cast<XzLookInStream*>(const_cast<ILookInStream*>(p));
      ctx->offset += len;
      return SZ_OK;
    }
    static SRes ReadImpl(const ILookInStream* p, void* buf, size_t* size) {
      auto* ctx = reinterpret_cast<const XzLookInStream*>(p);
      *size = std::min(*size, ctx->size - ctx->offset);
      memcpy(buf, ctx->data + ctx->offset, *size);
      return SZ_OK;
    }
    static SRes SeekImpl(const ILookInStream* p, Int64* pos, ESzSeek origin) {
      auto* ctx = reinterpret_cast<XzLookInStream*>(const_cast<ILookInStream*>(p));
      switch (origin) {
        case SZ_SEEK_SET:
          ctx->offset = *pos;
          break;
        case SZ_SEEK_CUR:
          ctx->offset += *pos;
          break;
        case SZ_SEEK_END:
          ctx->offset = ctx->size + *pos;
          break;
      }
      *pos = ctx->offset;
      return SZ_OK;
    }
    static SRes ProgressImpl(const ICompressProgress*, UInt64, UInt64) { return SZ_OK; }
    size_t offset;
    uint8_t* data;
    size_t size;
  };
  XzLookInStream callbacks;
  callbacks.Look = &XzLookInStream::LookImpl;
  callbacks.Skip = &XzLookInStream::SkipImpl;
  callbacks.Read = &XzLookInStream::ReadImpl;
  callbacks.Seek = &XzLookInStream::SeekImpl;
  callbacks.Progress = &XzLookInStream::ProgressImpl;
  callbacks.offset = 0;
  callbacks.data = compressed_data.get();
  callbacks.size = compressed_size_;

  // Iterate over the internal XZ blocks without decompressing them.
  CXzs xzs;
  Xzs_Construct(&xzs);
  Int64 end_offset = compressed_size_;
  if (Xzs_ReadBackward(&xzs, &callbacks, &end_offset, &callbacks, &alloc) == SZ_OK) {
    blocks_.reserve(Xzs_GetNumBlocks(&xzs));
    size_t dst_offset = 0;
    for (int s = xzs.num - 1; s >= 0; s--) {
      const CXzStream& stream = xzs.streams[s];
      size_t src_offset = stream.startOffset + XZ_STREAM_HEADER_SIZE;
      for (size_t b = 0; b < stream.numBlocks; b++) {
        const CXzBlockSizes& block = stream.blocks[b];
        blocks_.push_back(XzBlock{
            .decompressed_data = nullptr,  // Lazy allocation and decompression.
            .decompressed_size = static_cast<uint32_t>(block.unpackSize),
            .compressed_offset = static_cast<uint32_t>(src_offset),
            .compressed_size = static_cast<uint32_t>((block.totalSize + 3) & ~3u),
            .stream_flags = stream.flags,
        });
        dst_offset += blocks_.back().decompressed_size;
        src_offset += blocks_.back().compressed_size;
      }
    }
    size_ = dst_offset;
    total_size_ += dst_offset;
  }
  Xzs_Free(&xzs, &alloc);
  return !blocks_.empty();
}

bool MemoryXz::Decompress(XzBlock* block) {
  static ISzAlloc alloc;
  alloc.Alloc = [](ISzAllocPtr, size_t size) { return malloc(size); };
  alloc.Free = [](ISzAllocPtr, void* ptr) { return free(ptr); };

  // Read the compressed data for this block.
  std::unique_ptr<uint8_t[]> compressed_data(new (std::nothrow) uint8_t[block->compressed_size]);
  if (compressed_data.get() == nullptr) {
    return false;
  }
  if (!compressed_memory_->ReadFully(compressed_addr_ + block->compressed_offset,
                                     compressed_data.get(), block->compressed_size)) {
    return false;
  }

  // Allocate decompressed memory.
  std::unique_ptr<uint8_t[]> decompressed_data(new uint8_t[block->decompressed_size]);
  if (decompressed_data == nullptr) {
    return false;
  }

  // Decompress.
  CXzUnpacker state{};
  XzUnpacker_Construct(&state, &alloc);
  state.streamFlags = block->stream_flags;
  XzUnpacker_PrepareToRandomBlockDecoding(&state);
  size_t decompressed_size = block->decompressed_size;
  size_t compressed_size = block->compressed_size;
  ECoderStatus status;
  XzUnpacker_SetOutBuf(&state, decompressed_data.get(), decompressed_size);
  int return_val =
      XzUnpacker_Code(&state, /*decompressed_data=*/nullptr, &decompressed_size,
                      compressed_data.get(), &compressed_size, true, CODER_FINISH_END, &status);
  XzUnpacker_Free(&state);
  if (return_val != SZ_OK || status != CODER_STATUS_FINISHED_WITH_MARK) {
    log(0, "Can not decompress \"%s\"", name_.c_str());
    return false;
  }

  used_ += block->decompressed_size;
  total_used_ += block->decompressed_size;
  if (kLogMemoryXzUsage) {
    log(0, "decompressed memory: %zi%% of %ziKB (%zi files), %i%% of %iKB (%s)",
        100 * total_used_ / total_size_, total_size_ / 1024, total_open_.load(),
        100 * used_ / size_, size_ / 1024, name_.c_str());
  }

  block->decompressed_data = std::move(decompressed_data);
  return true;
}

}  // namespace unwindstack
