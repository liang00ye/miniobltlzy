/* Copyright (c) 2021-2022 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by wangyunlai on 2024/01/31
//

#include <mutex>
#include "storage/clog/log_buffer.h"
#include "storage/clog/log_file.h"

using namespace std;
using namespace common;

RC LogEntryBuffer::init(LSN lsn)
{
  current_lsn_.store(lsn);
  flushed_lsn_.store(lsn);
  return RC::SUCCESS;
}

RC LogEntryBuffer::append(LSN &lsn, LogModule::Id module_id, unique_ptr<char[]> data, int32_t size)
{
  return append(lsn, LogModule(module_id), std::move(data), size);
}

RC LogEntryBuffer::append(LSN &lsn, LogModule module, unique_ptr<char[]> data, int32_t size)
{
  lock_guard guard(mutex_);
  lsn = ++current_lsn_;

  LogEntry entry;
  RC rc = entry.init(lsn, module, std::move(data), size);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to init log entry. rc=%s", strrc(rc));
    return rc;
  }

  entries_.emplace_back(std::move(entry));
  bytes_ += entry.total_size();
  return RC::SUCCESS;
}

RC LogEntryBuffer::flush(LogFileWriter &writer, int &count)
{
  count = 0;

  while (entry_number() > 0) {
    LogEntry entry;
    {
      lock_guard guard(mutex_);
      if (entries_.empty()) {
        break;
      }

      entry = std::move(entries_.front());
      entries_.pop_front();
      bytes_ -= entry.total_size();
    }
    
    RC rc = writer.write(entry);
    if (OB_FAIL(rc)) {
      lock_guard guard(mutex_);
      entries_.emplace_front(std::move(entry));
      return rc;
    } else {
      ++count;
      flushed_lsn_ = entry.lsn();
    }
  }
  
  return RC::SUCCESS;
}

int64_t LogEntryBuffer::bytes() const
{
  return bytes_.load();
}

int32_t LogEntryBuffer::entry_number() const
{
  return entries_.size();
}