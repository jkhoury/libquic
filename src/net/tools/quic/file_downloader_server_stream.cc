// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "net/tools/quic/file_downloader_server_stream.h"

#include "net/tools/quic/quic_server_session.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"

using base::StringPiece;
using base::StringToInt;
using std::string;

namespace net {
namespace tools {

FileDownloaderServerStream::FileDownloaderServerStream(QuicStreamId id,
                                           QuicServerSession* session)
    : ReliableQuicStream(id, session),
      fd_(-1), file_mmap_address_(NULL), file_size_(0), sent_bytes_(0) {
}

FileDownloaderServerStream::~FileDownloaderServerStream() {
  if (fd_ != -1) {
    munmap(file_mmap_address_, file_size_);
    close(fd_);
  }
}

uint32 FileDownloaderServerStream::ProcessRawData(const char* data, uint32 data_len) {
  request_.append(data, data_len);
  DVLOG(1) << "################ Processed " << data_len << " bytes for stream " << id();
  DVLOG(1) << request_;
  return data_len;
}

void FileDownloaderServerStream::OnFinRead() {
  DVLOG(1) << "################ FIN received for stream ################" << id();
  ReliableQuicStream::OnFinRead();
  if (write_side_closed() || fin_buffered()) {
    return;
  }

  StartFileDownload();
}

void FileDownloaderServerStream::StartFileDownload() {
  DVLOG(1) << "Sending file (" << request_ << ") on stream " << id();

  if (!MapFileIntoMemory()) {
    // Just close the stream.
    WriteOrBufferData("", true, nullptr);
    return;
  }

//  std::string data(200000, 0);
//  WriteOrBufferData(data, true, nullptr);

  SendNextFileBlock();
}

void FileDownloaderServerStream::SendNextFileBlock() {
  struct iovec iov = {
    static_cast<uint8_t*>(file_mmap_address_) + sent_bytes_,
    file_size_ - sent_bytes_
  };

  // TODO(dimm): do we need to be notified when all data has been sent?
  QuicConsumedData consumed_data = WritevData(&iov, 1, true, nullptr);
  sent_bytes_ += consumed_data.bytes_consumed;
  DVLOG(1) << "===> Sent " << sent_bytes_ << " bytes";

  if (sent_bytes_ == file_size_) {
    DVLOG(1) << "=====> Done!";
    // TODO(dimm): perhaps unmap/close the file here. Currently it will be done
    // when the stream closes.
  }
}

void FileDownloaderServerStream::OnCanWrite() {
  DVLOG(1) << "@@@@@ OnCanWrite";
  if (sent_bytes_ < file_size_) {
    SendNextFileBlock();
  }
}

// TODO(dimm): We map a complete file into memory. This may be problematic on
// 32bit systems and big files. If this proves to be a problem, we can map
// a big file in chunks (one after another) or use another interface
// (e.g. read() at the cost of extra copying).
bool FileDownloaderServerStream::MapFileIntoMemory() {
  fd_ = open(request_.c_str(), O_RDONLY);
  if (fd_ == -1) {
    DLOG(ERROR) << "Failed to open() " << request_;
    return false;
  }

  struct stat file_stats;
  if (fstat(fd_, &file_stats) == -1) {
    DLOG(ERROR) << "Failed to stat() " << request_;
    close(fd_);
    fd_ = -1;
    return false;
  }
  file_size_ = file_stats.st_size;

  file_mmap_address_ = mmap(NULL, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (file_mmap_address_ == MAP_FAILED) {
    DLOG(ERROR) << "Failed to mmap() " << request_;
    close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

}  // namespace tools
}  // namespace net
