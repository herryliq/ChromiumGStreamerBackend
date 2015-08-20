// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/utility/safe_browsing/mac/udif.h"

#include <CoreFoundation/CoreFoundation.h>
#include <libkern/OSByteOrder.h>
#include <uuid/uuid.h>

#include <algorithm>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/numerics/safe_math.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/utility/safe_browsing/mac/convert_big_endian.h"
#include "chrome/utility/safe_browsing/mac/read_stream.h"
#include "third_party/zlib/zlib.h"

namespace safe_browsing {
namespace dmg {

#pragma pack(push, 1)

// The following structures come from the analysis provided by Jonathan Levin
// at <http://newosxbook.com/DMG.html>.
//
// Note that all fields are stored in big endian.

struct UDIFChecksum {
  uint32_t type;
  uint32_t size;
  uint32_t data[32];
};

static void ConvertBigEndian(UDIFChecksum* checksum) {
  ConvertBigEndian(&checksum->type);
  ConvertBigEndian(&checksum->size);
  for (size_t i = 0; i < arraysize(checksum->data); ++i) {
    ConvertBigEndian(&checksum->data[i]);
  }
}

// The trailer structure for a UDIF file.
struct UDIFResourceFile {
  static const uint32_t kSignature = 'koly';
  static const uint32_t kVersion = 4;

  uint32_t signature;
  uint32_t version;
  uint32_t header_size;  // Size of this structure.
  uint32_t flags;
  uint64_t running_data_fork_offset;
  uint64_t data_fork_offset;
  uint64_t data_fork_length;
  uint64_t rsrc_fork_offset;
  uint64_t rsrc_fork_length;
  uint32_t segment_number;
  uint32_t segment_count;
  uuid_t   segment_id;

  UDIFChecksum data_checksum;

  uint64_t plist_offset;  // Offset and length of the blkx plist.
  uint64_t plist_length;

  uint8_t  reserved1[120];

  UDIFChecksum master_checksum;

  uint32_t image_variant;
  uint64_t sector_count;

  uint32_t reserved2;
  uint32_t reserved3;
  uint32_t reserved4;
};

static void ConvertBigEndian(uuid_t* uuid) {
  // UUID is never consulted, so do not swap.
}

static void ConvertBigEndian(UDIFResourceFile* file) {
  ConvertBigEndian(&file->signature);
  ConvertBigEndian(&file->version);
  ConvertBigEndian(&file->flags);
  ConvertBigEndian(&file->header_size);
  ConvertBigEndian(&file->running_data_fork_offset);
  ConvertBigEndian(&file->data_fork_offset);
  ConvertBigEndian(&file->data_fork_length);
  ConvertBigEndian(&file->rsrc_fork_offset);
  ConvertBigEndian(&file->rsrc_fork_length);
  ConvertBigEndian(&file->segment_number);
  ConvertBigEndian(&file->segment_count);
  ConvertBigEndian(&file->segment_id);
  ConvertBigEndian(&file->data_checksum);
  ConvertBigEndian(&file->plist_offset);
  ConvertBigEndian(&file->plist_length);
  ConvertBigEndian(&file->master_checksum);
  ConvertBigEndian(&file->image_variant);
  ConvertBigEndian(&file->sector_count);
  // Reserved fields are skipped.
}

struct UDIFBlockChunk {
  enum class Type : uint32_t {
    ZERO_FILL     = 0x00000000,
    UNCOMPRESSED  = 0x00000001,
    IGNORED       = 0x00000002,
    COMPRESS_ADC  = 0x80000004,
    COMPRESS_ZLIB = 0x80000005,
    COMPRESSS_BZ2 = 0x80000006,
    COMMENT       = 0x7ffffffe,
    LAST_BLOCK    = 0xffffffff,
  };

  Type type;
  uint32_t comment;
  uint64_t start_sector;  // Logical chunk offset and length, in sectors.
  uint64_t sector_count;
  uint64_t compressed_offset;  // Compressed offset and length, in bytes.
  uint64_t compressed_length;
};

static void ConvertBigEndian(UDIFBlockChunk* chunk) {
  ConvertBigEndian(reinterpret_cast<uint32_t*>(&chunk->type));
  ConvertBigEndian(&chunk->comment);
  ConvertBigEndian(&chunk->start_sector);
  ConvertBigEndian(&chunk->sector_count);
  ConvertBigEndian(&chunk->compressed_offset);
  ConvertBigEndian(&chunk->compressed_length);
}

struct UDIFBlockData {
  static const uint32_t kSignature = 'mish';
  static const uint32_t kVersion = 1;

  uint32_t signature;
  uint32_t version;
  uint64_t start_sector;  // Logical block offset and length, in sectors.
  uint64_t sector_count;

  uint64_t data_offset;
  uint32_t buffers_needed;
  uint32_t block_descriptors;

  uint32_t reserved1;
  uint32_t reserved2;
  uint32_t reserved3;
  uint32_t reserved4;
  uint32_t reserved5;
  uint32_t reserved6;

  UDIFChecksum checksum;

  uint32_t chunk_count;
  UDIFBlockChunk chunks[0];
};

static void ConvertBigEndian(UDIFBlockData* block) {
  ConvertBigEndian(&block->signature);
  ConvertBigEndian(&block->version);
  ConvertBigEndian(&block->start_sector);
  ConvertBigEndian(&block->sector_count);
  ConvertBigEndian(&block->data_offset);
  ConvertBigEndian(&block->buffers_needed);
  ConvertBigEndian(&block->block_descriptors);
  // Reserved fields are skipped.
  ConvertBigEndian(&block->checksum);
  ConvertBigEndian(&block->chunk_count);
  // Note: This deliberately does not swap the chunks themselves.
}

// UDIFBlock takes a raw, big-endian block data pointer and stores, in host
// endian, the data for both the block and the chunk.
class UDIFBlock {
 public:
  explicit UDIFBlock(const UDIFBlockData* block_data) : block(*block_data) {
    ConvertBigEndian(&block);
    for (uint32_t i = 0; i < block.chunk_count; ++i) {
      chunks.push_back(block_data->chunks[i]);
      ConvertBigEndian(&chunks[i]);
    }
  }

  uint32_t signature() const { return block.signature; }
  uint32_t version() const { return block.version; }
  uint64_t start_sector() const { return block.start_sector; }
  uint64_t sector_count() const { return block.sector_count; }
  uint64_t chunk_count() const { return chunks.size(); }

  const UDIFBlockChunk* chunk(uint32_t i) const {
    if (i >= chunk_count())
      return nullptr;
    return &chunks[i];
  }

 private:
  UDIFBlockData block;
  std::vector<UDIFBlockChunk> chunks;

  DISALLOW_COPY_AND_ASSIGN(UDIFBlock);
};

#pragma pack(pop)

namespace {

const size_t kSectorSize = 512;

class UDIFBlockChunkReadStream;

// A UDIFPartitionReadStream virtualizes a partition's non-contiguous blocks
// into a single stream.
class UDIFPartitionReadStream : public ReadStream {
 public:
  UDIFPartitionReadStream(ReadStream* stream,
                          uint16_t block_size,
                          const UDIFBlock* partition_block);
  ~UDIFPartitionReadStream() override;

  bool Read(uint8_t* buffer, size_t buffer_size, size_t* bytes_read) override;
  // Seek only supports SEEK_SET and SEEK_CUR.
  off_t Seek(off_t offset, int whence) override;

 private:
  ReadStream* const stream_;  // The UDIF stream.
  const uint16_t block_size_;  // The UDIF block size.
  const UDIFBlock* const block_;  // The block for this partition.
  uint64_t current_chunk_;  // The current chunk number.
  // The current chunk stream.
  scoped_ptr<UDIFBlockChunkReadStream> chunk_stream_;

  DISALLOW_COPY_AND_ASSIGN(UDIFPartitionReadStream);
};

// A ReadStream for a single block chunk, which transparently handles
// decompression.
class UDIFBlockChunkReadStream : public ReadStream {
 public:
  UDIFBlockChunkReadStream(ReadStream* stream,
                           uint16_t block_size,
                           const UDIFBlockChunk* chunk);
  ~UDIFBlockChunkReadStream() override;

  bool Read(uint8_t* buffer, size_t buffer_size, size_t* bytes_read) override;
  // Seek only supports SEEK_SET.
  off_t Seek(off_t offset, int whence) override;

  bool IsAtEnd() { return offset_ >= length_in_bytes_; }

  const UDIFBlockChunk* chunk() const { return chunk_; }
  size_t length_in_bytes() const { return length_in_bytes_; }

 private:
  bool CopyOutZeros(uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
  bool CopyOutUncompressed(
      uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
  bool CopyOutDecompressed(
      uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
  bool HandleADC(uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
  bool HandleZLib(uint8_t* buffer, size_t buffer_size, size_t* bytes_read);
  bool HandleBZ2(uint8_t* buffer, size_t buffer_size, size_t* bytes_read);

  ReadStream* const stream_;  // The UDIF stream.
  const UDIFBlockChunk* const chunk_;  // The chunk to be read.
  size_t length_in_bytes_;  // The decompressed length in bytes.
  size_t offset_;  // The offset into the decompressed buffer.
  std::vector<uint8_t> decompress_buffer_;  // Decompressed data buffer.
  bool did_decompress_;  // Whether or not the chunk has been decompressed.
  union {
    z_stream zlib;
  } decompressor_;  // Union for any decompressor state.
                    // Tagged by |chunk_->type|.

  DISALLOW_COPY_AND_ASSIGN(UDIFBlockChunkReadStream);
};

}  // namespace

UDIFParser::UDIFParser(ReadStream* stream)
    : stream_(stream),
      partition_names_(),
      blocks_(),
      block_size_(kSectorSize) {
}

UDIFParser::~UDIFParser() {}

bool UDIFParser::Parse() {
  if (!ParseBlkx())
    return false;

  return true;
}

size_t UDIFParser::GetNumberOfPartitions() {
  return blocks_.size();
}

std::string UDIFParser::GetPartitionName(size_t part_number) {
  DCHECK_LT(part_number, partition_names_.size());
  return partition_names_[part_number];
}

std::string UDIFParser::GetPartitionType(size_t part_number) {
  // The partition type is embedded in the Name field, as such:
  // "Partition-Name (Partition-Type : Partition-ID)".
  std::string name = GetPartitionName(part_number);
  size_t open = name.rfind('(');
  size_t separator = name.rfind(':');
  if (open == std::string::npos || separator == std::string::npos)
    return std::string();

  // Name does not end in ')' or no space after ':'.
  if (*(name.end() - 1) != ')' ||
      (name.size() - separator < 2 || name[separator + 1] != ' ')) {
    return std::string();
  }

  --separator;
  ++open;
  if (separator <= open)
    return std::string();
  return name.substr(open, separator - open);
}

size_t UDIFParser::GetPartitionSize(size_t part_number) {
  DCHECK_LT(part_number, blocks_.size());
  auto size =
      base::CheckedNumeric<size_t>(blocks_[part_number]->sector_count()) *
      block_size_;
  return size.ValueOrDie();
}

scoped_ptr<ReadStream> UDIFParser::GetPartitionReadStream(size_t part_number) {
  DCHECK_LT(part_number, blocks_.size());
  return make_scoped_ptr(
      new UDIFPartitionReadStream(stream_, block_size_, blocks_[part_number]));
}

bool UDIFParser::ParseBlkx() {
  UDIFResourceFile trailer;
  if (stream_->Seek(-sizeof(trailer), SEEK_END) == -1)
    return false;

  if (!stream_->ReadType(&trailer)) {
    DLOG(ERROR) << "Failed to read UDIFResourceFile";
    return false;
  }
  ConvertBigEndian(&trailer);

  if (trailer.signature != trailer.kSignature) {
    DLOG(ERROR) << "blkx signature does not match, is 0x"
                << std::hex << trailer.signature;
    return false;
  }
  if (trailer.version != trailer.kVersion) {
    DLOG(ERROR) << "blkx version does not match, is " << trailer.version;
    return false;
  }

  std::vector<uint8_t> plist_bytes(trailer.plist_length, 0);

  if (stream_->Seek(trailer.plist_offset, SEEK_SET) == -1)
    return false;

  if (!stream_->ReadExact(&plist_bytes[0], trailer.plist_length)) {
    DLOG(ERROR) << "Failed to read blkx plist data";
    return false;
  }

  base::ScopedCFTypeRef<CFDataRef> plist_data(
      CFDataCreateWithBytesNoCopy(kCFAllocatorDefault,
          &plist_bytes[0], plist_bytes.size(), kCFAllocatorNull));
  if (!plist_data) {
    DLOG(ERROR) << "Failed to create data from bytes";
    return false;
  }

  CFErrorRef error = nullptr;
  base::ScopedCFTypeRef<CFDictionaryRef> plist(
      base::mac::CFCast<CFDictionaryRef>(
           CFPropertyListCreateWithData(kCFAllocatorDefault,
                                        plist_data,
                                        kCFPropertyListImmutable,
                                        nullptr,
                                        &error)),
      base::scoped_policy::RETAIN);
  base::ScopedCFTypeRef<CFErrorRef> error_ref(error);
  if (error) {
    base::ScopedCFTypeRef<CFStringRef> error_string(
        CFErrorCopyDescription(error));
    DLOG(ERROR) << "Failed to parse XML plist: "
                << base::SysCFStringRefToUTF8(error_string);
    return false;
  }

  if (!plist) {
    DLOG(ERROR) << "Plist is not a dictionary";
    return false;
  }

  auto resource_fork = base::mac::GetValueFromDictionary<CFDictionaryRef>(
      plist.get(), CFSTR("resource-fork"));
  if (!resource_fork) {
    DLOG(ERROR) << "No resource-fork entry in plist";
    return false;
  }

  auto blkx = base::mac::GetValueFromDictionary<CFArrayRef>(resource_fork,
                                                            CFSTR("blkx"));
  if (!blkx) {
    DLOG(ERROR) << "No blkx entry in resource-fork";
    return false;
  }

  for (CFIndex i = 0; i < CFArrayGetCount(blkx); ++i) {
    auto block_dictionary =
        base::mac::CFCast<CFDictionaryRef>(CFArrayGetValueAtIndex(blkx, i));
    auto data = base::mac::GetValueFromDictionary<CFDataRef>(block_dictionary,
                                                             CFSTR("Data"));
    if (!data) {
      DLOG(ERROR) << "Skipping block " << i
                  << " because it has no Data section";
      continue;
    }

    // Copy the block table out of the plist.
    auto block_data =
        reinterpret_cast<const UDIFBlockData*>(CFDataGetBytePtr(data));
    scoped_ptr<UDIFBlock> block(new UDIFBlock(block_data));

    if (block->signature() != UDIFBlockData::kSignature) {
      DLOG(ERROR) << "Skipping block " << i << " because its signature does not"
                  << " match, is 0x" << std::hex << block->signature();
      continue;
    }
    if (block->version() != UDIFBlockData::kVersion) {
      DLOG(ERROR) << "Skipping block " << i << "because its version does not "
                  << "match, is " << block->version();
      continue;
    }

    CFStringRef partition_name_cf = base::mac::CFCast<CFStringRef>(
        CFDictionaryGetValue(block_dictionary, CFSTR("Name")));
    if (!partition_name_cf) {
      DLOG(ERROR) << "Skipping block " << i << " because it has no name";
      continue;
    }
    std::string partition_name = base::SysCFStringRefToUTF8(partition_name_cf);

    if (DLOG_IS_ON(INFO) && VLOG_IS_ON(1)) {
      DVLOG(1) << "Name: " << partition_name;
      DVLOG(1) << "StartSector = " << block->start_sector()
               << ", SectorCount = " << block->sector_count()
               << ", ChunkCount = " << block->chunk_count();
      for (uint32_t j = 0; j < block->chunk_count(); ++j) {
        const UDIFBlockChunk* chunk = block->chunk(j);
        DVLOG(1) << "Chunk#" << j
                 << " type = " << std::hex << static_cast<uint32_t>(chunk->type)
                 << ", StartSector = " << std::dec << chunk->start_sector
                 << ", SectorCount = " << chunk->sector_count
                 << ", CompressOffset = " << chunk->compressed_offset
                 << ", CompressLen = " << chunk->compressed_length;
      }
    }

    blocks_.push_back(block.Pass());
    partition_names_.push_back(partition_name);
  }

  return true;
}

bool UDIFParser::ReadBlockChunk(const UDIFBlockChunk* chunk,
                                std::vector<uint8_t>* decompressed_data) {
  UDIFBlockChunkReadStream chunk_read_stream(stream_, block_size_, chunk);
  decompressed_data->resize(chunk_read_stream.length_in_bytes());
  return chunk_read_stream.ReadExact(&(*decompressed_data)[0],
                                     decompressed_data->size());
}

namespace {

UDIFPartitionReadStream::UDIFPartitionReadStream(
    ReadStream* stream,
    uint16_t block_size,
    const UDIFBlock* partition_block)
    : stream_(stream),
      block_size_(block_size),
      block_(partition_block),
      current_chunk_(0),
      chunk_stream_() {
}

UDIFPartitionReadStream::~UDIFPartitionReadStream() {}

bool UDIFPartitionReadStream::Read(uint8_t* buffer,
                                   size_t buffer_size,
                                   size_t* bytes_read) {
  size_t buffer_space_remaining = buffer_size;
  *bytes_read = 0;

  for (uint32_t i = current_chunk_; i < block_->chunk_count(); ++i) {
    const UDIFBlockChunk* chunk = block_->chunk(i);

    // If this is the last block chunk, then the read is complete.
    if (chunk->type == UDIFBlockChunk::Type::LAST_BLOCK) {
      break;
    }

    // If the buffer is full, then the read is complete.
    if (buffer_space_remaining == 0)
      break;

    // A chunk stream may exist if the last read from this chunk was partial,
    // or if the stream was Seek()ed.
    if (!chunk_stream_) {
      chunk_stream_.reset(
          new UDIFBlockChunkReadStream(stream_, block_size_, chunk));
    }
    DCHECK_EQ(chunk, chunk_stream_->chunk());

    size_t chunk_bytes_read = 0;
    if (!chunk_stream_->Read(&buffer[buffer_size - buffer_space_remaining],
                             buffer_space_remaining,
                             &chunk_bytes_read)) {
      DLOG(ERROR) << "Failed to read " << buffer_space_remaining << " bytes "
                  << "from chunk " << i;
      return false;
    }
    *bytes_read += chunk_bytes_read;
    buffer_space_remaining -= chunk_bytes_read;

    if (chunk_stream_->IsAtEnd()) {
      chunk_stream_.reset();
      ++current_chunk_;
    }
  }

  return true;
}

off_t UDIFPartitionReadStream::Seek(off_t offset, int whence) {
  // Translate SEEK_END to SEEK_SET. SEEK_CUR is not currently supported.
  if (whence == SEEK_END) {
    base::CheckedNumeric<off_t> safe_offset(block_->sector_count());
    safe_offset *= block_size_;
    safe_offset += offset;
    if (!safe_offset.IsValid()) {
      DLOG(ERROR) << "Seek offset overflows";
      return -1;
    }
    offset = safe_offset.ValueOrDie();
  } else if (whence != SEEK_SET) {
    DCHECK_EQ(SEEK_SET, whence);
  }

  uint64_t sector = offset / block_size_;

  // Find the chunk for this sector.
  uint32_t chunk_number = 0;
  const UDIFBlockChunk* chunk = nullptr;
  for (uint32_t i = 0; i < block_->chunk_count(); ++i) {
    const UDIFBlockChunk* chunk_it = block_->chunk(i);
    // This assumes that all the chunks are ordered by sector.
    if (i != 0) {
      DLOG_IF(ERROR,
              chunk_it->start_sector < block_->chunk(i - 1)->start_sector)
          << "Chunks are not ordered by sector at chunk " << i
          << " , previous start_sector = "
          << block_->chunk(i - 1)->start_sector << ", current = "
          << chunk_it->start_sector;
    }
    if (sector >= chunk_it->start_sector) {
      chunk = chunk_it;
      chunk_number = i;
    } else {
      break;
    }
  }
  if (!chunk) {
    DLOG(ERROR) << "Failed to Seek to partition offset " << offset;
    return -1;
  }

  // Compute the offset into the chunk.
  uint64_t offset_in_sector = offset % block_size_;
  uint64_t start_sector = sector - chunk->start_sector;
  base::CheckedNumeric<uint64_t> decompress_read_offset(start_sector);
  decompress_read_offset *= block_size_;
  decompress_read_offset += offset_in_sector;

  if (!decompress_read_offset.IsValid()) {
    DLOG(ERROR) << "Partition decompress read offset overflows";
    return -1;
  }

  if (!chunk_stream_ || chunk != chunk_stream_->chunk()) {
    chunk_stream_.reset(
        new UDIFBlockChunkReadStream(stream_, block_size_, chunk));
  }
  current_chunk_ = chunk_number;
  chunk_stream_->Seek(decompress_read_offset.ValueOrDie(), SEEK_SET);

  return offset;
}

UDIFBlockChunkReadStream::UDIFBlockChunkReadStream(ReadStream* stream,
                                                   uint16_t block_size,
                                                   const UDIFBlockChunk* chunk)
    : stream_(stream),
      chunk_(chunk),
      length_in_bytes_(chunk->sector_count * block_size),
      offset_(0),
      decompress_buffer_(),
      did_decompress_(false),
      decompressor_() {
  // Make sure the multiplication above did not overflow.
  CHECK(length_in_bytes_ == 0 || length_in_bytes_ >= block_size);
}

UDIFBlockChunkReadStream::~UDIFBlockChunkReadStream() {
}

bool UDIFBlockChunkReadStream::Read(uint8_t* buffer,
                                    size_t buffer_size,
                                    size_t* bytes_read) {
  switch (chunk_->type) {
    case UDIFBlockChunk::Type::ZERO_FILL:
    case UDIFBlockChunk::Type::IGNORED:
      return CopyOutZeros(buffer, buffer_size, bytes_read);
    case UDIFBlockChunk::Type::UNCOMPRESSED:
      return CopyOutUncompressed(buffer, buffer_size, bytes_read);
    case UDIFBlockChunk::Type::COMPRESS_ADC:
      return HandleADC(buffer, buffer_size, bytes_read);
    case UDIFBlockChunk::Type::COMPRESS_ZLIB:
      return HandleZLib(buffer, buffer_size, bytes_read);
    case UDIFBlockChunk::Type::COMPRESSS_BZ2:
      return HandleBZ2(buffer, buffer_size, bytes_read);
    case UDIFBlockChunk::Type::COMMENT:
      NOTREACHED();
      break;
    case UDIFBlockChunk::Type::LAST_BLOCK:
      *bytes_read = 0;
      return true;
  }
  return false;
}

off_t UDIFBlockChunkReadStream::Seek(off_t offset, int whence) {
  DCHECK_EQ(SEEK_SET, whence);
  DCHECK_LT(static_cast<uint64_t>(offset), length_in_bytes_);
  offset_ = offset;
  return offset_;
}

bool UDIFBlockChunkReadStream::CopyOutZeros(uint8_t* buffer,
                                            size_t buffer_size,
                                            size_t* bytes_read) {
  *bytes_read = std::min(buffer_size, length_in_bytes_ - offset_);
  bzero(buffer, *bytes_read);
  offset_ += *bytes_read;
  return true;
}

bool UDIFBlockChunkReadStream::CopyOutUncompressed(uint8_t* buffer,
                                                   size_t buffer_size,
                                                   size_t* bytes_read) {
  *bytes_read = std::min(buffer_size, length_in_bytes_ - offset_);

  if (*bytes_read == 0)
    return true;

  uint64_t offset = chunk_->compressed_offset + offset_;
  if (stream_->Seek(offset, SEEK_SET) == -1)
    return false;

  bool rv = stream_->Read(buffer, *bytes_read, bytes_read);
  if (rv)
    offset_ += *bytes_read;
  else
    DLOG(ERROR) << "Failed to read uncompressed chunk data";
  return rv;
}

bool UDIFBlockChunkReadStream::CopyOutDecompressed(uint8_t* buffer,
                                                   size_t buffer_size,
                                                   size_t* bytes_read) {
  DCHECK(did_decompress_);
  *bytes_read = std::min(buffer_size, decompress_buffer_.size() - offset_);
  memcpy(buffer, &decompress_buffer_[offset_], *bytes_read);
  offset_ += *bytes_read;
  return true;
}

bool UDIFBlockChunkReadStream::HandleADC(uint8_t* buffer,
                                         size_t buffer_size,
                                         size_t* bytes_read) {
  // TODO(rsesek): Implement ADC handling.
  NOTIMPLEMENTED();
  return false;
}

bool UDIFBlockChunkReadStream::HandleZLib(uint8_t* buffer,
                                          size_t buffer_size,
                                          size_t* bytes_read) {
  if (!did_decompress_) {
    if (stream_->Seek(chunk_->compressed_offset, SEEK_SET) == -1) {
      return false;
    }

    std::vector<uint8_t> compressed_data(chunk_->compressed_length, 0);
    if (!stream_->ReadExact(&compressed_data[0], compressed_data.size())) {
      DLOG(ERROR) << "Failed to read chunk compressed data at "
                  << chunk_->compressed_offset;
      return false;
    }

    if (inflateInit(&decompressor_.zlib) != Z_OK) {
      DLOG(ERROR) << "Failed to initialize zlib";
      return false;
    }

    decompress_buffer_.resize(length_in_bytes_);
    decompressor_.zlib.next_in = &compressed_data[0];
    decompressor_.zlib.avail_in = compressed_data.size();
    decompressor_.zlib.next_out = &decompress_buffer_[0];
    decompressor_.zlib.avail_out = decompress_buffer_.size();

    int rv = inflate(&decompressor_.zlib, Z_FINISH);
    inflateEnd(&decompressor_.zlib);

    if (rv != Z_STREAM_END) {
      DLOG(ERROR) << "Failed to inflate data, error = " << rv;
      return false;
    }

    did_decompress_ = true;
  }

  return CopyOutDecompressed(buffer, buffer_size, bytes_read);
}

bool UDIFBlockChunkReadStream::HandleBZ2(uint8_t* buffer,
                                          size_t buffer_size,
                                          size_t* bytes_read) {
  // TODO(rsesek): Implement bz2 handling.
  NOTIMPLEMENTED();
  return false;
}

}  // namespace

}  // namespace dmg
}  // namespace safe_browsing
